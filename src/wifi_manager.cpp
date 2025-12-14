/*
 * ESP32 Temperature Monitoring System
 * WiFi Manager Implementation
 */

#include "wifi_manager.h"

// Global instance
WiFiManager wifiManager;

// Maximum connection attempts before falling back to AP mode
constexpr uint8_t MAX_CONNECT_ATTEMPTS = 3;

// ============================================================================
// Constructor
// ============================================================================

WiFiManager::WiFiManager() :
    _state(WiFiState::DISCONNECTED),
    _previousState(WiFiState::DISCONNECTED),
    _lastConnectAttempt(0),
    _connectStartTime(0),
    _connectAttempts(0),
    _apActive(false),
    _scanResults(-1),
    _scanComplete(false),
    _scanInProgress(false),
    _dnsServer(nullptr),
    _stateCallback(nullptr) {
}

// ============================================================================
// Public Methods
// ============================================================================

void WiFiManager::begin() {
    Serial.println(F("[WiFiManager] Initializing..."));
    
    // Set WiFi mode
    WiFi.mode(WIFI_STA);
    
    // Configure hostname
    configureHostname();
    
    // Check if we have WiFi credentials
    const WiFiConfig& wifiConfig = configManager.getWiFiConfig();
    
    if (strlen(wifiConfig.ssid) > 0) {
        Serial.printf("[WiFiManager] Attempting to connect to '%s'\n", wifiConfig.ssid);
        attemptConnection();
    } else {
        Serial.println(F("[WiFiManager] No WiFi configured, starting AP mode"));
        startAP();
    }
}

void WiFiManager::update() {
    // Process DNS requests for captive portal
    if (_dnsServer) {
        _dnsServer->processNextRequest();
    }
    
    uint32_t now = millis();
    
    switch (_state) {
        case WiFiState::CONNECTING:
            // Check if connected
            if (WiFi.status() == WL_CONNECTED) {
                Serial.println(F("[WiFiManager] Connected to WiFi"));
                Serial.printf("[WiFiManager] IP Address: %s\n", WiFi.localIP().toString().c_str());
                Serial.printf("[WiFiManager] Signal strength: %d dBm (%d%%)\n", 
                    getRSSI(), getSignalStrength());
                
                _connectAttempts = 0;
                setState(WiFiState::CONNECTED);
                
                // Stop AP if it was running as fallback
                if (_apActive) {
                    // Keep AP running for a bit to allow user to see success
                    // stopAP();
                }
            }
            // Check for timeout
            else if (now - _connectStartTime > WIFI_CONNECT_TIMEOUT) {
                handleConnectionTimeout();
            }
            break;
            
        case WiFiState::CONNECTED:
            // Check if still connected
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println(F("[WiFiManager] WiFi connection lost"));
                setState(WiFiState::DISCONNECTED);
                
                // Try to reconnect
                _lastConnectAttempt = now;
                attemptConnection();
            }
            break;
            
        case WiFiState::DISCONNECTED:
            // Attempt periodic reconnection
            if (strlen(configManager.getWiFiConfig().ssid) > 0 &&
                now - _lastConnectAttempt > WIFI_RETRY_INTERVAL) {
                Serial.println(F("[WiFiManager] Attempting reconnection..."));
                attemptConnection();
            }
            break;
            
        case WiFiState::AP_MODE:
        case WiFiState::AP_STA_MODE:
            // In AP mode, periodically try to connect to configured WiFi
            if (strlen(configManager.getWiFiConfig().ssid) > 0 &&
                now - _lastConnectAttempt > WIFI_RETRY_INTERVAL) {
                Serial.println(F("[WiFiManager] Attempting WiFi connection from AP mode..."));
                connect(configManager.getWiFiConfig().ssid, 
                       configManager.getWiFiConfig().password, false);
            }
            break;
    }
}

IPAddress WiFiManager::getIP() const {
    return WiFi.localIP();
}

IPAddress WiFiManager::getAPIP() const {
    return WiFi.softAPIP();
}

int32_t WiFiManager::getRSSI() const {
    if (_state == WiFiState::CONNECTED) {
        return WiFi.RSSI();
    }
    return 0;
}

uint8_t WiFiManager::getSignalStrength() const {
    int32_t rssi = getRSSI();
    if (rssi == 0) return 0;
    
    // Convert RSSI to percentage
    // -50 dBm or better = 100%
    // -100 dBm or worse = 0%
    if (rssi >= -50) return 100;
    if (rssi <= -100) return 0;
    
    return (uint8_t)(2 * (rssi + 100));
}

String WiFiManager::getSSID() const {
    return WiFi.SSID();
}

