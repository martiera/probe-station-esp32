/*
 * ESP32 Temperature Monitoring System
 * Sensor Manager Implementation
 */

#include "sensor_manager.h"

// Global instance
SensorManager sensorManager;

// ============================================================================
// Helper Functions
// ============================================================================

const char* alarmStateToString(AlarmState state) {
    switch (state) {
        case AlarmState::NORMAL:       return "normal";
        case AlarmState::BELOW_LOW:    return "low";
        case AlarmState::ABOVE_HIGH:   return "high";
        case AlarmState::SENSOR_ERROR: return "error";
        default:                       return "unknown";
    }
}

// ============================================================================
// Constructor
// ============================================================================

SensorManager::SensorManager() :
    _oneWire(ONEWIRE_PIN),
    _sensors(&_oneWire),
    _sensorCount(0),
    _lastReadTime(0),
    _lastDiscoveryTime(0),
    _rescanRequested(false),
    _alarmCallback(nullptr),
    _connectionCallback(nullptr),
    _dataChanged(false),
    _readState(SensorReadState::IDLE),
    _conversionStartTime(0) {
}

// ============================================================================
// Public Methods
// ============================================================================

bool SensorManager::begin() {
    Serial.println(F("[SensorManager] Initializing..."));
    
    // Initialize DallasTemperature library
    _sensors.begin();
    _sensors.setWaitForConversion(false);  // Async mode
    
    // Discover sensors
    uint8_t found = discoverSensors();
    
    Serial.printf("[SensorManager] Initialization complete. Found %d sensors\n", found);
    
    return found > 0;
}

uint8_t SensorManager::discoverSensors() {
    Serial.println(F("[SensorManager] Scanning for sensors..."));
    
    // Get number of devices on bus
    uint8_t deviceCount = _sensors.getDeviceCount();
    Serial.printf("[SensorManager] Found %d devices on OneWire bus\n", deviceCount);
    
    // Track previously connected sensors to detect changes
    bool previouslyConnected[MAX_SENSORS];
    for (uint8_t i = 0; i < _sensorCount; i++) {
        previouslyConnected[i] = _sensorData[i].connected;
    }
    
    // Reset sensor count for rediscovery
    uint8_t oldSensorCount = _sensorCount;
    _sensorCount = 0;
    
    // Enumerate all DS18B20 sensors
    DeviceAddress addr;
    _oneWire.reset_search();
    
    while (_oneWire.search(addr) && _sensorCount < MAX_SENSORS) {
        // Check if this is a DS18B20 (family code 0x28)
        if (addr[0] != 0x28) {
            continue;
        }
        
        // Copy address
        memcpy(_sensorData[_sensorCount].address, addr, sizeof(DeviceAddress));
        
        // Convert to string
        addressToString(addr, _sensorData[_sensorCount].addressStr);
        
        // Set resolution
        _sensors.setResolution(addr, SENSOR_RESOLUTION);
        
        // Don't mark as connected yet - wait for first valid temperature reading
        // This prevents showing -127.0 on display during boot
        _sensorData[_sensorCount].connected = false;
        _sensorData[_sensorCount].errorCount = 0;
        
        // Ensure sensor has configuration
        SensorConfig* config = configManager.findOrCreateSensorConfig(
            _sensorData[_sensorCount].addressStr
        );
        
        if (config) {
            Serial.printf("[SensorManager] Sensor %d: %s (%s)\n",
                _sensorCount,
                _sensorData[_sensorCount].addressStr,
                config->name
            );
        }
        
        _sensorCount++;
    }
    
    // Check for disconnected sensors
    for (uint8_t i = _sensorCount; i < oldSensorCount; i++) {
        if (previouslyConnected[i]) {
            _sensorData[i].connected = false;
            _sensorData[i].alarmState = AlarmState::SENSOR_ERROR;
            
            if (_connectionCallback) {
                _connectionCallback(i, false);
            }
        }
    }
    
    _lastDiscoveryTime = millis();
    _rescanRequested = false;
    
    Serial.printf("[SensorManager] Discovery complete. %d DS18B20 sensors found\n", _sensorCount);
    
    return _sensorCount;
}

