/*
 * ESP32 Temperature Monitoring System
 * Web Server Implementation
 */

#include <AsyncJson.h>
#include "web_server.h"
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include "wifi_manager.h"
#include "mqtt_client.h"
#include "ota_manager.h"

// Global instance
WebServer webServer;

// WebSocket update interval (ms)
constexpr uint32_t WS_UPDATE_INTERVAL = 2000;

// ============================================================================
// Constructor
// ============================================================================

WebServer::WebServer() :
    _server(WEB_SERVER_PORT),
    _ws("/ws"),
    _lastWsUpdate(0),
    _otaMode(false) {
}

// ============================================================================
// Public Methods
// ============================================================================

void WebServer::begin() {
    // WebSocket disabled to save memory (~4-8KB)
    // Using API polling instead
    
    // Setup routes
    setupRoutes();
    setupStaticFiles();
    
    // Start server
    _server.begin();
    
    Serial.println(F("[WebServer] Started (WebSocket disabled, using API polling)"));
}

void WebServer::update() {
    // WebSocket disabled - nothing to update
    // Clients poll /api/sensors instead
}

void WebServer::sendSensorUpdate() {
    // WebSocket disabled - no-op
    // Clients poll /api/sensors instead
    return;
    
    JsonDocument doc;
    doc["type"] = "sensors";
    
    JsonArray sensors = doc["data"].to<JsonArray>();
    
    for (uint8_t i = 0; i < sensorManager.getSensorCount(); i++) {
        JsonObject obj = sensors.add<JsonObject>();
        buildSensorJson(obj, i);
    }
    
    // Add summary
    JsonObject summary = doc["summary"].to<JsonObject>();
    summary["avg"] = sensorManager.getAverageTemperature();
    summary["min"] = sensorManager.getMinTemperature();
    summary["max"] = sensorManager.getMaxTemperature();
    summary["alarms"] = sensorManager.getAlarmCount();
    
    char buffer[1024];
    serializeJson(doc, buffer, sizeof(buffer));
    
    _ws.textAll(buffer);
}

void WebServer::sendNotification(const char* type, const char* message) {
    // WebSocket disabled - no-op
    return;
    
    JsonDocument doc;
    doc["type"] = "notification";
    doc["level"] = type;
    doc["message"] = message;
    doc["timestamp"] = millis() / 1000;
    
    char buffer[256];
    serializeJson(doc, buffer, sizeof(buffer));
    
    _ws.textAll(buffer);
}

void WebServer::sendUpdateNotification(AsyncWebSocketClient* client) {
    // WebSocket disabled - no-op
}

void WebServer::setOtaMode(bool enabled) {
    _otaMode = enabled;
    // WebSocket disabled - nothing to do
}

// ============================================================================
// Route Setup
// ============================================================================

