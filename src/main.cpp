/*
 * ESP32 Temperature Monitoring System
 * Main Application Entry Point
 * 
 * Features:
 * - DS18B20 temperature sensor support (5-10 sensors)
 * - WiFi connectivity with AP fallback mode
 * - Web dashboard with real-time updates
 * - MQTT publishing with Home Assistant auto-discovery
 * - Sensor calibration and naming
 * - Temperature threshold alerts
 * - OTA firmware updates
 * 
 * Hardware:
 * - TTGO T-Display (ESP32 with ST7789 TFT)
 * - DS18B20 temperature sensors (1-10) on GPIO27
 * - 4.7kΩ pull-up resistor on data line
 * 
 * Author: Temperature Monitor Project
 * License: MIT
 */

#include <Arduino.h>
#include <ArduinoOTA.h>

#include "config.h"
#include "config_manager.h"
#include "sensor_manager.h"
#include "wifi_manager.h"
#include "mqtt_client.h"
#include "web_server.h"
#include "display_manager.h"

// ============================================================================
// Global State
// ============================================================================

static uint32_t lastStatusPrint = 0;
static uint32_t lastLedToggle = 0;
static bool ledState = false;

#ifdef USE_DISPLAY
static uint32_t lastButton1State = HIGH;
static uint32_t lastButton2State = HIGH;
static uint32_t button1PressTime = 0;
static const uint32_t LONG_PRESS_TIME = 800;  // 800ms for long press
#endif

// ============================================================================
// Forward Declarations
// ============================================================================

void setupOTA();

// ============================================================================
// Callback Handlers
// ============================================================================

/**
 * Handle alarm state changes
 */
void onAlarmStateChange(uint8_t sensorIndex, AlarmState oldState, 
                        AlarmState newState, float temperature) {
    const SensorData* data = sensorManager.getSensorData(sensorIndex);
    const SensorConfig* config = data ? 
        configManager.getSensorConfigByAddress(data->addressStr) : nullptr;
    
    const char* sensorName = config ? config->name : "Unknown";
    
    Serial.printf("[ALARM] Sensor '%s': %s -> %s (%.1f°C)\n",
        sensorName,
        alarmStateToString(oldState),
        alarmStateToString(newState),
        temperature
    );
    
    // Publish alarm via MQTT
    if (mqttClient.isConnected()) {
        mqttClient.publishAlarm(sensorIndex, newState, temperature);
    }
    
    // Send WebSocket notification
    char message[64];
    if (newState == AlarmState::ABOVE_HIGH) {
        snprintf(message, sizeof(message), "⚠️ %s: High temperature (%.1f°C)", 
            sensorName, temperature);
        webServer.sendNotification("warning", message);
    } else if (newState == AlarmState::BELOW_LOW) {
        snprintf(message, sizeof(message), "❄️ %s: Low temperature (%.1f°C)", 
            sensorName, temperature);
        webServer.sendNotification("warning", message);
    } else if (newState == AlarmState::NORMAL && 
               (oldState == AlarmState::ABOVE_HIGH || oldState == AlarmState::BELOW_LOW)) {
        snprintf(message, sizeof(message), "✅ %s: Temperature normal (%.1f°C)", 
            sensorName, temperature);
        webServer.sendNotification("success", message);
    }
}

/**
 * Handle sensor connection changes
 */
void onSensorConnectionChange(uint8_t sensorIndex, bool connected) {
    const SensorData* data = sensorManager.getSensorData(sensorIndex);
    const SensorConfig* config = data ? 
        configManager.getSensorConfigByAddress(data->addressStr) : nullptr;
    
    const char* sensorName = config ? config->name : "Unknown";
    
    Serial.printf("[SENSOR] %s: %s\n", sensorName, connected ? "Connected" : "Disconnected");
    
    // Send WebSocket notification
    char message[64];
    if (connected) {
        snprintf(message, sizeof(message), "🔌 %s: Sensor connected", sensorName);
        webServer.sendNotification("info", message);
    } else {
        snprintf(message, sizeof(message), "⚠️ %s: Sensor disconnected", sensorName);
        webServer.sendNotification("error", message);
    }
}

/**
 * Handle WiFi state changes
 */