void SensorManager::readTemperatures() {
    if (_sensorCount == 0) {
        return;
    }
    
    // Non-blocking temperature reading using state machine
    switch (_readState) {
        case SensorReadState::IDLE:
            // Start temperature conversion
            _sensors.requestTemperatures();
            _conversionStartTime = millis();
            _readState = SensorReadState::CONVERSION_REQUESTED;
            // Exit and let conversion happen in background
            return;
            
        case SensorReadState::CONVERSION_REQUESTED:
            // Check if conversion time has elapsed (750ms for 12-bit resolution)
            if (millis() - _conversionStartTime < 750) {
                // Still converting, exit and check again next update
                return;
            }
            // Conversion complete, ready to read
            _readState = SensorReadState::READY_TO_READ;
            // Fall through to read values
            
        case SensorReadState::READY_TO_READ:
            // Conversion complete, now read the values
            break;
    }
    
    // Read temperatures from all discovered sensors
    for (uint8_t i = 0; i < _sensorCount; i++) {
        float temp = _sensors.getTempC(_sensorData[i].address);
        
        // Check for valid reading
        if (temp == DEVICE_DISCONNECTED_C || temp < -55.0f || temp > 125.0f) {
            _sensorData[i].errorCount++;
            
            if (_sensorData[i].errorCount >= 3) {
                // Mark as disconnected after 3 consecutive errors
                if (_sensorData[i].connected) {
                    _sensorData[i].connected = false;
                    _sensorData[i].temperature = TEMP_INVALID;
                    _sensorData[i].rawTemperature = TEMP_INVALID;
                    
                    AlarmState oldState = _sensorData[i].alarmState;
                    _sensorData[i].alarmState = AlarmState::SENSOR_ERROR;
                    
                    if (_connectionCallback) {
                        _connectionCallback(i, false);
                    }
                    
                    if (_alarmCallback && oldState != AlarmState::SENSOR_ERROR) {
                        _alarmCallback(i, oldState, AlarmState::SENSOR_ERROR, TEMP_INVALID);
                    }
                }
            }
            continue;
        }
        
        // Valid reading
        _sensorData[i].errorCount = 0;
        
        // Check if sensor was reconnected
        if (!_sensorData[i].connected) {
            _sensorData[i].connected = true;
            if (_connectionCallback) {
                _connectionCallback(i, true);
            }
        }
        
        // Store raw temperature
        _sensorData[i].rawTemperature = temp;
        
        // Apply calibration
        _sensorData[i].temperature = applyCalibration(i, temp);
        
        // Add to history
        addToHistory(i, _sensorData[i].temperature);
    }
    
    _lastReadTime = millis();
    
    // Check alarm states
    checkAlarms();
    
    // Mark data as changed
    _dataChanged = true;
    
    // Reset state machine for next reading cycle
    _readState = SensorReadState::IDLE;
}

void SensorManager::update() {
    uint32_t now = millis();
    
    // Manual sensor discovery only (via rescan button)
    if (_rescanRequested) {
        discoverSensors();
    }
    
    // Non-blocking temperature reading state machine
    uint32_t readInterval = configManager.getSystemConfig().readInterval * 1000;
    
    if (_readState == SensorReadState::IDLE) {
        // Start new reading cycle if interval has elapsed
        if (now - _lastReadTime >= readInterval) {
            readTemperatures();
        }
    } else {
        // Continue existing reading cycle (checking conversion status)
        readTemperatures();
    }
}

SensorData* SensorManager::getSensorData(uint8_t index) {
    if (index >= _sensorCount) {
        return nullptr;
    }
    return &_sensorData[index];
}

const SensorData* SensorManager::getSensorData(uint8_t index) const {
    if (index >= _sensorCount) {
        return nullptr;
    }
    return &_sensorData[index];
}

SensorData* SensorManager::getSensorDataByAddress(const char* address) {
    for (uint8_t i = 0; i < _sensorCount; i++) {
        if (strcmp(_sensorData[i].addressStr, address) == 0) {
            return &_sensorData[i];
        }
    }
    return nullptr;
}

int8_t SensorManager::getSensorIndexByAddress(const char* address) {
    for (uint8_t i = 0; i < _sensorCount; i++) {
        if (strcmp(_sensorData[i].addressStr, address) == 0) {
            return i;
        }
    }
    return -1;
}

void SensorManager::addressToString(const DeviceAddress addr, char* buffer) {
    for (uint8_t i = 0; i < 8; i++) {
        sprintf(buffer + (i * 2), "%02X", addr[i]);
    }
    buffer[16] = '\0';
}

void SensorManager::calibrateAll(float referenceTemp) {
    Serial.printf("[SensorManager] Calibrating all sensors to %.2f°C\n", referenceTemp);
    
    for (uint8_t i = 0; i < _sensorCount; i++) {
        calibrateSensor(i, referenceTemp);
    }
    
    configManager.save();
}