void WebServer::setupRoutes() {
    // ========== Status ==========
    _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetStatus(request);
    });
    
    // ========== Sensors ==========
    _server.on("/api/sensors", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetSensors(request);
    });
    
    // Sensor by index
    _server.on("^\\/api\\/sensors\\/(\\d+)$", HTTP_GET, 
        [this](AsyncWebServerRequest* request) {
            uint8_t idx = request->pathArg(0).toInt();
            handleGetSensor(request, idx);
        });
    
    // Update sensor config
    AsyncCallbackJsonWebHandler* sensorUpdateHandler = new AsyncCallbackJsonWebHandler(
        "/api/sensors/update",
        [this](AsyncWebServerRequest* request, JsonVariant& json) {
            uint8_t idx = json["index"] | 255;
            if (idx < sensorManager.getSensorCount()) {
                String jsonStr;
                serializeJson(json, jsonStr);
                handleUpdateSensor(request, idx, (uint8_t*)jsonStr.c_str(), jsonStr.length());
            } else {
                sendError(request, 400, "Invalid sensor index");
            }
        }
    );
    _server.addHandler(sensorUpdateHandler);
    
    // ========== Configuration ==========
    _server.on("/api/config/wifi", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetWiFiConfig(request);
    });
    
    AsyncCallbackJsonWebHandler* wifiConfigHandler = new AsyncCallbackJsonWebHandler(
        "/api/config/wifi",
        [this](AsyncWebServerRequest* request, JsonVariant& json) {
            String jsonStr;
            serializeJson(json, jsonStr);
            handleUpdateWiFiConfig(request, (uint8_t*)jsonStr.c_str(), jsonStr.length());
        }
    );
    _server.addHandler(wifiConfigHandler);
    
    _server.on("/api/config/mqtt", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetMQTTConfig(request);
    });
    
    AsyncCallbackJsonWebHandler* mqttConfigHandler = new AsyncCallbackJsonWebHandler(
        "/api/config/mqtt",
        [this](AsyncWebServerRequest* request, JsonVariant& json) {
            String jsonStr;
            serializeJson(json, jsonStr);
            handleUpdateMQTTConfig(request, (uint8_t*)jsonStr.c_str(), jsonStr.length());
        }
    );
    _server.addHandler(mqttConfigHandler);
    
    _server.on("/api/config/system", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetSystemConfig(request);
    });
    
    AsyncCallbackJsonWebHandler* sysConfigHandler = new AsyncCallbackJsonWebHandler(
        "/api/config/system",
        [this](AsyncWebServerRequest* request, JsonVariant& json) {
            String jsonStr;
            serializeJson(json, jsonStr);
            handleUpdateSystemConfig(request, (uint8_t*)jsonStr.c_str(), jsonStr.length());
        }
    );
    _server.addHandler(sysConfigHandler);
    
    // ========== WiFi Scan ==========
    _server.on("/api/wifi/scan", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleWiFiScan(request);
    });

    // ========== OTA (GitHub Releases) ==========
    _server.on("/api/ota/info", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetOtaInfo(request);
    });

    _server.on("/api/ota/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetOtaStatus(request);
    });

    AsyncCallbackJsonWebHandler* otaUpdateHandler = new AsyncCallbackJsonWebHandler(
        "/api/ota/update",
        [this](AsyncWebServerRequest* request, JsonVariant& json) {
            String jsonStr;
            serializeJson(json, jsonStr);
            handleStartOtaUpdate(request, (uint8_t*)jsonStr.c_str(), jsonStr.length());
        }
    );
    _server.addHandler(otaUpdateHandler);
    
    // ========== Calibration ==========
    AsyncCallbackJsonWebHandler* calibrateHandler = new AsyncCallbackJsonWebHandler(
        "/api/calibrate",
        [this](AsyncWebServerRequest* request, JsonVariant& json) {
            String jsonStr;
            serializeJson(json, jsonStr);
            handleCalibrate(request, (uint8_t*)jsonStr.c_str(), jsonStr.length());
        }
    );
    _server.addHandler(calibrateHandler);
    
    // Calibrate only uncalibrated sensors
    AsyncCallbackJsonWebHandler* calibrateNewHandler = new AsyncCallbackJsonWebHandler(
        "/api/calibrate/new",
        [this](AsyncWebServerRequest* request, JsonVariant& json) {
            String jsonStr;
            serializeJson(json, jsonStr);
            handleCalibrateNew(request, (uint8_t*)jsonStr.c_str(), jsonStr.length());
        }
    );
    _server.addHandler(calibrateNewHandler);
    
    // ========== Actions ==========
    _server.on("/api/rescan", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleRescan(request);
    });
    
    _server.on("/api/reboot", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleReboot(request);
    });
    
    _server.on("/api/reset", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleFactoryReset(request);
    });
    
    // ========== History ==========
    _server.on("^\\/api\\/history\\/(\\d+)$", HTTP_GET,
        [this](AsyncWebServerRequest* request) {
            uint8_t idx = request->pathArg(0).toInt();
            handleGetHistory(request, idx);
        });
    
    // ========== Captive Portal Detection ==========
    // Android captive portal detection
    _server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/");
    });
    
    // iOS/macOS captive portal detection
    _server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/");
    });
    
    // Microsoft captive portal detection
    _server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/");
    });
    
    // Generic captive portal detection
    _server.on("/redirect", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/");
    });
    
    _server.on("/canonical.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/");
    });
    
    _server.on("/success.txt", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/");
    });
    
    // ========== CORS Headers ==========
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");
    
    // Handle OPTIONS requests for CORS
    _server.onNotFound([](AsyncWebServerRequest* request) {
        if (request->method() == HTTP_OPTIONS) {
            request->send(200);
        } else {
            // Redirect to root for captive portal
            if (wifiManager.isAPMode()) {
                request->redirect("/");
            } else {
                request->send(404, "text/plain", "Not found");
            }
        }
    });
}

