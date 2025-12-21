/*
 * ESP32 Temperature Monitoring System
 * Sensor Manager Header
 * 
 * Handles DS18B20 temperature sensor operations including:
 * - Sensor discovery and enumeration
 * - Temperature reading with calibration
 * - Alarm state management
 * - Temperature history
 */

#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "config.h"
#include "config_manager.h"

// ============================================================================
// Data Structures
// ============================================================================

/**
 * Alarm state for a sensor
 */
enum class AlarmState {
    NORMAL,         // Temperature within thresholds
    BELOW_LOW,      // Below low threshold
    ABOVE_HIGH,     // Above high threshold
    SENSOR_ERROR    // Sensor error (disconnected, etc.)
};

/**
 * Runtime sensor data (not persisted)
 */
// Invalid temperature marker for int16_t history (INT16_MIN)
constexpr int16_t TEMP_HISTORY_INVALID = -32768;

struct SensorData {
    DeviceAddress address;                  // Raw sensor address
    char addressStr[SENSOR_ADDR_STR_LEN];   // Address as hex string
    float temperature;                       // Current calibrated temperature
    float rawTemperature;                    // Raw temperature (before calibration)
    int16_t history[TEMP_HISTORY_SIZE];      // Temperature history (temp*100), saves ~50% RAM
    uint16_t historyIndex;                   // Current position in history buffer
    uint16_t historyCount;                   // Number of valid history entries
    uint32_t lastHistoryTime;                // Last time a history point was stored
    float lastHistoryTemp;                   // Last temperature stored in history
    AlarmState alarmState;                   // Current alarm state
    AlarmState prevAlarmState;               // Previous alarm state (for change detection)
    bool connected;                          // Whether sensor is currently responding
    uint32_t errorCount;                     // Consecutive error count
    
    SensorData() : 
        temperature(TEMP_INVALID),
        rawTemperature(TEMP_INVALID),
        historyIndex(0),
        historyCount(0),
        lastHistoryTime(0),
        lastHistoryTemp(TEMP_INVALID),
        alarmState(AlarmState::SENSOR_ERROR),
        prevAlarmState(AlarmState::SENSOR_ERROR),
        connected(false),
        errorCount(0) {
        addressStr[0] = '\0';
        memset(address, 0, sizeof(address));
        for (uint16_t i = 0; i < TEMP_HISTORY_SIZE; i++) {
            history[i] = TEMP_HISTORY_INVALID;
        }
    }
};

// ============================================================================
// Callback Types
// ============================================================================

/**
 * Callback for alarm state changes
 * @param sensorIndex Index of the sensor
 * @param oldState Previous alarm state
 * @param newState New alarm state
 * @param temperature Current temperature
 */
typedef void (*AlarmCallback)(uint8_t sensorIndex, AlarmState oldState, 
                               AlarmState newState, float temperature);

/**
 * Callback for sensor connection changes
 * @param sensorIndex Index of the sensor
 * @param connected true if sensor connected, false if disconnected
 */
typedef void (*ConnectionCallback)(uint8_t sensorIndex, bool connected);

// ============================================================================
// SensorManager Class
// ============================================================================

/**
 * Temperature reading state for non-blocking operation
 */
enum class SensorReadState : uint8_t {
    IDLE,                   // Ready to start new reading
    CONVERSION_REQUESTED,   // Conversion in progress, waiting for completion
    READY_TO_READ          // Conversion complete, ready to read values
};

class SensorManager {
public:
    /**
     * Constructor
     */
    SensorManager();
    
    /**
     * Initialize the sensor manager
     * @return true if at least one sensor found
     */
    bool begin();
    
    /**
     * Discover all connected sensors
     * @return Number of sensors found
     */
    uint8_t discoverSensors();
    
    /**
     * Read temperatures from all sensors
     * Must be called periodically
     */
    void readTemperatures();
    
    /**
     * Update sensor manager (call in main loop)
     * Handles periodic reading and alarm checking
     */
    void update();
    
    /**
     * Get the number of discovered sensors
     */
    uint8_t getSensorCount() const { return _sensorCount; }
    
    /**
     * Get sensor data by index
     * @param index Sensor index (0 to getSensorCount()-1)
     * @return Pointer to sensor data or nullptr if invalid
     */
    SensorData* getSensorData(uint8_t index);
    const SensorData* getSensorData(uint8_t index) const;
    