uint8_t SensorManager::calibrateUncalibrated(float referenceTemp) {
    Serial.printf("[SensorManager] Calibrating uncalibrated sensors to %.2f°C\n", referenceTemp);
    
    uint8_t count = 0;
    for (uint8_t i = 0; i < _sensorCount; i++) {
        if (isUncalibrated(i)) {
            calibrateSensor(i, referenceTemp);
            count++;
        }
    }
    
    if (count > 0) {
        configManager.save();
    }
    
    Serial.printf("[SensorManager] Calibrated %d uncalibrated sensors\n", count);
    return count;
}

bool SensorManager::isUncalibrated(uint8_t index) const {
    if (index >= _sensorCount) return false;
    
    const SensorConfig* config = configManager.getSensorConfigByAddress(
        _sensorData[index].addressStr
    );
    
    if (!config) return true;  // No config = uncalibrated
    
    // Check if using default name (starts with "Sensor " or is empty)
    bool hasDefaultName = (strlen(config->name) == 0) ||
                          (strncmp(config->name, "Sensor ", 7) == 0);
    
    // Check if calibration offset is zero (never calibrated)
    bool hasZeroOffset = (config->calibrationOffset == 0.0f);
    
    // Consider uncalibrated if has default name AND zero offset
    return hasDefaultName && hasZeroOffset;
}

void SensorManager::calibrateSensor(uint8_t index, float referenceTemp) {
    if (index >= _sensorCount || !_sensorData[index].connected) {
        return;
    }
    
    // Calculate offset: reference - raw
    float offset = referenceTemp - _sensorData[index].rawTemperature;
    
    // Get config and update offset
    SensorConfig* config = configManager.getSensorConfigByAddress(
        _sensorData[index].addressStr
    );
    
    if (config) {
        config->calibrationOffset = offset;
        configManager.markDirty();
        
        Serial.printf("[SensorManager] Sensor %d (%s) calibrated. Offset: %.2f\n",
            index, config->name, offset);
        
        // Update current temperature with new calibration
        _sensorData[index].temperature = applyCalibration(index, 
            _sensorData[index].rawTemperature);
    }
}

void SensorManager::resetCalibration() {
    Serial.println(F("[SensorManager] Resetting all calibration offsets"));
    
    for (uint8_t i = 0; i < _sensorCount; i++) {
        resetSensorCalibration(i);
    }
    
    configManager.save();
}

void SensorManager::resetSensorCalibration(uint8_t index) {
    if (index >= _sensorCount) {
        return;
    }
    
    SensorConfig* config = configManager.getSensorConfigByAddress(
        _sensorData[index].addressStr
    );
    
    if (config) {
        config->calibrationOffset = 0.0f;
        configManager.markDirty();
        
        // Update current temperature
        _sensorData[index].temperature = _sensorData[index].rawTemperature;
    }
}

float SensorManager::getAverageTemperature() const {
    float sum = 0;
    uint8_t count = 0;
    
    for (uint8_t i = 0; i < _sensorCount; i++) {
        if (_sensorData[i].connected && _sensorData[i].temperature != TEMP_INVALID) {
            sum += _sensorData[i].temperature;
            count++;
        }
    }
    
    return count > 0 ? sum / count : TEMP_INVALID;
}

float SensorManager::getMinTemperature() const {
    float minTemp = TEMP_INVALID;
    bool found = false;
    
    for (uint8_t i = 0; i < _sensorCount; i++) {
        if (_sensorData[i].connected && _sensorData[i].temperature != TEMP_INVALID) {
            if (!found || _sensorData[i].temperature < minTemp) {
                minTemp = _sensorData[i].temperature;
                found = true;
            }
        }
    }
    
    return minTemp;
}

float SensorManager::getMaxTemperature() const {
    float maxTemp = TEMP_INVALID;
    bool found = false;
    
    for (uint8_t i = 0; i < _sensorCount; i++) {
        if (_sensorData[i].connected && _sensorData[i].temperature != TEMP_INVALID) {
            if (!found || _sensorData[i].temperature > maxTemp) {
                maxTemp = _sensorData[i].temperature;
                found = true;
            }
        }
    }
    
    return maxTemp;
}

bool SensorManager::hasAlarm() const {
    for (uint8_t i = 0; i < _sensorCount; i++) {
        if (_sensorData[i].alarmState == AlarmState::BELOW_LOW ||
            _sensorData[i].alarmState == AlarmState::ABOVE_HIGH) {
            return true;
        }
    }
    return false;
}

uint8_t SensorManager::getAlarmCount() const {
    uint8_t count = 0;
    
    for (uint8_t i = 0; i < _sensorCount; i++) {
        if (_sensorData[i].alarmState == AlarmState::BELOW_LOW ||
            _sensorData[i].alarmState == AlarmState::ABOVE_HIGH) {
            count++;
        }
    }
    
    return count;
}

