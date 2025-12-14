/*
 * ESP32 Temperature Monitoring System
 * Configuration Manager Implementation
 */

#include "config_manager.h"
#include <SPIFFS.h>

namespace {
constexpr const char* PREFS_NS = "tempmon";
constexpr const char* PREFS_KEY = "cfg";
constexpr uint32_t CFG_MAGIC = 0x544D4346; // 'TMCF'
constexpr uint16_t CFG_VERSION = 1;
}

// Global instance
ConfigManager configManager;

// ============================================================================
// Public Methods
// ============================================================================

bool ConfigManager::begin() {
    _isDirty = false;
    _initialized = false;
    _prefsOpen = false;
    
    // Initialize SPIFFS
    if (!SPIFFS.begin(true)) {
        Serial.println(F("[ConfigManager] Failed to mount SPIFFS"));
        return false;
    }
    
    Serial.println(F("[ConfigManager] SPIFFS mounted successfully"));
    
    // Print SPIFFS info
    size_t totalBytes = SPIFFS.totalBytes();
    size_t usedBytes = SPIFFS.usedBytes();
    Serial.printf("[ConfigManager] SPIFFS: %u/%u bytes used\n", usedBytes, totalBytes);
    
    _initialized = true;

    // Initialize NVS preferences
    if (!_prefs.begin(PREFS_NS, false)) {
        Serial.println(F("[ConfigManager] Failed to open NVS preferences"));
        return false;
    }
    _prefsOpen = true;
    
    // Try to load existing configuration (NVS)
    if (!loadFromNVS()) {
        // One-time legacy import from SPIFFS (/config.json)
        if (loadLegacyFromSPIFFS()) {
            Serial.println(F("[ConfigManager] Imported legacy SPIFFS config into NVS"));
            saveToNVS();
        } else {
            Serial.println(F("[ConfigManager] No valid config found, using defaults"));
            resetToDefaults();
            saveToNVS();
        }
    }
    
    return true;
}

bool ConfigManager::load() {
    return loadFromNVS();
}

bool ConfigManager::save() {
    return saveToNVS();
}

void ConfigManager::resetToDefaults() {
    _wifiConfig = WiFiConfig();
    _mqttConfig = MQTTConfig();
    _systemConfig = SystemConfig();
    
    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        _sensorConfigs[i] = SensorConfig();
    }
    
    _isDirty = true;
    Serial.println(F("[ConfigManager] Reset to defaults"));
}

bool ConfigManager::loadFromNVS() {
    if (!_initialized || !_prefsOpen) {
        return false;
    }

    size_t len = _prefs.getBytesLength(PREFS_KEY);
    if (len != sizeof(PersistentConfigBlob)) {
        Serial.println(F("[ConfigManager] NVS config not present (or size mismatch)"));
        return false;
    }

    PersistentConfigBlob blob{};
    size_t read = _prefs.getBytes(PREFS_KEY, &blob, sizeof(blob));
    if (read != sizeof(blob)) {
        Serial.println(F("[ConfigManager] Failed to read NVS config"));
        return false;
    }
    if (blob.magic != CFG_MAGIC || blob.version != CFG_VERSION) {
        Serial.println(F("[ConfigManager] NVS config invalid (magic/version)"));
        return false;
    }

    _wifiConfig = blob.wifi;
    _mqttConfig = blob.mqtt;
    _systemConfig = blob.system;
    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        _sensorConfigs[i] = blob.sensors[i];
    }

    Serial.println(F("[ConfigManager] Configuration loaded from NVS"));
    _isDirty = false;
    return true;
}

bool ConfigManager::saveToNVS() {
    if (!_initialized || !_prefsOpen) {
        return false;
    }

    PersistentConfigBlob blob{};
    blob.magic = CFG_MAGIC;
    blob.version = CFG_VERSION;
    blob.wifi = _wifiConfig;
    blob.mqtt = _mqttConfig;
    blob.system = _systemConfig;
    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        blob.sensors[i] = _sensorConfigs[i];
    }

    size_t written = _prefs.putBytes(PREFS_KEY, &blob, sizeof(blob));
    if (written != sizeof(blob)) {
        Serial.println(F("[ConfigManager] Failed to write NVS config"));
        return false;
    }

    Serial.println(F("[ConfigManager] Configuration saved to NVS"));
    _isDirty = false;
    return true;
}