void WebServer::setupStaticFiles() {
    // Serve static files from SPIFFS
    // AsyncWebServer automatically serves .gz versions if they exist
    // Version is baked into HTML at build time (?v=X.Y.Z on CSS/JS links)
    // so we can use aggressive caching - version change = new URL = cache miss
    _server.serveStatic("/", SPIFFS, "/")
        .setDefaultFile("index.html")
        .setCacheControl("max-age=86400");
}

// ============================================================================
// API Handlers
// ============================================================================

void WebServer::handleGetStatus(AsyncWebServerRequest* request) {
    JsonDocument doc;
    
    // Device info
    doc["device"]["name"] = configManager.getSystemConfig().deviceName;
    doc["device"]["firmware"] = FIRMWARE_VERSION;
    doc["device"]["uptime"] = millis() / 1000;
    doc["device"]["freeHeap"] = ESP.getFreeHeap();
    doc["device"]["chipModel"] = ESP.getChipModel();
    
    // WiFi status
    doc["wifi"]["status"] = WiFiManager::stateToString(wifiManager.getState());
    doc["wifi"]["ssid"] = wifiManager.getSSID();
    doc["wifi"]["ip"] = wifiManager.getIP().toString();
    doc["wifi"]["rssi"] = wifiManager.getRSSI();
    doc["wifi"]["signal"] = wifiManager.getSignalStrength();
    doc["wifi"]["mac"] = wifiManager.getMACAddress();
    
    if (wifiManager.isAPMode()) {
        doc["wifi"]["apIP"] = wifiManager.getAPIP().toString();
        doc["wifi"]["apClients"] = wifiManager.getAPClientCount();
    }
    
    // MQTT status
    doc["mqtt"]["enabled"] = mqttClient.isEnabled();
    doc["mqtt"]["connected"] = mqttClient.isConnected();
    doc["mqtt"]["publishCount"] = mqttClient.getPublishCount();
    
    // Sensor summary
    doc["sensors"]["count"] = sensorManager.getSensorCount();
    doc["sensors"]["alarms"] = sensorManager.getAlarmCount();
    doc["sensors"]["avgTemp"] = sensorManager.getAverageTemperature();
    doc["sensors"]["minTemp"] = sensorManager.getMinTemperature();
    doc["sensors"]["maxTemp"] = sensorManager.getMaxTemperature();
    
    char buffer[512];
    serializeJson(doc, buffer, sizeof(buffer));
    sendJson(request, 200, buffer);
}

void WebServer::handleGetSensors(AsyncWebServerRequest* request) {
    JsonDocument doc;
    JsonArray sensors = doc.to<JsonArray>();
    
    for (uint8_t i = 0; i < sensorManager.getSensorCount(); i++) {
        JsonObject obj = sensors.add<JsonObject>();
        buildSensorJson(obj, i);
    }
    
    char buffer[1024];
    serializeJson(doc, buffer, sizeof(buffer));
    sendJson(request, 200, buffer);
}

void WebServer::handleGetSensor(AsyncWebServerRequest* request, uint8_t sensorIndex) {
    if (sensorIndex >= sensorManager.getSensorCount()) {
        sendError(request, 404, "Sensor not found");
        return;
    }
    
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    buildSensorJson(obj, sensorIndex);
    
    char buffer[512];
    serializeJson(doc, buffer, sizeof(buffer));
    sendJson(request, 200, buffer);
}