// ============================================================================
// Private Methods
// ============================================================================

void SensorManager::checkAlarms() {
    for (uint8_t i = 0; i < _sensorCount; i++) {
        if (_sensorData[i].connected) {
            updateAlarmState(i);
        }
    }
}

void SensorManager::updateAlarmState(uint8_t index) {
    if (index >= _sensorCount || !_sensorData[index].connected) {
        return;
    }
    
    const SensorConfig* config = configManager.getSensorConfigByAddress(
        _sensorData[index].addressStr
    );
    
    if (!config || !config->alertEnabled) {
        _sensorData[index].alarmState = AlarmState::NORMAL;
        return;
    }
    
    float temp = _sensorData[index].temperature;
    AlarmState newState = AlarmState::NORMAL;
    AlarmState currentState = _sensorData[index].alarmState;
    
    // Apply hysteresis to prevent rapid toggling
    float lowThreshold = config->thresholdLow;
    float highThreshold = config->thresholdHigh;
    
    if (currentState == AlarmState::BELOW_LOW) {
        // Already in low alarm, need to rise above threshold + hysteresis to clear
        lowThreshold += THRESHOLD_HYSTERESIS;
    } else if (currentState == AlarmState::ABOVE_HIGH) {
        // Already in high alarm, need to drop below threshold - hysteresis to clear
        highThreshold -= THRESHOLD_HYSTERESIS;
    }
    
    // Determine new state
    if (temp < lowThreshold) {
        newState = AlarmState::BELOW_LOW;
    } else if (temp > highThreshold) {
        newState = AlarmState::ABOVE_HIGH;
    } else {
        newState = AlarmState::NORMAL;
    }
    
    // Check for state change
    if (newState != currentState) {
        _sensorData[index].prevAlarmState = currentState;
        _sensorData[index].alarmState = newState;
        
        // Trigger callback
        if (_alarmCallback) {
            _alarmCallback(index, currentState, newState, temp);
        }
        
        Serial.printf("[SensorManager] Sensor %d alarm state: %s -> %s (%.1f°C)\n",
            index, 
            alarmStateToString(currentState),
            alarmStateToString(newState),
            temp
        );
    }
}

void SensorManager::addToHistory(uint8_t index, float temp) {
    if (index >= _sensorCount) {
        return;
    }
    
    // Skip invalid temperatures
    if (temp == TEMP_INVALID) {
        return;
    }
    
    SensorData& sensor = _sensorData[index];
    uint32_t now = millis();
    uint32_t timeSinceLastHistory = now - sensor.lastHistoryTime;
    
    // Round temperature to 0.1°C for comparison
    float roundedTemp = roundf(temp * 10.0f) / 10.0f;
    float lastRoundedTemp = roundf(sensor.lastHistoryTemp * 10.0f) / 10.0f;
    float tempDiff = fabsf(roundedTemp - lastRoundedTemp);
    
    // Determine if we should store this reading:
    // - First reading (no history yet)
    // - 1 minute passed AND temperature changed by >= 0.1°C
    // - 5 minutes passed (force store even if stable)
    bool shouldStore = false;
    
    if (sensor.historyCount == 0) {
        shouldStore = true;  // First reading
    } else if (timeSinceLastHistory >= 5 * 60 * 1000) {
        shouldStore = true;  // 5 minutes passed - force store
    } else if (timeSinceLastHistory >= 60 * 1000 && tempDiff >= 0.1f) {
        shouldStore = true;  // 1 minute passed and temp changed
    }
    
    if (!shouldStore) {
        return;
    }
    
    // Store as int16_t (temp * 100) to save memory
    int16_t historyValue = (int16_t)(temp * 100.0f);
    sensor.history[sensor.historyIndex] = historyValue;
    sensor.historyIndex = (sensor.historyIndex + 1) % TEMP_HISTORY_SIZE;
    
    if (sensor.historyCount < TEMP_HISTORY_SIZE) {
        sensor.historyCount++;
    }
    
    // Update last history tracking
    sensor.lastHistoryTime = now;
    sensor.lastHistoryTemp = temp;
}

float SensorManager::applyCalibration(uint8_t index, float rawTemp) {
    if (rawTemp == TEMP_INVALID) {
        return TEMP_INVALID;
    }
    
    const SensorConfig* config = configManager.getSensorConfigByAddress(
        _sensorData[index].addressStr
    );
    
    if (config) {
        return rawTemp + config->calibrationOffset;
    }
    
    return rawTemp;
}
