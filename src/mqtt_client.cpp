/*
 * ESP32 Temperature Monitoring System
 * MQTT Client Implementation
 */

#include "mqtt_client.h"
#include <ArduinoJson.h>
#include "wifi_manager.h"

// Global instance
MQTTClient mqttClient;

// Static callback wrapper
static MQTTClient* _mqttInstance = nullptr;

// ============================================================================
// Constructor
// ============================================================================

MQTTClient::MQTTClient() :
    _client(_wifiClient),
    _lastConnectAttempt(0),
    _lastPublishTime(0),
    _publishCount(0),
    _haDiscoveryPublished(false),
    _reconnectRequested(false),
    _otaInProgress(false) {
    _lastError[0] = '\0';
    
    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        _lastPublishedTemp[i] = TEMP_INVALID;
    }
    
    _mqttInstance = this;
}

// ============================================================================
// Public Methods
// ============================================================================

void MQTTClient::begin() {
    Serial.println(F("[MQTT] Initializing..."));
    
    _client.setCallback(messageCallback);
    _client.setKeepAlive(MQTT_KEEP_ALIVE);
    
    // Buffer size for larger messages (HA discovery payloads can be 600+ bytes)
    _client.setBufferSize(1024);
}

void MQTTClient::setOtaMode(bool enabled) {
    _otaInProgress = enabled;
    if (enabled && _client.connected()) {
        _client.disconnect();
    }
}

void MQTTClient::update() {
    // Completely disable MQTT during OTA updates
    if (_otaInProgress) {
        return;
    }
    
    if (!isEnabled()) {
        return;
    }
    
    // Handle reconnect request from web handlers (thread-safe)
    if (_reconnectRequested) {
        _reconnectRequested = false;
        _lastConnectAttempt = millis(); // Set to now to delay reconnect
        _haDiscoveryPublished = false;
        if (_client.connected()) {
            _client.disconnect();
        }
        return; // Skip this update cycle to let disconnect complete
    }
    
    // Check WiFi connection
    if (!wifiManager.isConnected()) {
        return;
    }
    
    uint32_t now = millis();
    
    // Handle MQTT connection
    if (!_client.connected()) {
        if (now - _lastConnectAttempt >= MQTT_RECONNECT_INTERVAL) {
            _lastConnectAttempt = now;
            connect();
        }
        return;
    }
    
    // Process incoming messages
    _client.loop();
    
    // Publish Home Assistant discovery once connected
    if (!_haDiscoveryPublished) {
        publishHADiscovery();
        _haDiscoveryPublished = true;
    }
    
    // Publish temperatures (only when changed if publishOnChange is enabled)
    const MQTTConfig& config = configManager.getMQTTConfig();
    
    if (config.publishOnChange) {
        // Publish on every sensor read cycle (will only publish if temp changed)
        publishTemperatures();
    } else {
        // Fallback to interval-based publishing if publishOnChange is disabled
        uint32_t publishInterval = config.publishInterval * 1000;
        if (now - _lastPublishTime >= publishInterval) {
            publishTemperatures();
            _lastPublishTime = now;
        }
    }
}

bool MQTTClient::isEnabled() const {
    const MQTTConfig& config = configManager.getMQTTConfig();
    return config.enabled && strlen(config.server) > 0;
}

void MQTTClient::reconnect() {
    if (_otaInProgress) {
        return;
    }
    // Just set flag - actual reconnect happens in update() on main loop
    // This is safe to call from async web handlers
    _reconnectRequested = true;
}

void MQTTClient::disconnect() {
    if (_client.connected()) {
        publishStatus(false);
        _client.disconnect();
    }
}

void MQTTClient::publishTemperatures() {
    if (!_client.connected()) {
        return;
    }
    
    const MQTTConfig& config = configManager.getMQTTConfig();
    
    for (uint8_t i = 0; i < sensorManager.getSensorCount(); i++) {
        const SensorData* data = sensorManager.getSensorData(i);
        if (!data || !data->connected) {
            continue;
        }
        
        // Always publish first reading (when _lastPublishedTemp is TEMP_INVALID)
        // Then check change threshold if publishOnChange is enabled
        bool isFirstPublish = (_lastPublishedTemp[i] == TEMP_INVALID);
        bool shouldPublish = isFirstPublish || !config.publishOnChange || shouldPublishTemperature(i, data->temperature);
        
        if (!shouldPublish) {
            continue;
        }
        
        publishSensorTemperature(i);
    }
}