    /**
     * Get sensor data by address string
     * @param address Sensor address as hex string
     * @return Pointer to sensor data or nullptr if not found
     */
    SensorData* getSensorDataByAddress(const char* address);
    
    /**
     * Get sensor index by address string
     * @param address Sensor address as hex string
     * @return Sensor index or -1 if not found
     */
    int8_t getSensorIndexByAddress(const char* address);
    
    /**
     * Convert DeviceAddress to hex string
     * @param addr DeviceAddress to convert
     * @param buffer Output buffer (must be at least SENSOR_ADDR_STR_LEN bytes)
     */
    static void addressToString(const DeviceAddress addr, char* buffer);
    
    /**
     * Perform calibration for all sensors
     * Sets calibration offsets so all sensors read the reference temperature
     * @param referenceTemp The known temperature all sensors should read
     */
    void calibrateAll(float referenceTemp);
    
    /**
     * Calibrate only uncalibrated sensors (default name or zero offset)
     * @param referenceTemp The known temperature the sensors should read
     * @return Number of sensors calibrated
     */
    uint8_t calibrateUncalibrated(float referenceTemp);
    
    /**
     * Check if a sensor is considered "uncalibrated"
     * @param index Sensor index
     * @return true if sensor has default name and zero calibration offset
     */
    bool isUncalibrated(uint8_t index) const;
    
    /**
     * Calibrate a single sensor
     * @param index Sensor index
     * @param referenceTemp The known temperature the sensor should read
     */
    void calibrateSensor(uint8_t index, float referenceTemp);
    
    /**
     * Reset calibration for all sensors
     */
    void resetCalibration();
    
    /**
     * Reset calibration for a single sensor
     * @param index Sensor index
     */
    void resetSensorCalibration(uint8_t index);
    
    /**
     * Get average temperature across all connected sensors
     * @return Average temperature or TEMP_INVALID if no sensors connected
     */
    float getAverageTemperature() const;
    
    /**
     * Get minimum temperature across all connected sensors
     * @return Minimum temperature or TEMP_INVALID if no sensors connected
     */
    float getMinTemperature() const;
    
    /**
     * Get maximum temperature across all connected sensors
     * @return Maximum temperature or TEMP_INVALID if no sensors connected
     */
    float getMaxTemperature() const;
    
    /**
     * Check if any sensor is in alarm state
     */
    bool hasAlarm() const;
    
    /**
     * Get number of sensors in alarm state
     */
    uint8_t getAlarmCount() const;
    
    /**
     * Set alarm callback
     */
    void setAlarmCallback(AlarmCallback callback) { _alarmCallback = callback; }
    
    /**
     * Set connection callback
     */
    void setConnectionCallback(ConnectionCallback callback) { _connectionCallback = callback; }
    
    /**
     * Force a sensor rescan on next update
     */
    void requestRescan() { _rescanRequested = true; }
    
    /**
     * Check if sensor data has changed since last check
     * Clears the flag after returning true
     * @return true if data changed
     */
    bool hasDataChanged() {
        bool changed = _dataChanged;
        if (changed) _dataChanged = false;
        return changed;
    }
    
private:
    OneWire _oneWire;
    DallasTemperature _sensors;
    SensorData _sensorData[MAX_SENSORS];
    uint8_t _sensorCount;
    uint32_t _lastReadTime;
    uint32_t _lastDiscoveryTime;
    bool _rescanRequested;
    
    // Non-blocking temperature reading state
    SensorReadState _readState;
    uint32_t _conversionStartTime;
    
    AlarmCallback _alarmCallback;
    ConnectionCallback _connectionCallback;
    bool _dataChanged;
    
    /**
     * Check and update alarm states for all sensors
     */
    void checkAlarms();
    
    /**
     * Update alarm state for a single sensor
     * @param index Sensor index
     */
    void updateAlarmState(uint8_t index);
    
    /**
     * Add temperature to history buffer
     * @param index Sensor index
     * @param temp Temperature to add
     */
    void addToHistory(uint8_t index, float temp);
    
    /**
     * Apply calibration offset to raw temperature
     * @param index Sensor index
     * @param rawTemp Raw temperature from sensor
     * @return Calibrated temperature
     */
    float applyCalibration(uint8_t index, float rawTemp);
};

// Global sensor manager instance
extern SensorManager sensorManager;

/**
 * Get alarm state as string
 */
const char* alarmStateToString(AlarmState state);

#endif // SENSOR_MANAGER_H