void WebServer::handleUpdateSensor(AsyncWebServerRequest* request, uint8_t sensorIndex,
                                    uint8_t* data, size_t len) {
    if (sensorIndex >= sensorManager.getSensorCount()) {
        sendError(request, 404, "Sensor not found");
        return;
    }
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);
    
    if (error) {
        sendError(request, 400, "Invalid JSON");
        return;
    }
    
    const SensorData* sensorData = sensorManager.getSensorData(sensorIndex);
    SensorConfig* config = configManager.getSensorConfigByAddress(sensorData->addressStr);
    
    if (!config) {
        sendError(request, 500, "Sensor config not found");
        return;
    }
    
    // Update fields
    if (doc["name"].is<JsonVariant>()) {
        strncpy(config->name, doc["name"] | "", SENSOR_NAME_MAX_LEN - 1);
    }
    if (doc["thresholdLow"].is<JsonVariant>()) {
        config->thresholdLow = doc["thresholdLow"];
    }
    if (doc["thresholdHigh"].is<JsonVariant>()) {
        config->thresholdHigh = doc["thresholdHigh"];
    }
    if (doc["alertEnabled"].is<JsonVariant>()) {
        config->alertEnabled = doc["alertEnabled"];
    }
    if (doc["calibrationOffset"].is<JsonVariant>()) {
        config->calibrationOffset = doc["calibrationOffset"];
    }
    
    configManager.markDirty();
    if (!configManager.save()) {
        sendError(request, 500, "Failed to save configuration");
        return;
    }
    
    sendSuccess(request, "Sensor updated");
}

void WebServer::handleGetWiFiConfig(AsyncWebServerRequest* request) {
    const WiFiConfig& config = configManager.getWiFiConfig();
    
    JsonDocument doc;
    doc["ssid"] = config.ssid;
    doc["password"] = ""; // Don't expose password
    doc["dhcp"] = config.dhcp;
    doc["staticIP"] = config.staticIP;
    doc["gateway"] = config.gateway;
    doc["subnet"] = config.subnet;
    doc["dns"] = config.dns;
    
    char buffer[256];
    serializeJson(doc, buffer, sizeof(buffer));
    sendJson(request, 200, buffer);
}

void WebServer::handleUpdateWiFiConfig(AsyncWebServerRequest* request,
                                        uint8_t* data, size_t len) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);
    
    if (error) {
        sendError(request, 400, "Invalid JSON");
        return;
    }
    
    WiFiConfig& config = configManager.getWiFiConfig();
    
    if (doc["ssid"].is<JsonVariant>()) {
        strncpy(config.ssid, doc["ssid"] | "", 32);
    }
    if (doc["password"].is<JsonVariant>() && strlen(doc["password"] | "") > 0) {
        strncpy(config.password, doc["password"] | "", 64);
    }
    if (doc["dhcp"].is<JsonVariant>()) {
        config.dhcp = doc["dhcp"];
    }
    if (doc["staticIP"].is<JsonVariant>()) {
        strncpy(config.staticIP, doc["staticIP"] | "", 15);
    }
    if (doc["gateway"].is<JsonVariant>()) {
        strncpy(config.gateway, doc["gateway"] | "", 15);
    }
    if (doc["subnet"].is<JsonVariant>()) {
        strncpy(config.subnet, doc["subnet"] | "", 15);
    }
    if (doc["dns"].is<JsonVariant>()) {
        strncpy(config.dns, doc["dns"] | "", 15);
    }
    
    if (!configManager.save()) {
        sendError(request, 500, "Failed to save configuration");
        return;
    }
    
    // Send response before triggering reconnect
    sendSuccess(request, "WiFi configuration updated. Reconnecting...");
    
    // Request reconnection (handled safely in main loop)
    wifiManager.reconnect();
}