void MQTTClient::publishSensorTemperature(uint8_t sensorIndex) {
    if (!_client.connected()) {
        return;
    }
    
    const SensorData* data = sensorManager.getSensorData(sensorIndex);
    if (!data) {
        return;
    }
    
    const SensorConfig* config = configManager.getSensorConfigByAddress(data->addressStr);
    
    // Build topic
    char topic[128];
    buildSensorTopic(topic, sizeof(topic), sensorIndex, TOPIC_TEMPERATURE);
    
    // Build JSON payload
    JsonDocument doc;
    doc["temperature"] = round(data->temperature * 100) / 100.0;
    doc["raw_temperature"] = round(data->rawTemperature * 100) / 100.0;
    doc["unit"] = configManager.getSystemConfig().celsiusUnits ? "C" : "F";
    doc["alarm"] = alarmStateToString(data->alarmState);
    doc["connected"] = data->connected;
    
    if (config) {
        doc["name"] = config->name;
        doc["address"] = config->address;
    }
    
    char payload[256];
    serializeJson(doc, payload, sizeof(payload));
    
    if (_client.publish(topic, payload)) {
        _publishCount++;
        _lastPublishedTemp[sensorIndex] = data->temperature;
    } else {
        strcpy(_lastError, "Failed to publish temperature");
        Serial.printf("[MQTT] Failed to publish to %s\n", topic);
    }
}

void MQTTClient::publishAlarm(uint8_t sensorIndex, AlarmState state, float temperature) {
    if (!_client.connected()) {
        return;
    }
    
    const SensorData* data = sensorManager.getSensorData(sensorIndex);
    const SensorConfig* config = data ? 
        configManager.getSensorConfigByAddress(data->addressStr) : nullptr;
    
    // Build topic
    char topic[128];
    buildSensorTopic(topic, sizeof(topic), sensorIndex, TOPIC_ALARM);
    
    // Build JSON payload
    JsonDocument doc;
    doc["alarm"] = alarmStateToString(state);
    doc["temperature"] = round(temperature * 100) / 100.0;
    doc["timestamp"] = millis() / 1000;
    
    if (config) {
        doc["name"] = config->name;
        doc["address"] = config->address;
        doc["threshold_low"] = config->thresholdLow;
        doc["threshold_high"] = config->thresholdHigh;
    }
    
    char payload[256];
    serializeJson(doc, payload, sizeof(payload));
    
    // Publish with retain flag for alarms
    if (_client.publish(topic, payload, true)) {
        _publishCount++;
        Serial.printf("[MQTT] Published alarm: %s = %s\n", topic, alarmStateToString(state));
    } else {
        strcpy(_lastError, "Failed to publish alarm");
    }
}

void MQTTClient::publishStatus(bool online) {
    if (!_client.connected() && online) {
        return;
    }
    
    const MQTTConfig& mqttConfig = configManager.getMQTTConfig();
    const SystemConfig& sysConfig = configManager.getSystemConfig();
    
    // Build topic
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/%s/%s",
        mqttConfig.topicPrefix,
        sysConfig.deviceName,
        TOPIC_STATUS
    );
    
    // Build JSON payload
    JsonDocument doc;
    doc["online"] = online;
    doc["ip"] = wifiManager.getIP().toString();
    doc["rssi"] = wifiManager.getRSSI();
    doc["uptime"] = millis() / 1000;
    doc["sensors"] = sensorManager.getSensorCount();
    doc["firmware"] = FIRMWARE_VERSION;
    
    char payload[256];
    serializeJson(doc, payload, sizeof(payload));
    
    // Publish with retain flag
    _client.publish(topic, payload, true);
    _publishCount++;
}

void MQTTClient::publishHADiscovery() {
    if (!_client.connected()) {
        return;
    }
    
    Serial.println(F("[MQTT] Publishing Home Assistant discovery..."));
    
    for (uint8_t i = 0; i < sensorManager.getSensorCount(); i++) {
        publishHADiscoverySensor(i);
    }
}

// ============================================================================
// Private Methods
// ============================================================================