bool ConfigManager::loadLegacyFromSPIFFS() {
    if (!_initialized) {
        return false;
    }

    File file = SPIFFS.open(CONFIG_FILE_PATH, "r");
    if (!file) {
        Serial.println(F("[ConfigManager] Legacy config file not found in SPIFFS"));
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.printf("[ConfigManager] Legacy JSON parse error: %s\n", error.c_str());
        return false;
    }

    if (!fromJson(doc)) {
        Serial.println(F("[ConfigManager] Failed to import legacy SPIFFS configuration"));
        return false;
    }

    _isDirty = true;
    return true;
}

SensorConfig* ConfigManager::getSensorConfig(uint8_t index) {
    if (index >= MAX_SENSORS) {
        return nullptr;
    }
    return &_sensorConfigs[index];
}

const SensorConfig* ConfigManager::getSensorConfig(uint8_t index) const {
    if (index >= MAX_SENSORS) {
        return nullptr;
    }
    return &_sensorConfigs[index];
}

SensorConfig* ConfigManager::getSensorConfigByAddress(const char* address) {
    if (!address) {
        return nullptr;
    }
    
    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        if (_sensorConfigs[i].isConfigured && 
            strcmp(_sensorConfigs[i].address, address) == 0) {
            return &_sensorConfigs[i];
        }
    }
    
    return nullptr;
}

const SensorConfig* ConfigManager::getSensorConfigByAddress(const char* address) const {
    if (!address) {
        return nullptr;
    }
    
    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        if (_sensorConfigs[i].isConfigured && 
            strcmp(_sensorConfigs[i].address, address) == 0) {
            return &_sensorConfigs[i];
        }
    }
    
    return nullptr;
}

SensorConfig* ConfigManager::findOrCreateSensorConfig(const char* address) {
    // First, try to find existing config
    SensorConfig* existing = getSensorConfigByAddress(address);
    if (existing) {
        return existing;
    }
    
    // Find first empty slot
    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        if (!_sensorConfigs[i].isConfigured) {
            strncpy(_sensorConfigs[i].address, address, SENSOR_ADDR_STR_LEN - 1);
            _sensorConfigs[i].address[SENSOR_ADDR_STR_LEN - 1] = '\0';
            
            // Generate default name
            snprintf(_sensorConfigs[i].name, SENSOR_NAME_MAX_LEN, "Sensor %d", i + 1);
            
            _sensorConfigs[i].isConfigured = true;
            _isDirty = true;
            
            return &_sensorConfigs[i];
        }
    }
    
    // No space available
    return nullptr;
}

uint8_t ConfigManager::getConfiguredSensorCount() const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        if (_sensorConfigs[i].isConfigured) {
            count++;
        }
    }
    return count;
}

void ConfigManager::toJson(JsonDocument& doc) const {
    doc.clear();
    
    // System configuration
    JsonObject sys = doc["system"].to<JsonObject>();
    sys["deviceName"] = _systemConfig.deviceName;
    sys["readInterval"] = _systemConfig.readInterval;
    sys["celsiusUnits"] = _systemConfig.celsiusUnits;
    sys["utcOffset"] = _systemConfig.utcOffset;
    sys["otaEnabled"] = _systemConfig.otaEnabled;
    
    // WiFi configuration
    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["ssid"] = _wifiConfig.ssid;
    wifi["password"] = _wifiConfig.password;
    wifi["dhcp"] = _wifiConfig.dhcp;
    wifi["staticIP"] = _wifiConfig.staticIP;
    wifi["gateway"] = _wifiConfig.gateway;
    wifi["subnet"] = _wifiConfig.subnet;
    wifi["dns"] = _wifiConfig.dns;
    
    // MQTT configuration
    JsonObject mqtt = doc["mqtt"].to<JsonObject>();
    mqtt["server"] = _mqttConfig.server;
    mqtt["port"] = _mqttConfig.port;
    mqtt["username"] = _mqttConfig.username;
    mqtt["password"] = _mqttConfig.password;
    mqtt["topicPrefix"] = _mqttConfig.topicPrefix;
    mqtt["enabled"] = _mqttConfig.enabled;
    mqtt["publishOnChange"] = _mqttConfig.publishOnChange;
    mqtt["publishThreshold"] = _mqttConfig.publishThreshold;
    mqtt["publishInterval"] = _mqttConfig.publishInterval;
    
    // Sensor configurations
    JsonArray sensors = doc["sensors"].to<JsonArray>();
    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        if (_sensorConfigs[i].isConfigured) {
            JsonObject sensor = sensors.add<JsonObject>();
            sensorConfigToJson(_sensorConfigs[i], sensor);
        }
    }
}

