/*
 * ESP32 Temperature Monitoring System
 * MQTT Client Header
 * 
 * Handles MQTT connectivity including:
 * - Connection management with auto-reconnect
 * - Temperature publishing
 * - Alarm notifications
 * - Home Assistant auto-discovery
 */

#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <Arduino.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include "config.h"
#include "config_manager.h"
#include "sensor_manager.h"

// ============================================================================
// MQTT Topic Suffixes
// ============================================================================

// Topic structure: {prefix}/{device_name}/{suffix}
// Example: tempmonitor/TempMonitor/sensor/1/temperature

constexpr char TOPIC_STATUS[] = "status";
constexpr char TOPIC_SENSOR[] = "sensor";
constexpr char TOPIC_TEMPERATURE[] = "temperature";
constexpr char TOPIC_ALARM[] = "alarm";
constexpr char TOPIC_COMMAND[] = "cmd";
constexpr char TOPIC_CONFIG[] = "config";

// Home Assistant discovery prefix
constexpr char HA_DISCOVERY_PREFIX[] = "homeassistant";

// ============================================================================
// MQTTClient Class
// ============================================================================

class MQTTClient {
public:
    /**
     * Constructor
     */
    MQTTClient();
    
    /**
     * Initialize MQTT client
     */
    void begin();
    
    /**
     * Update MQTT client (call in main loop)
     * Handles connection, reconnection, and periodic publishing
     */
    void update();
    
    /**
     * Check if connected to MQTT broker
     */
    bool isConnected() { return _client.connected(); }
    
    /**
     * Check if MQTT is enabled
     */
    bool isEnabled() const;
    
    /**
     * Force reconnection
     */
    void reconnect();
    
    /**
     * Disconnect from MQTT broker
     */
    void disconnect();
    
    /**
     * Set OTA mode - disables all MQTT operations
     */
    void setOtaMode(bool enabled);
    
    /**
     * Publish all sensor temperatures
     */
    void publishTemperatures();
    
    /**
     * Publish temperature for a specific sensor
     * @param sensorIndex Sensor index
     */
    void publishSensorTemperature(uint8_t sensorIndex);
    
    /**
     * Publish alarm state change
     * @param sensorIndex Sensor index
     * @param state Alarm state
     * @param temperature Current temperature
     */
    void publishAlarm(uint8_t sensorIndex, AlarmState state, float temperature);
    
    /**
     * Publish device status (online/offline)
     * @param online true if online
     */
    void publishStatus(bool online);
    
    /**
     * Publish Home Assistant auto-discovery configuration
     */
    void publishHADiscovery();
    
    /**
     * Force republish of HA discovery on next update
     * Useful when sensor names are changed
     */
    void requestDiscoveryRepublish() { _haDiscoveryPublished = false; }
    
    /**
     * Get last error message
     */
    const char* getLastError() const { return _lastError; }
    
    /**
     * Get number of messages published
     */
    uint32_t getPublishCount() const { return _publishCount; }
    
private:
    WiFiClient _wifiClient;
    PubSubClient _client;
    
    uint32_t _lastConnectAttempt;
    uint32_t _lastPublishTime;
    uint32_t _publishCount;
    float _lastPublishedTemp[MAX_SENSORS];
    char _lastError[64];
    bool _haDiscoveryPublished;
    volatile bool _reconnectRequested;
    volatile bool _otaInProgress;
    
    /**
     * Attempt to connect to MQTT broker
     * @return true if connected
     */
    bool connect();
    
    /**
     * Build topic string
     * @param buffer Output buffer
     * @param bufferSize Buffer size
     * @param ... Topic parts (null-terminated list)
     */
    void buildTopic(char* buffer, size_t bufferSize, ...);
    
    /**
     * Build sensor topic string
     * @param buffer Output buffer
     * @param bufferSize Buffer size
     * @param sensorIndex Sensor index
     * @param suffix Topic suffix
     */
    void buildSensorTopic(char* buffer, size_t bufferSize, 
                          uint8_t sensorIndex, const char* suffix);
    
    /**
     * MQTT message callback
     */
    static void messageCallback(char* topic, byte* payload, unsigned int length);
    
    /**
     * Handle received message
     */
    void handleMessage(const char* topic, const char* payload);
    
    /**
     * Check if temperature change exceeds publish threshold
     */
    bool shouldPublishTemperature(uint8_t sensorIndex, float temperature);
    
    /**
     * Publish Home Assistant discovery for a sensor
     */
    void publishHADiscoverySensor(uint8_t sensorIndex);
};

// Global MQTT client instance
extern MQTTClient mqttClient;

#endif // MQTT_CLIENT_H