void WebServer::handleGetMQTTConfig(AsyncWebServerRequest* request) {
    const MQTTConfig& config = configManager.getMQTTConfig();
    
    JsonDocument doc;
    doc["server"] = config.server;
    doc["port"] = config.port;
    doc["username"] = config.username;
    doc["password"] = ""; // Don't expose password
    doc["topicPrefix"] = config.topicPrefix;
    doc["enabled"] = config.enabled;
    doc["publishOnChange"] = config.publishOnChange;
    doc["publishThreshold"] = config.publishThreshold;
    doc["publishInterval"] = config.publishInterval;
    
    char buffer[256];
    serializeJson(doc, buffer, sizeof(buffer));
    sendJson(request, 200, buffer);
}

void WebServer::handleUpdateMQTTConfig(AsyncWebServerRequest* request,
                                        uint8_t* data, size_t len) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);
    
    if (error) {
        sendError(request, 400, "Invalid JSON");
        return;
    }
    
    MQTTConfig& config = configManager.getMQTTConfig();
    
    if (doc["server"].is<JsonVariant>()) {
        strncpy(config.server, doc["server"] | "", 64);
    }
    if (doc["port"].is<JsonVariant>()) {
        config.port = doc["port"];
    }
    if (doc["username"].is<JsonVariant>()) {
        strncpy(config.username, doc["username"] | "", 32);
    }
    if (doc["password"].is<JsonVariant>() && strlen(doc["password"] | "") > 0) {
        strncpy(config.password, doc["password"] | "", 64);
    }
    if (doc["topicPrefix"].is<JsonVariant>()) {
        strncpy(config.topicPrefix, doc["topicPrefix"] | "", 64);
    }
    if (doc["enabled"].is<JsonVariant>()) {
        config.enabled = doc["enabled"];
    }
    if (doc["publishOnChange"].is<JsonVariant>()) {
        config.publishOnChange = doc["publishOnChange"];
    }
    if (doc["publishThreshold"].is<JsonVariant>()) {
        config.publishThreshold = doc["publishThreshold"];
    }
    if (doc["publishInterval"].is<JsonVariant>()) {
        config.publishInterval = doc["publishInterval"];
    }
    
    if (!configManager.save()) {
        sendError(request, 500, "Failed to save configuration");
        return;
    }
    
    // Send response before triggering reconnect
    sendSuccess(request, "MQTT configuration updated");
    
    // Request reconnection (handled safely in main loop)
    mqttClient.reconnect();
}

void WebServer::handleGetSystemConfig(AsyncWebServerRequest* request) {
    const SystemConfig& config = configManager.getSystemConfig();
    
    JsonDocument doc;
    doc["deviceName"] = config.deviceName;
    doc["readInterval"] = config.readInterval;
    doc["celsiusUnits"] = config.celsiusUnits;
    doc["utcOffset"] = config.utcOffset;
    doc["otaEnabled"] = config.otaEnabled;
    doc["pinnedSensorAddress"] = config.pinnedSensorAddress;
    
    char buffer[256];
    serializeJson(doc, buffer, sizeof(buffer));
    sendJson(request, 200, buffer);
}

void WebServer::handleUpdateSystemConfig(AsyncWebServerRequest* request,
                                          uint8_t* data, size_t len) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);
    
    if (error) {
        sendError(request, 400, "Invalid JSON");
        return;
    }
    
    SystemConfig& config = configManager.getSystemConfig();
    
    if (doc["deviceName"].is<JsonVariant>()) {
        strncpy(config.deviceName, doc["deviceName"] | "", 32);
    }
    if (doc["readInterval"].is<JsonVariant>()) {
        config.readInterval = doc["readInterval"];
    }
    if (doc["celsiusUnits"].is<JsonVariant>()) {
        config.celsiusUnits = doc["celsiusUnits"];
    }
    if (doc["utcOffset"].is<JsonVariant>()) {
        config.utcOffset = doc["utcOffset"];
    }
    if (doc["otaEnabled"].is<JsonVariant>()) {
        config.otaEnabled = doc["otaEnabled"];
    }
    if (doc["pinnedSensorAddress"].is<JsonVariant>()) {
        strlcpy(config.pinnedSensorAddress, doc["pinnedSensorAddress"] | "", sizeof(config.pinnedSensorAddress));
    }
    
    if (!configManager.save()) {
        sendError(request, 500, "Failed to save configuration");
        return;
    }
    
    sendSuccess(request, "System configuration updated");
}