bool MQTTClient::connect() {
    if (_otaInProgress) {
        return false;
    }
    const MQTTConfig& config = configManager.getMQTTConfig();
    const SystemConfig& sysConfig = configManager.getSystemConfig();
    
    // Validate config before attempting connection
    if (strlen(config.server) == 0) {
        Serial.println(F("[MQTT] No server configured"));
        return false;
    }
    
    Serial.printf("[MQTT] Connecting to %s:%d\n", config.server, config.port);
    
    // Ensure client is properly set up with the WiFi client
    _client.setClient(_wifiClient);
    _client.setServer(config.server, config.port);
    
    // Generate client ID
    char clientId[32];
    snprintf(clientId, sizeof(clientId), "%s%08X", 
        MQTT_CLIENT_PREFIX, (uint32_t)ESP.getEfuseMac());
    
    // Build last will topic
    char willTopic[128];
    snprintf(willTopic, sizeof(willTopic), "%s/%s/%s",
        config.topicPrefix,
        sysConfig.deviceName,
        TOPIC_STATUS
    );
    
    // Last will message
    const char* willMessage = "{\"online\":false}";
    
    bool connected = false;
    
    if (strlen(config.username) > 0) {
        connected = _client.connect(
            clientId,
            config.username,
            config.password,
            willTopic,
            0,      // QoS
            true,   // Retain
            willMessage
        );
    } else {
        connected = _client.connect(
            clientId,
            willTopic,
            0,
            true,
            willMessage
        );
    }
    
    if (connected) {
        Serial.println(F("[MQTT] Connected"));
        
        // Subscribe to command topic
        char cmdTopic[128];
        snprintf(cmdTopic, sizeof(cmdTopic), "%s/%s/%s/#",
            config.topicPrefix,
            sysConfig.deviceName,
            TOPIC_COMMAND
        );
        _client.subscribe(cmdTopic);
        
        // Publish online status
        publishStatus(true);
        
        _lastError[0] = '\0';
        return true;
    } else {
        int state = _client.state();
        snprintf(_lastError, sizeof(_lastError), "Connection failed: %d", state);
        Serial.printf("[MQTT] %s\n", _lastError);
        return false;
    }
}

void MQTTClient::buildTopic(char* buffer, size_t bufferSize, ...) {
    const MQTTConfig& config = configManager.getMQTTConfig();
    const SystemConfig& sysConfig = configManager.getSystemConfig();
    
    // Start with prefix/device_name
    snprintf(buffer, bufferSize, "%s/%s", config.topicPrefix, sysConfig.deviceName);
    
    va_list args;
    va_start(args, bufferSize);
    
    const char* part;
    while ((part = va_arg(args, const char*)) != nullptr) {
        size_t len = strlen(buffer);
        snprintf(buffer + len, bufferSize - len, "/%s", part);
    }
    
    va_end(args);
}

void MQTTClient::buildSensorTopic(char* buffer, size_t bufferSize,
                                   uint8_t sensorIndex, const char* suffix) {
    const MQTTConfig& config = configManager.getMQTTConfig();
    const SystemConfig& sysConfig = configManager.getSystemConfig();
    
    const SensorData* data = sensorManager.getSensorData(sensorIndex);
    const SensorConfig* sensorConfig = data ?
        configManager.getSensorConfigByAddress(data->addressStr) : nullptr;
    
    // Use sensor name (sanitized) or index
    char sensorId[SENSOR_NAME_MAX_LEN];
    if (sensorConfig && strlen(sensorConfig->name) > 0) {
        strncpy(sensorId, sensorConfig->name, sizeof(sensorId) - 1);
        sensorId[sizeof(sensorId) - 1] = '\0';
        
        // Sanitize: replace spaces and special chars with underscores
        for (char* p = sensorId; *p; p++) {
            if (*p == ' ' || *p == '/' || *p == '#' || *p == '+') {
                *p = '_';
            }
        }
    } else {
        snprintf(sensorId, sizeof(sensorId), "sensor_%d", sensorIndex);
    }
    
    snprintf(buffer, bufferSize, "%s/%s/%s/%s/%s",
        config.topicPrefix,
        sysConfig.deviceName,
        TOPIC_SENSOR,
        sensorId,
        suffix
    );
}

void MQTTClient::messageCallback(char* topic, byte* payload, unsigned int length) {
    if (_mqttInstance) {
        // Null-terminate payload
        char message[256];
        size_t copyLen = min((size_t)length, sizeof(message) - 1);
        memcpy(message, payload, copyLen);
        message[copyLen] = '\0';
        
        _mqttInstance->handleMessage(topic, message);
    }
}