void onWiFiStateChange(WiFiState oldState, WiFiState newState) {
    if (newState == WiFiState::CONNECTED) {
        Serial.println(F("[MAIN] WiFi connected, starting services..."));
        
        // Initialize MQTT
        mqttClient.begin();
        
        // Initialize OTA
        if (configManager.getSystemConfig().otaEnabled) {
            setupOTA();
        }
    }
}

// ============================================================================
// OTA Setup
// ============================================================================

void setupOTA() {
    ArduinoOTA.setHostname(configManager.getSystemConfig().deviceName);
    
    ArduinoOTA.onStart([]() {
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
        Serial.println("[OTA] Start updating " + type);
        
        // Stop services during update
        mqttClient.disconnect();
    });
    
    ArduinoOTA.onEnd([]() {
        Serial.println(F("\n[OTA] Update complete"));
    });
    
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("[OTA] Progress: %u%%\r", (progress / (total / 100)));
    });
    
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("[OTA] Error[%u]: ", error);
        switch (error) {
            case OTA_AUTH_ERROR:    Serial.println(F("Auth Failed")); break;
            case OTA_BEGIN_ERROR:   Serial.println(F("Begin Failed")); break;
            case OTA_CONNECT_ERROR: Serial.println(F("Connect Failed")); break;
            case OTA_RECEIVE_ERROR: Serial.println(F("Receive Failed")); break;
            case OTA_END_ERROR:     Serial.println(F("End Failed")); break;
        }
    });
    
    ArduinoOTA.begin();
    Serial.println(F("[OTA] OTA ready"));
}

// ============================================================================
// Status LED
// ============================================================================

void updateStatusLed() {
    uint32_t now = millis();
    
    if (now - lastLedToggle < LED_BLINK_INTERVAL) {
        return;
    }
    
    lastLedToggle = now;
    
    // LED indicates system state:
    // - Fast blink: No WiFi, AP mode
    // - Slow blink: Connected, normal operation
    // - Solid on: Alarm active
    
    if (sensorManager.hasAlarm()) {
        // Solid on during alarm
        digitalWrite(LED_PIN, HIGH);
        return;
    }
    
    // Toggle LED
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState ? HIGH : LOW);
}

// ============================================================================
// Debug Status Print
// ============================================================================

void printStatus() {
    uint32_t now = millis();
    
    if (now - lastStatusPrint < 30000) {  // Every 30 seconds
        return;
    }
    
    lastStatusPrint = now;
    
    Serial.println(F("\n========== System Status =========="));
    Serial.printf("Uptime: %lu seconds\n", now / 1000);
    Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());
    
    // WiFi status
    Serial.printf("WiFi: %s", WiFiManager::stateToString(wifiManager.getState()));
    if (wifiManager.isConnected()) {
        Serial.printf(" (%s, %d dBm)\n", wifiManager.getIP().toString().c_str(), wifiManager.getRSSI());
    } else {
        Serial.println();
    }
    
    // MQTT status
    Serial.printf("MQTT: %s (published: %lu)\n", 
        mqttClient.isConnected() ? "Connected" : "Disconnected",
        mqttClient.getPublishCount()
    );
    
    // Sensor status
    Serial.printf("Sensors: %d connected, %d alarms\n",
        sensorManager.getSensorCount(),
        sensorManager.getAlarmCount()
    );
    
    // Print each sensor
    for (uint8_t i = 0; i < sensorManager.getSensorCount(); i++) {
        const SensorData* data = sensorManager.getSensorData(i);
        const SensorConfig* config = configManager.getSensorConfigByAddress(data->addressStr);
        
        if (data && config) {
            Serial.printf("  [%d] %s: %.1f°C (%s)\n",
                i,
                config->name,
                data->temperature,
                alarmStateToString(data->alarmState)
            );
        }
    }
    
    Serial.println(F("====================================\n"));
}

// ============================================================================
// Setup
// ============================================================================