void WebServer::handleWiFiScan(AsyncWebServerRequest* request) {
    DEBUG_PRINTLN(F("[WebServer] WiFi scan requested"));
    
    int16_t count = wifiManager.scanNetworks();
    
    // Scan in progress
    if (count == -1) {
        DEBUG_PRINTLN(F("[WebServer] Scan in progress, returning status"));
        request->send(202, "application/json", "{\"status\":\"scanning\",\"message\":\"WiFi scan in progress, please retry in 2-3 seconds\"}");
        return;
    }
    
    // Scan failed to start
    if (count == -2) {
        DEBUG_PRINTLN(F("[WebServer] Scan failed"));
        sendError(request, 500, "WiFi scan failed to start");
        return;
    }
    
    // Return results
    JsonDocument doc;
    JsonArray networks = doc.to<JsonArray>();
    
    for (int16_t i = 0; i < count && i < 20; i++) {  // Limit to 20 networks
        String ssid;
        int32_t rssi;
        bool encrypted;
        
        if (wifiManager.getScannedNetwork(i, ssid, rssi, encrypted)) {
            // Skip empty SSIDs
            if (ssid.length() == 0) continue;
            
            JsonObject net = networks.add<JsonObject>();
            net["ssid"] = ssid;
            net["rssi"] = rssi;
            net["encrypted"] = encrypted;
            
            // Signal strength as percentage
            int strength = 0;
            if (rssi >= -50) strength = 100;
            else if (rssi <= -100) strength = 0;
            else strength = 2 * (rssi + 100);
            net["signal"] = strength;
        }
    }
    
    DEBUG_PRINTF("[WebServer] Returning %d networks\n", networks.size());
    
    String response;
    serializeJson(doc, response);
    sendJson(request, 200, response.c_str());
}

void WebServer::handleCalibrate(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);
    
    if (error || !doc["referenceTemp"].is<JsonVariant>()) {
        sendError(request, 400, "Missing referenceTemp");
        return;
    }
    
    float refTemp = doc["referenceTemp"];
    sensorManager.calibrateAll(refTemp);
    
    sendSuccess(request, "All sensors calibrated");
}

void WebServer::handleCalibrateNew(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);
    
    if (error || !doc["referenceTemp"].is<JsonVariant>()) {
        sendError(request, 400, "Missing referenceTemp");
        return;
    }
    
    float refTemp = doc["referenceTemp"];
    uint8_t count = sensorManager.calibrateUncalibrated(refTemp);
    
    char message[64];
    snprintf(message, sizeof(message), "Calibrated %d new sensor(s)", count);
    sendSuccess(request, message);
}

void WebServer::handleCalibrateSensor(AsyncWebServerRequest* request, uint8_t sensorIndex,
                                       uint8_t* data, size_t len) {
    if (sensorIndex >= sensorManager.getSensorCount()) {
        sendError(request, 404, "Sensor not found");
        return;
    }
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);
    
    if (error || !doc["referenceTemp"].is<JsonVariant>()) {
        sendError(request, 400, "Missing referenceTemp");
        return;
    }
    
    float refTemp = doc["referenceTemp"];
    sensorManager.calibrateSensor(sensorIndex, refTemp);
    
    if (!configManager.save()) {
        sendError(request, 500, "Failed to save configuration");
        return;
    }
    
    sendSuccess(request, "Sensor calibrated");
}

void WebServer::handleRescan(AsyncWebServerRequest* request) {
    sensorManager.requestRescan();
    sendSuccess(request, "Sensor rescan initiated");
}

void WebServer::handleReboot(AsyncWebServerRequest* request) {
    sendSuccess(request, "Rebooting...");
    delay(1000);
    ESP.restart();
}

void WebServer::handleFactoryReset(AsyncWebServerRequest* request) {
    configManager.resetToDefaults();
    
    if (!configManager.save()) {
        sendError(request, 500, "Failed to save configuration");
        return;
    }
    
    sendSuccess(request, "Factory reset complete. Rebooting...");
    delay(1000);
    ESP.restart();
}

