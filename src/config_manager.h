/*
 * ESP32 Temperature Monitoring System
 * Configuration Manager Header
 * 
 * Handles persistent storage of all configuration data including:
 * - WiFi credentials
 * - MQTT settings
 * - Sensor names and calibration offsets
 * - Threshold settings
 */

#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "config.h"

// ============================================================================
// Data Structures
// ============================================================================

/**
 * Sensor configuration stored in persistent storage
 */
struct SensorConfig {
    char address[SENSOR_ADDR_STR_LEN];  // Sensor address as hex string
    char name[SENSOR_NAME_MAX_LEN];     // User-defined name
    float calibrationOffset;             // Temperature offset for calibration
    float thresholdLow;                  // Low temperature threshold
    float thresholdHigh;                 // High temperature threshold
    bool alertEnabled;                   // Whether alerts are enabled for this sensor
    bool isConfigured;                   // Whether this sensor has been configured
    
    SensorConfig() : 
        calibrationOffset(0.0f),
        thresholdLow(DEFAULT_THRESHOLD_LOW),
        thresholdHigh(DEFAULT_THRESHOLD_HIGH),
        alertEnabled(true),
        isConfigured(false) {
        address[0] = '\0';
        name[0] = '\0';
    }
};

/**
 * WiFi configuration
 */
struct WiFiConfig {
    char ssid[33];
    char password[65];
    bool dhcp;
    char staticIP[16];
    char gateway[16];
    char subnet[16];
    char dns[16];
    
    WiFiConfig() : dhcp(true) {
        ssid[0] = '\0';
        password[0] = '\0';
        strcpy(staticIP, "192.168.1.100");
        strcpy(gateway, "192.168.1.1");
        strcpy(subnet, "255.255.255.0");
        strcpy(dns, "8.8.8.8");
    }
};

/**
 * MQTT configuration
 */
struct MQTTConfig {
    char server[65];
    uint16_t port;
    char username[33];
    char password[65];
    char topicPrefix[65];
    bool enabled;
    bool publishOnChange;      // Publish only when temperature changes
    float publishThreshold;    // Minimum change to trigger publish
    uint32_t publishInterval;  // Publish interval in seconds
    
    MQTTConfig() : 
        port(MQTT_DEFAULT_PORT),
        enabled(false),
        publishOnChange(true),
        publishThreshold(0.5f),
        publishInterval(10) {
        server[0] = '\0';
        username[0] = '\0';
        password[0] = '\0';
        strcpy(topicPrefix, MQTT_TOPIC_BASE);
    }
};

/**
 * System configuration
 */
struct SystemConfig {
    char deviceName[33];
    uint32_t readInterval;      // Temperature read interval in seconds
    bool celsiusUnits;          // true = Celsius, false = Fahrenheit
    int8_t utcOffset;           // UTC offset in hours
    bool otaEnabled;            // OTA updates enabled
    
    SystemConfig() : 
        readInterval(2),
        celsiusUnits(true),
        utcOffset(0),
        otaEnabled(true) {
        strcpy(deviceName, "TempMonitor");
    }
};

// ============================================================================
// ConfigManager Class
// ============================================================================

class ConfigManager {
public:
    /**
     * Initialize the configuration manager
     * @return true if initialization successful
     */
    bool begin();
    
    /**
     * Load configuration from SPIFFS
     * @return true if loaded successfully
     */
    bool load();
    
    /**
     * Save configuration to SPIFFS
     * @return true if saved successfully
     */
    bool save();
    
    /**
     * Reset all configuration to defaults
     */
    void resetToDefaults();
    
    /**
     * Get WiFi configuration
     */
    WiFiConfig& getWiFiConfig() { return _wifiConfig; }
    const WiFiConfig& getWiFiConfig() const { return _wifiConfig; }
    
    /**
     * Get MQTT configuration
     */
    MQTTConfig& getMQTTConfig() { return _mqttConfig; }
    const MQTTConfig& getMQTTConfig() const { return _mqttConfig; }
    
    /**
     * Get system configuration
     */
    SystemConfig& getSystemConfig() { return _systemConfig; }
    const SystemConfig& getSystemConfig() const { return _systemConfig; }
    
    /**
     * Get sensor configuration by index
     * @param index Sensor index (0 to MAX_SENSORS-1)
     * @return Pointer to sensor config or nullptr if index invalid
     */
    SensorConfig* getSensorConfig(uint8_t index);
    const SensorConfig* getSensorConfig(uint8_t index) const;
    
    /**
     * Get sensor configuration by address
     * @param address Sensor address as hex string
     * @return Pointer to sensor config or nullptr if not found
     */
    SensorConfig* getSensorConfigByAddress(const char* address);
    const SensorConfig* getSensorConfigByAddress(const char* address) const;
    
    /**
     * Find or create sensor configuration for an address
     * @param address Sensor address as hex string
     * @return Pointer to sensor config or nullptr if no space available
     */
    SensorConfig* findOrCreateSensorConfig(const char* address);
    
    /**
     * Get the number of configured sensors
     */
    uint8_t getConfiguredSensorCount() const;
    
    /**
     * Mark configuration as dirty (needs saving)
     */
    void markDirty() { _isDirty = true; }
    
    /**
     * Check if configuration needs saving
     */
    bool isDirty() const { return _isDirty; }
    
    /**
     * Clear dirty flag
     */
    void clearDirty() { _isDirty = false; }
    
    /**
     * Export configuration as JSON
     * @param doc JSON document to populate
     */
    void toJson(JsonDocument& doc) const;
    
    /**
     * Import configuration from JSON
     * @param doc JSON document containing configuration
     * @return true if import successful
     */
    bool fromJson(const JsonDocument& doc);
    
private:
    struct PersistentConfigBlob {
        uint32_t magic;
        uint16_t version;
        uint16_t reserved;
        WiFiConfig wifi;
        MQTTConfig mqtt;
        SystemConfig system;
        SensorConfig sensors[MAX_SENSORS];
    };

    WiFiConfig _wifiConfig;
    MQTTConfig _mqttConfig;
    SystemConfig _systemConfig;
    SensorConfig _sensorConfigs[MAX_SENSORS];
    bool _isDirty;
    bool _initialized;

    Preferences _prefs;
    bool _prefsOpen = false;

    bool loadFromNVS();
    bool saveToNVS();
    bool loadLegacyFromSPIFFS();
    
    /**
     * Serialize sensor config to JSON
     */
    void sensorConfigToJson(const SensorConfig& config, JsonObject& obj) const;
    
    /**
     * Deserialize sensor config from JSON
     */
    void sensorConfigFromJson(SensorConfig& config, JsonObjectConst obj);
};

// Global configuration manager instance
extern ConfigManager configManager;

#endif // CONFIG_MANAGER_H