String WiFiManager::getMACAddress() const {
    return WiFi.macAddress();
}

String WiFiManager::getHostname() const {
    return WiFi.getHostname();
}

bool WiFiManager::connect(const char* ssid, const char* password, bool save) {
    if (!ssid || strlen(ssid) == 0) {
        return false;
    }
    
    Serial.printf("[WiFiManager] Connecting to '%s'\n", ssid);
    
    // Save credentials if requested
    if (save) {
        WiFiConfig& config = configManager.getWiFiConfig();
        strncpy(config.ssid, ssid, 32);
        strncpy(config.password, password ? password : "", 64);
        configManager.markDirty();
        configManager.save();
    }
    
    // Disconnect if currently connected
    if (WiFi.status() == WL_CONNECTED) {
        WiFi.disconnect();
        delay(100);
    }
    
    // Configure static IP if enabled
    configureStaticIP();
    
    // Start connection
    if (password && strlen(password) > 0) {
        WiFi.begin(ssid, password);
    } else {
        WiFi.begin(ssid);
    }
    
    _connectStartTime = millis();
    _lastConnectAttempt = _connectStartTime;
    setState(WiFiState::CONNECTING);
    
    return true;
}

void WiFiManager::disconnect() {
    Serial.println(F("[WiFiManager] Disconnecting from WiFi"));
    WiFi.disconnect();
    setState(WiFiState::DISCONNECTED);
}

void WiFiManager::startAP(bool keepStation) {
    Serial.println(F("[WiFiManager] Starting Access Point"));
    
    if (keepStation) {
        WiFi.mode(WIFI_AP_STA);
    } else {
        WiFi.mode(WIFI_AP);
    }
    
    // Configure AP
    WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, false, AP_MAX_CONNECTIONS);
    
    _apActive = true;
    
    IPAddress apIP = WiFi.softAPIP();
    Serial.printf("[WiFiManager] AP SSID: %s\n", AP_SSID);
    Serial.printf("[WiFiManager] AP Password: %s\n", AP_PASSWORD);
    Serial.printf("[WiFiManager] AP IP: %s\n", apIP.toString().c_str());
    
    // Start DNS server for captive portal
    if (!_dnsServer) {
        _dnsServer = new DNSServer();
    }
    // Redirect all DNS requests to AP IP for captive portal
    _dnsServer->start(53, "*", apIP);
    Serial.println(F("[WiFiManager] DNS server started for captive portal"));
    
    if (keepStation) {
        setState(WiFiState::AP_STA_MODE);
    } else {
        setState(WiFiState::AP_MODE);
    }
}

void WiFiManager::stopAP() {
    if (!_apActive) return;
    
    Serial.println(F("[WiFiManager] Stopping Access Point"));
    
    // Stop DNS server
    if (_dnsServer) {
        _dnsServer->stop();
        delete _dnsServer;
        _dnsServer = nullptr;
        Serial.println(F("[WiFiManager] DNS server stopped"));
    }
    
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    
    _apActive = false;
    
    if (_state == WiFiState::AP_MODE) {
        setState(WiFiState::DISCONNECTED);
    } else if (_state == WiFiState::AP_STA_MODE) {
        setState(WiFi.status() == WL_CONNECTED ? WiFiState::CONNECTED : WiFiState::DISCONNECTED);
    }
}

void WiFiManager::reconnect() {
    _connectAttempts = 0;
    _lastConnectAttempt = 0;
    attemptConnection();
}

int16_t WiFiManager::scanNetworks() {
    // If scan is complete, return results
    if (_scanComplete && !_scanInProgress) {
        DEBUG_PRINTF("[WiFiManager] Returning cached scan results: %d networks\n", _scanResults);
        return _scanResults;
    }
    
    // If scan is in progress, check if complete
    if (_scanInProgress) {
        int16_t result = WiFi.scanComplete();
        if (result >= 0) {
            _scanResults = result;
            _scanComplete = true;
            _scanInProgress = false;
            DEBUG_PRINTF("[WiFiManager] Scan complete: %d networks\n", _scanResults);
            return _scanResults;
        } else if (result == WIFI_SCAN_RUNNING) {
            DEBUG_PRINTLN(F("[WiFiManager] Scan still in progress"));
            return -1;  // Scan in progress
        } else {
            // Scan failed
            _scanInProgress = false;
            _scanComplete = false;
            DEBUG_PRINTLN(F("[WiFiManager] Scan failed"));
            return -2;
        }
    }
    
    // Start new async scan
    if (startAsyncScan()) {
        return -1;  // Scan started, in progress
    }
    
    return -2;  // Failed to start scan
}

