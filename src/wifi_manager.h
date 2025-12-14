/*
 * ESP32 Temperature Monitoring System
 * WiFi Manager Header
 * 
 * Handles WiFi connectivity including:
 * - Station mode (connect to existing WiFi)
 * - Access Point mode (create hotspot for configuration)
 * - Automatic fallback to AP mode when WiFi unavailable
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include "config.h"
#include "config_manager.h"

// ============================================================================
// Enums
// ============================================================================

/**
 * WiFi manager state
 */
enum class WiFiState {
    DISCONNECTED,    // Not connected to any network
    CONNECTING,      // Attempting to connect to WiFi
    CONNECTED,       // Connected to WiFi in station mode
    AP_MODE,         // Running in Access Point mode
    AP_STA_MODE      // Running both AP and Station mode
};

// ============================================================================
// Callback Types
// ============================================================================

/**
 * Callback for WiFi state changes
 */
typedef void (*WiFiStateCallback)(WiFiState oldState, WiFiState newState);

// ============================================================================
// WiFiManager Class
// ============================================================================

class WiFiManager {
public:
    /**
     * Constructor
     */
    WiFiManager();
    
    /**
     * Initialize WiFi manager
     * Attempts to connect to configured WiFi, falls back to AP mode if fails
     */
    void begin();
    
    /**
     * Update WiFi manager (call in main loop)
     * Handles reconnection attempts and state management
     */
    void update();
    
    /**
     * Get current WiFi state
     */
    WiFiState getState() const { return _state; }
    
    /**
     * Check if connected to WiFi (station mode)
     */
    bool isConnected() const { return _state == WiFiState::CONNECTED; }
    
    /**
     * Check if in AP mode
     */
    bool isAPMode() const { 
        return _state == WiFiState::AP_MODE || _state == WiFiState::AP_STA_MODE; 
    }
    
    /**
     * Get IP address (station mode)
     */
    IPAddress getIP() const;
    
    /**
     * Get AP IP address
     */
    IPAddress getAPIP() const;
    
    /**
     * Get WiFi signal strength (RSSI) in dBm
     * @return RSSI value or 0 if not connected
     */
    int32_t getRSSI() const;
    
    /**
     * Get WiFi signal strength as percentage (0-100)
     */
    uint8_t getSignalStrength() const;
    
    /**
     * Get connected SSID
     */
    String getSSID() const;
    
    /**
     * Get MAC address
     */
    String getMACAddress() const;
    
    /**
     * Get hostname
     */
    String getHostname() const;
    
    /**
     * Connect to WiFi with provided credentials
     * @param ssid WiFi SSID
     * @param password WiFi password
     * @param save Whether to save credentials to config
     * @return true if connection initiated
     */
    bool connect(const char* ssid, const char* password, bool save = true);
    
    /**
     * Disconnect from WiFi
     */
    void disconnect();
    
    /**
     * Start Access Point mode
     * @param keepStation Keep station mode active (AP+STA mode)
     */
    void startAP(bool keepStation = false);
    
    /**
     * Stop Access Point mode
     */
    void stopAP();
    
    /**
     * Force reconnection attempt
     */
    void reconnect();
    
    /**
     * Scan for available networks (async)
     * @return Number of networks found, or -1 if scan in progress, -2 if starting scan
     */
    int16_t scanNetworks();
    
    /**
     * Start async WiFi scan
     * @return true if scan started
     */
    bool startAsyncScan();
    
    /**
     * Check if scan is complete
     * @return true if scan results are ready
     */
    bool isScanComplete() const { return _scanComplete; }
    
    /**
     * Get scan results count
     */
    int16_t getScanResultCount() const { return _scanResults; }
    
    /**
     * Get scanned network info
     * @param index Network index
     * @param ssid Output SSID
     * @param rssi Output signal strength
     * @param encrypted Output whether network is encrypted
     * @return true if valid index
     */
    bool getScannedNetwork(uint8_t index, String& ssid, int32_t& rssi, bool& encrypted);
    
    /**
     * Get number of clients connected to AP
     */
    uint8_t getAPClientCount() const;
    
    /**
     * Set state change callback
     */
    void setStateCallback(WiFiStateCallback callback) { _stateCallback = callback; }
    
    /**
     * Get state as string
     */
    static const char* stateToString(WiFiState state);
    
private:
    WiFiState _state;
    WiFiState _previousState;
    uint32_t _lastConnectAttempt;
    uint32_t _connectStartTime;
    uint8_t _connectAttempts;
    bool _apActive;
    int16_t _scanResults;
    bool _scanComplete;
    bool _scanInProgress;
    
    // Captive portal DNS server
    DNSServer* _dnsServer;
    
    WiFiStateCallback _stateCallback;
    
    /**
     * Set new state and trigger callback if changed
     */
    void setState(WiFiState newState);
    
    /**
     * Attempt to connect to configured WiFi
     */
    void attemptConnection();
    
    /**
     * Handle connection timeout
     */
    void handleConnectionTimeout();
    
    /**
     * Configure static IP if enabled
     */
    void configureStaticIP();
    
    /**
     * Configure hostname
     */
    void configureHostname();
};

// Global WiFi manager instance
extern WiFiManager wifiManager;

#endif // WIFI_MANAGER_H