void MQTTClient::handleMessage(const char* topic, const char* payload) {
    Serial.printf("[MQTT] Received: %s = %s\n", topic, payload);
    
    // Parse command from topic
    // Expected format: {prefix}/{device}/cmd/{command}
    
    if (strstr(topic, "/cmd/calibrate")) {
        // Calibration command
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        
        if (!error && doc["reference_temp"].is<JsonVariant>()) {
            float refTemp = doc["reference_temp"];
            sensorManager.calibrateAll(refTemp);
            Serial.printf("[MQTT] Calibration triggered with reference: %.2f\n", refTemp);
        }
    }
    else if (strstr(topic, "/cmd/rescan")) {
        // Rescan sensors
        sensorManager.requestRescan();
        Serial.println(F("[MQTT] Sensor rescan requested"));
    }
    else if (strstr(topic, "/cmd/reboot")) {
        // Reboot device
        Serial.println(F("[MQTT] Reboot requested"));
        delay(1000);
        ESP.restart();
    }
}

bool MQTTClient::shouldPublishTemperature(uint8_t sensorIndex, float temperature) {
    if (sensorIndex >= MAX_SENSORS) {
        return false;
    }
    
    if (_lastPublishedTemp[sensorIndex] == TEMP_INVALID) {
        return true;
    }
    
    const MQTTConfig& config = configManager.getMQTTConfig();
    float diff = abs(temperature - _lastPublishedTemp[sensorIndex]);
    
    return diff >= config.publishThreshold;
}

void MQTTClient::publishHADiscoverySensor(uint8_t sensorIndex) {
    const SensorData* data = sensorManager.getSensorData(sensorIndex);
    if (!data) {
        return;
    }
    
    const SensorConfig* sensorConfig = configManager.getSensorConfigByAddress(data->addressStr);
    const SystemConfig& sysConfig = configManager.getSystemConfig();
    const MQTTConfig& mqttConfig = configManager.getMQTTConfig();
    
    // Generate unique ID (sensor address is globally unique)
    char uniqueId[32];
    snprintf(uniqueId, sizeof(uniqueId), "sensor_%s", data->addressStr);
    
    // Sensor name
    char sensorName[64];
    if (sensorConfig && strlen(sensorConfig->name) > 0) {
        snprintf(sensorName, sizeof(sensorName), "%s %s", 
            sysConfig.deviceName, sensorConfig->name);
    } else {
        snprintf(sensorName, sizeof(sensorName), "%s Sensor %d", 
            sysConfig.deviceName, sensorIndex + 1);
    }
    
    // State topic
    char stateTopic[128];
    buildSensorTopic(stateTopic, sizeof(stateTopic), sensorIndex, TOPIC_TEMPERATURE);
    
    // Discovery topic
    char discoveryTopic[128];
    snprintf(discoveryTopic, sizeof(discoveryTopic), 
        "%s/sensor/%s/config", HA_DISCOVERY_PREFIX, uniqueId);
    
    // Build discovery payload
    JsonDocument doc;
    doc["name"] = sensorName;
    doc["unique_id"] = uniqueId;
    doc["state_topic"] = stateTopic;
    doc["value_template"] = "{{ value_json.temperature }}";
    doc["unit_of_measurement"] = sysConfig.celsiusUnits ? "°C" : "°F";
    doc["device_class"] = "temperature";
    doc["state_class"] = "measurement";
    
    // Device info - all sensors share the same device (the ESP32 hardware)
    JsonObject device = doc["device"].to<JsonObject>();
    {
        // Stable device identifier (ESP MAC) - all sensors use the same device
        JsonArray identifiers = device["identifiers"].to<JsonArray>();
        identifiers.add(String("probe-station-") + WiFi.macAddress());

        device["name"] = sysConfig.deviceName;
        device["manufacturer"] = "martiera";
        device["model"] = "probe-station-esp32";
        device["sw_version"] = FIRMWARE_VERSION;
        device["hw_version"] = ESP.getChipModel();
        device["configuration_url"] = String("http://") + wifiManager.getIP().toString() + "/";
    }
    
    // Availability
    char availTopic[128];
    snprintf(availTopic, sizeof(availTopic), "%s/%s/%s",
        mqttConfig.topicPrefix, sysConfig.deviceName, TOPIC_STATUS);
    doc["availability_topic"] = availTopic;
    doc["availability_template"] = "{{ 'online' if value_json.online else 'offline' }}";
    
    char payload[768];
    size_t len = serializeJson(doc, payload, sizeof(payload));
    
    if (len >= sizeof(payload) - 1) {
        Serial.printf("[MQTT] WARNING: Discovery payload truncated! (%d bytes)\n", len);
    }
    
    _client.publish(discoveryTopic, payload, true);
    Serial.printf("[MQTT] Published HA discovery for sensor %d\n", sensorIndex);
}