void WebServer::handleGetHistory(AsyncWebServerRequest* request, uint8_t sensorIndex) {
    if (sensorIndex >= sensorManager.getSensorCount()) {
        sendError(request, 404, "Sensor not found");
        return;
    }
    
    const SensorData* data = sensorManager.getSensorData(sensorIndex);
    
    JsonDocument doc;
    JsonArray history = doc.to<JsonArray>();
    
    // Output history from oldest to newest (convert from int16_t back to float)
    for (uint16_t i = 0; i < data->historyCount; i++) {
        uint16_t idx = (data->historyIndex - data->historyCount + i + TEMP_HISTORY_SIZE) 
                       % TEMP_HISTORY_SIZE;
        if (data->history[idx] != TEMP_HISTORY_INVALID) {
            history.add(data->history[idx] / 100.0f);
        }
    }
    
    char buffer[512];
    serializeJson(doc, buffer, sizeof(buffer));
    sendJson(request, 200, buffer);
}

// ============================================================================
// OTA Handlers
// ============================================================================

static const char* otaStateToString(OTAState state) {
    switch (state) {
        case OTAState::IDLE:             return "idle";
        case OTAState::CHECKING:         return "checking";
        case OTAState::READY:            return "ready";
        case OTAState::UPDATING_FIRMWARE:return "updating_firmware";
        case OTAState::UPDATING_SPIFFS:  return "updating_spiffs";
        case OTAState::REBOOTING:        return "rebooting";
        case OTAState::ERROR:            return "error";
        default:                         return "unknown";
    }
}

void WebServer::handleGetOtaInfo(AsyncWebServerRequest* request) {
    JsonDocument doc;
    doc["current"] = FIRMWARE_VERSION;
    doc["github"]["owner"] = GITHUB_OWNER;
    doc["github"]["repo"] = GITHUB_REPO;
    
    // Add partition and memory info
    OTAPartitionInfo partInfo = OTAManager::getPartitionInfo();
    doc["partition"]["firmware"] = partInfo.firmwarePartitionSize;
    doc["partition"]["spiffs"] = partInfo.spiffsPartitionSize;
    doc["memory"]["freeHeap"] = partInfo.freeHeap;
    doc["memory"]["minFreeHeap"] = partInfo.minFreeHeap;

    // If OTA is disabled, still return current version so the UI can render.
    if (!configManager.getSystemConfig().otaEnabled) {
        doc["updateAvailable"] = false;
        doc["configPreserved"] = true;
        doc["error"] = "OTA disabled";

        String out;
        serializeJson(doc, out);
        sendJson(request, 200, out.c_str());
        return;
    }

    // Check for force parameter (e.g., ?force=1 from check button)
    // Only force-check if explicitly requested, don't auto-check on every page load
    String err;
    if (request->hasParam("force") && request->getParam("force")->value() == "1") {
        otaManager.ensureReleaseInfoFresh(true, err);
    }

    OTAProgress p = otaManager.getProgress();
    doc["state"] = otaStateToString(p.state);
    doc["statusMessage"] = p.message;

    OTAReleaseInfo info;
    otaManager.getReleaseInfoCopy(info);

    doc["latest"]["tag"] = info.tag;
    doc["latest"]["name"] = info.name;
    doc["latest"]["notes"] = info.body;
    doc["latest"]["readme"] = info.readme;
    doc["latest"]["assets"]["firmware"] = info.firmwareUrl.length() > 0;
    doc["latest"]["assets"]["spiffs"] = info.spiffsUrl.length() > 0;

    doc["configPreserved"] = true; // config stored in NVS

    bool updateAvailable = (info.tag.length() > 0) && (String(FIRMWARE_VERSION) != info.tag);
    doc["updateAvailable"] = updateAvailable;

    if (p.error[0] != '\0') {
        doc["error"] = p.error;
    } else if (err.length() > 0) {
        doc["error"] = err;
    }

    String out;
    serializeJson(doc, out);
    sendJson(request, 200, out.c_str());
}