bool WiFiManager::startAsyncScan() {
    DEBUG_PRINTLN(F("[WiFiManager] Starting async WiFi scan..."));
    
    // Clear previous results
    WiFi.scanDelete();
    _scanComplete = false;
    _scanResults = 0;
    
    // Start async scan (false = don't show hidden, true = async)
    int16_t result = WiFi.scanNetworks(true, false);
    
    if (result == WIFI_SCAN_RUNNING) {
        _scanInProgress = true;
        DEBUG_PRINTLN(F("[WiFiManager] Async scan started"));
        return true;
    }
    
    DEBUG_PRINTF("[WiFiManager] Failed to start scan: %d\n", result);
    _scanInProgress = false;
    return false;
}

bool WiFiManager::getScannedNetwork(uint8_t index, String& ssid, int32_t& rssi, bool& encrypted) {
    if (index >= _scanResults || _scanResults < 0) {
        return false;
    }
    
    ssid = WiFi.SSID(index);
    rssi = WiFi.RSSI(index);
    encrypted = WiFi.encryptionType(index) != WIFI_AUTH_OPEN;
    
    return true;
}

uint8_t WiFiManager::getAPClientCount() const {
    return WiFi.softAPgetStationNum();
}

const char* WiFiManager::stateToString(WiFiState state) {
    switch (state) {
        case WiFiState::DISCONNECTED: return "disconnected";
        case WiFiState::CONNECTING:   return "connecting";
        case WiFiState::CONNECTED:    return "connected";
        case WiFiState::AP_MODE:      return "ap_mode";
        case WiFiState::AP_STA_MODE:  return "ap_sta_mode";
        default:                      return "unknown";
    }
}

// ============================================================================
// Private Methods
// ============================================================================

void WiFiManager::setState(WiFiState newState) {
    if (newState != _state) {
        _previousState = _state;
        _state = newState;
        
        Serial.printf("[WiFiManager] State: %s -> %s\n",
            stateToString(_previousState),
            stateToString(_state));
        
        if (_stateCallback) {
            _stateCallback(_previousState, _state);
        }
    }
}

void WiFiManager::attemptConnection() {
    const WiFiConfig& config = configManager.getWiFiConfig();
    
    if (strlen(config.ssid) == 0) {
        Serial.println(F("[WiFiManager] No SSID configured"));
        startAP();
        return;
    }
    
    _connectAttempts++;
    _lastConnectAttempt = millis();
    
    Serial.printf("[WiFiManager] Connection attempt %d/%d\n", 
        _connectAttempts, MAX_CONNECT_ATTEMPTS);
    
    // If in AP mode, switch to AP+STA
    if (_apActive) {
        WiFi.mode(WIFI_AP_STA);
    } else {
        WiFi.mode(WIFI_STA);
    }
    
    // Configure static IP if enabled
    configureStaticIP();
    
    // Configure hostname
    configureHostname();
    
    // Start connection
    WiFi.begin(config.ssid, config.password);
    
    _connectStartTime = millis();
    setState(WiFiState::CONNECTING);
}

void WiFiManager::handleConnectionTimeout() {
    Serial.println(F("[WiFiManager] Connection timeout"));
    
    WiFi.disconnect();
    
    if (_connectAttempts >= MAX_CONNECT_ATTEMPTS) {
        Serial.println(F("[WiFiManager] Max attempts reached, starting AP mode"));
        startAP(true);  // Keep trying in background
    } else {
        setState(WiFiState::DISCONNECTED);
        // Next attempt will be triggered by update()
    }
}

void WiFiManager::configureStaticIP() {
    const WiFiConfig& config = configManager.getWiFiConfig();
    
    if (!config.dhcp) {
        IPAddress ip, gateway, subnet, dns;
        
        if (ip.fromString(config.staticIP) &&
            gateway.fromString(config.gateway) &&
            subnet.fromString(config.subnet) &&
            dns.fromString(config.dns)) {
            
            Serial.printf("[WiFiManager] Configuring static IP: %s\n", config.staticIP);
            WiFi.config(ip, gateway, subnet, dns);
        } else {
            Serial.println(F("[WiFiManager] Invalid static IP configuration, using DHCP"));
        }
    }
}

void WiFiManager::configureHostname() {
    const SystemConfig& config = configManager.getSystemConfig();
    
    String hostname = config.deviceName;
    hostname.replace(" ", "-");
    hostname.toLowerCase();
    
    WiFi.setHostname(hostname.c_str());
    
    Serial.printf("[WiFiManager] Hostname: %s\n", hostname.c_str());
}