void setup() {
    // Initialize serial
    Serial.begin(115200);
    delay(1000);
    
    Serial.println(F("\n"));
    Serial.println(F("╔════════════════════════════════════════╗"));
    Serial.println(F("║   ESP32 Temperature Monitoring System  ║"));
    Serial.printf("║   Firmware Version: %-19s║\n", FIRMWARE_VERSION);
    Serial.println(F("╚════════════════════════════════════════╝"));
    Serial.println();
    
    // Initialize LED pin
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    
#ifdef USE_DISPLAY
    // Initialize buttons
    pinMode(BUTTON_1_PIN, INPUT_PULLUP);
    pinMode(BUTTON_2_PIN, INPUT_PULLUP);
    
    // Initialize display
    Serial.println(F("[MAIN] Initializing display..."));
    displayManager.begin();
#endif
    
    // Initialize configuration manager (loads from SPIFFS)
    Serial.println(F("[MAIN] Initializing configuration..."));
    if (!configManager.begin()) {
        Serial.println(F("[MAIN] ERROR: Failed to initialize configuration!"));
    }
    
    // Initialize sensor manager
    Serial.println(F("[MAIN] Initializing sensors..."));
    sensorManager.setAlarmCallback(onAlarmStateChange);
    sensorManager.setConnectionCallback(onSensorConnectionChange);
    
    if (!sensorManager.begin()) {
        Serial.println(F("[MAIN] WARNING: No sensors found at startup"));
    }
    
    // Initialize WiFi manager
    Serial.println(F("[MAIN] Initializing WiFi..."));
    wifiManager.setStateCallback(onWiFiStateChange);
    wifiManager.begin();
    
    // Initialize web server (works in both AP and STA mode)
    Serial.println(F("[MAIN] Initializing web server..."));
    webServer.begin();
    
#ifdef USE_DISPLAY
    // Set display manager references
    displayManager.setSensorManager(&sensorManager);
    displayManager.setWiFiManager(&wifiManager);
    displayManager.setMQTTClient(&mqttClient);
#endif
    
    // Print initial status
    Serial.println(F("\n[MAIN] Initialization complete!"));
    Serial.println(F("[MAIN] Access the dashboard at:"));
    
    if (wifiManager.isConnected()) {
        Serial.printf("[MAIN]   http://%s/\n", wifiManager.getIP().toString().c_str());
    }
    
    if (wifiManager.isAPMode()) {
        Serial.printf("[MAIN]   http://%s/ (AP Mode)\n", wifiManager.getAPIP().toString().c_str());
        Serial.printf("[MAIN]   WiFi: %s / %s\n", AP_SSID, AP_PASSWORD);
    }
    
    Serial.println();
}

// ============================================================================
// Main Loop
// ============================================================================

void loop() {
    // Update WiFi manager (handles reconnection)
    wifiManager.update();
    
    // Update sensor manager (handles reading and alarms)
    sensorManager.update();
    
    // Update MQTT client (handles publishing)
    if (wifiManager.isConnected()) {
        mqttClient.update();
    }
    
    // Update web server (handles WebSocket updates)
    webServer.update();
    
    // Handle OTA updates
    if (wifiManager.isConnected() && configManager.getSystemConfig().otaEnabled) {
        ArduinoOTA.handle();
    }
    
    // Save configuration if needed (debounced)
    static uint32_t lastConfigCheck = 0;
    if (configManager.isDirty() && millis() - lastConfigCheck > CONFIG_SAVE_DEBOUNCE) {
        configManager.save();
        lastConfigCheck = millis();
    }
    
    // Update status LED
    updateStatusLed();
    
#ifdef USE_DISPLAY
    // Handle button presses
    bool button1 = digitalRead(BUTTON_1_PIN);
    bool button2 = digitalRead(BUTTON_2_PIN);
    
    // BTN1: short press = next sensor, long press = toggle auto-rotate
    if (button1 == LOW && lastButton1State == HIGH) {
        // Button just pressed - record time
        button1PressTime = millis();
    } else if (button1 == HIGH && lastButton1State == LOW) {
        // Button released - check duration
        uint32_t pressDuration = millis() - button1PressTime;
        if (pressDuration >= LONG_PRESS_TIME) {
            displayManager.handleButton1LongPress();
        } else {
            displayManager.handleButton1();
        }
    }
    
    // BTN2: simple press = next page
    if (button2 == LOW && lastButton2State == HIGH) {
        displayManager.handleButton2();
    }
    
    lastButton1State = button1;
    lastButton2State = button2;
    
    // Update display
    displayManager.update();
#endif
    
    // Print debug status
    printStatus();
    
    // Small delay to prevent watchdog issues
    yield();
}