void WebServer::handleGetOtaStatus(AsyncWebServerRequest* request) {
    OTAProgress p = otaManager.getProgress();

    JsonDocument doc;
    doc["state"] = otaStateToString(p.state);
    doc["progress"] = p.progressPercent;
    doc["message"] = p.message;
    doc["error"] = p.error;

    String out;
    serializeJson(doc, out);
    sendJson(request, 200, out.c_str());
}

void WebServer::handleStartOtaUpdate(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    if (!configManager.getSystemConfig().otaEnabled) {
        sendError(request, 403, "OTA disabled");
        return;
    }

    if (wifiManager.getState() != WiFiState::CONNECTED && wifiManager.getState() != WiFiState::AP_STA_MODE) {
        sendError(request, 400, "WiFi not connected (need internet for GitHub OTA)");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);
    if (error) {
        sendError(request, 400, "Invalid JSON");
        return;
    }

    String targetStr = doc["target"] | "both";
    targetStr.toLowerCase();

    OTATarget target = OTATarget::BOTH;
    if (targetStr == "firmware") target = OTATarget::FIRMWARE;
    else if (targetStr == "spiffs") target = OTATarget::SPIFFS;

    String err;
    if (!otaManager.startUpdate(target, err)) {
        sendError(request, 400, err.c_str());
        return;
    }

    sendSuccess(request, "OTA update started");
}

// ============================================================================
// WebSocket Handlers
// ============================================================================

void WebServer::onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                          AwsEventType type, void* arg, uint8_t* data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            // Send initial data
            sendSensorUpdate();
            // Check if update is available and notify client
            sendUpdateNotification(client);
            break;
            
        case WS_EVT_DISCONNECT:
            break;
            
        case WS_EVT_DATA:
            handleWsMessage(client, data, len);
            break;
            
        case WS_EVT_ERROR:
            break;
            
        default:
            break;
    }
}

void WebServer::handleWsMessage(AsyncWebSocketClient* client, uint8_t* data, size_t len) {
    // Parse message
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);
    
    if (error) {
        return;
    }
    
    const char* cmd = doc["cmd"] | "";
    
    if (strcmp(cmd, "refresh") == 0) {
        sendSensorUpdate();
    }
}

// ============================================================================
// Utility Methods
// ============================================================================

void WebServer::sendJson(AsyncWebServerRequest* request, int code, const char* json) {
    request->send(code, "application/json", json);
}

void WebServer::sendError(AsyncWebServerRequest* request, int code, const char* message) {
    JsonDocument doc;
    doc["error"] = true;
    doc["message"] = message;
    
    char buffer[128];
    serializeJson(doc, buffer, sizeof(buffer));
    sendJson(request, code, buffer);
}

void WebServer::sendSuccess(AsyncWebServerRequest* request, const char* message) {
    JsonDocument doc;
    doc["success"] = true;
    if (message) {
        doc["message"] = message;
    }
    
    char buffer[128];
    serializeJson(doc, buffer, sizeof(buffer));
    sendJson(request, 200, buffer);
}

void WebServer::buildSensorJson(JsonObject& obj, uint8_t sensorIndex) {
    const SensorData* data = sensorManager.getSensorData(sensorIndex);
    if (!data) {
        return;
    }
    
    const SensorConfig* config = configManager.getSensorConfigByAddress(data->addressStr);
    
    obj["index"] = sensorIndex;
    obj["address"] = data->addressStr;
    obj["connected"] = data->connected;
    obj["temperature"] = round(data->temperature * 100) / 100.0;
    obj["rawTemperature"] = round(data->rawTemperature * 100) / 100.0;
    obj["alarm"] = alarmStateToString(data->alarmState);
    
    if (config) {
        obj["name"] = config->name;
        obj["calibrationOffset"] = config->calibrationOffset;
        obj["thresholdLow"] = config->thresholdLow;
        obj["thresholdHigh"] = config->thresholdHigh;
        obj["alertEnabled"] = config->alertEnabled;
    }
}