bool ConfigManager::fromJson(const JsonDocument& doc) {
    // System configuration
    if (doc["system"].is<JsonObjectConst>()) {
        JsonObjectConst sys = doc["system"];
        
        if (sys["deviceName"].is<const char*>()) {
            strncpy(_systemConfig.deviceName, sys["deviceName"] | "TempMonitor", 32);
        }
        _systemConfig.readInterval = sys["readInterval"] | 2;
        _systemConfig.celsiusUnits = sys["celsiusUnits"] | true;
        _systemConfig.utcOffset = sys["utcOffset"] | 0;
        _systemConfig.otaEnabled = sys["otaEnabled"] | true;
    }
    
    // WiFi configuration
    if (doc["wifi"].is<JsonObjectConst>()) {
        JsonObjectConst wifi = doc["wifi"];
        
        strncpy(_wifiConfig.ssid, wifi["ssid"] | "", 32);
        strncpy(_wifiConfig.password, wifi["password"] | "", 64);
        _wifiConfig.dhcp = wifi["dhcp"] | true;
        strncpy(_wifiConfig.staticIP, wifi["staticIP"] | "192.168.1.100", 15);
        strncpy(_wifiConfig.gateway, wifi["gateway"] | "192.168.1.1", 15);
        strncpy(_wifiConfig.subnet, wifi["subnet"] | "255.255.255.0", 15);
        strncpy(_wifiConfig.dns, wifi["dns"] | "8.8.8.8", 15);
    }
    
    // MQTT configuration
    if (doc["mqtt"].is<JsonObjectConst>()) {
        JsonObjectConst mqtt = doc["mqtt"];
        
        strncpy(_mqttConfig.server, mqtt["server"] | "", 64);
        _mqttConfig.port = mqtt["port"] | MQTT_DEFAULT_PORT;
        strncpy(_mqttConfig.username, mqtt["username"] | "", 32);
        strncpy(_mqttConfig.password, mqtt["password"] | "", 64);
        strncpy(_mqttConfig.topicPrefix, mqtt["topicPrefix"] | MQTT_TOPIC_BASE, 64);
        _mqttConfig.enabled = mqtt["enabled"] | false;
        _mqttConfig.publishOnChange = mqtt["publishOnChange"] | true;
        _mqttConfig.publishThreshold = mqtt["publishThreshold"] | 0.5f;
        _mqttConfig.publishInterval = mqtt["publishInterval"] | 10;
    }
    
    // Sensor configurations
    // Reset all sensors first
    for (uint8_t i = 0; i < MAX_SENSORS; i++) {
        _sensorConfigs[i] = SensorConfig();
    }
    
    if (doc["sensors"].is<JsonArrayConst>()) {
        JsonArrayConst sensors = doc["sensors"];
        uint8_t idx = 0;
        
        for (JsonObjectConst sensor : sensors) {
            if (idx >= MAX_SENSORS) break;
            sensorConfigFromJson(_sensorConfigs[idx], sensor);
            idx++;
        }
    }
    
    return true;
}

// ============================================================================
// Private Methods
// ============================================================================

void ConfigManager::sensorConfigToJson(const SensorConfig& config, JsonObject& obj) const {
    obj["address"] = config.address;
    obj["name"] = config.name;
    obj["calibrationOffset"] = config.calibrationOffset;
    obj["thresholdLow"] = config.thresholdLow;
    obj["thresholdHigh"] = config.thresholdHigh;
    obj["alertEnabled"] = config.alertEnabled;
}

void ConfigManager::sensorConfigFromJson(SensorConfig& config, JsonObjectConst obj) {
    strncpy(config.address, obj["address"] | "", SENSOR_ADDR_STR_LEN - 1);
    config.address[SENSOR_ADDR_STR_LEN - 1] = '\0';
    
    strncpy(config.name, obj["name"] | "Sensor", SENSOR_NAME_MAX_LEN - 1);
    config.name[SENSOR_NAME_MAX_LEN - 1] = '\0';
    
    config.calibrationOffset = obj["calibrationOffset"] | 0.0f;
    config.thresholdLow = obj["thresholdLow"] | DEFAULT_THRESHOLD_LOW;
    config.thresholdHigh = obj["thresholdHigh"] | DEFAULT_THRESHOLD_HIGH;
    config.alertEnabled = obj["alertEnabled"] | true;
    config.isConfigured = true;
}
