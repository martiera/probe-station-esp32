/*
 * ESP32 Temperature Monitoring System
 * Web Server Header
 * 
 * Handles HTTP server and REST API including:
 * - Static file serving (SPIFFS)
 * - REST API for sensor data
 * - Configuration endpoints
 * - WebSocket for real-time updates
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "config.h"
#include "config_manager.h"
#include "sensor_manager.h"

// ============================================================================
// WebServer Class
// ============================================================================

class WebServer {
public:
    /**
     * Constructor
     */
    WebServer();
    
    /**
     * Initialize web server
     */
    void begin();
    
    /**
     * Update web server (call in main loop)
     * Handles WebSocket updates
     */
    void update();
    
    /**
     * Send WebSocket update with current sensor data
     */
    void sendSensorUpdate();
    
    /**
     * Send WebSocket notification
     * @param type Notification type (info, warning, error)
     * @param message Notification message
     */
    void sendNotification(const char* type, const char* message);
    
    /**
     * Send update notification to a specific client
     */
    void sendUpdateNotification(AsyncWebSocketClient* client);
    
    /**
     * Set OTA mode - closes all WebSocket connections to free memory
     */
    void setOtaMode(bool enabled);
    
private:
    AsyncWebServer _server;
    AsyncWebSocket _ws;
    uint32_t _lastWsUpdate;
    bool _otaMode = false; // disables WebSocket activity during OTA
    
    /**
     * Setup API routes
     */
    void setupRoutes();
    
    /**
     * Setup static file serving
     */
    void setupStaticFiles();
    
    // ========================================================================
    // API Handlers
    // ========================================================================
    
    /**
     * GET /api/status - System status
     */
    void handleGetStatus(AsyncWebServerRequest* request);
    
    /**
     * GET /api/sensors - All sensor data
     */
    void handleGetSensors(AsyncWebServerRequest* request);
    
    /**
     * GET /api/sensors/{id} - Single sensor data
     */
    void handleGetSensor(AsyncWebServerRequest* request, uint8_t sensorIndex);
    
    /**
     * PUT /api/sensors/{id} - Update sensor config
     */
    void handleUpdateSensor(AsyncWebServerRequest* request, uint8_t sensorIndex,
                           uint8_t* data, size_t len);
    
    /**
     * GET /api/config/wifi - WiFi configuration
     */
    void handleGetWiFiConfig(AsyncWebServerRequest* request);
    
    /**
     * PUT /api/config/wifi - Update WiFi configuration
     */
    void handleUpdateWiFiConfig(AsyncWebServerRequest* request,
                                uint8_t* data, size_t len);
    
    /**
     * GET /api/config/mqtt - MQTT configuration
     */
    void handleGetMQTTConfig(AsyncWebServerRequest* request);
    
    /**
     * PUT /api/config/mqtt - Update MQTT configuration
     */
    void handleUpdateMQTTConfig(AsyncWebServerRequest* request,
                                uint8_t* data, size_t len);
    
    /**
     * GET /api/config/system - System configuration
     */
    void handleGetSystemConfig(AsyncWebServerRequest* request);
    
    /**
     * PUT /api/config/system - Update system configuration
     */
    void handleUpdateSystemConfig(AsyncWebServerRequest* request,
                                  uint8_t* data, size_t len);
    
    /**
     * GET /api/wifi/scan - Scan for WiFi networks
     */
    void handleWiFiScan(AsyncWebServerRequest* request);
    
    /**
     * POST /api/calibrate - Calibrate all sensors
     */
    void handleCalibrate(AsyncWebServerRequest* request, uint8_t* data, size_t len);
    
    /**
     * POST /api/calibrate/new - Calibrate only new/uncalibrated sensors
     */
    void handleCalibrateNew(AsyncWebServerRequest* request, uint8_t* data, size_t len);
    
    /**
     * POST /api/calibrate/{id} - Calibrate single sensor
     */
    void handleCalibrateSensor(AsyncWebServerRequest* request, uint8_t sensorIndex,
                               uint8_t* data, size_t len);
    
    /**
     * POST /api/rescan - Rescan for sensors
     */
    void handleRescan(AsyncWebServerRequest* request);
    
    /**
     * POST /api/reboot - Reboot device
     */
    void handleReboot(AsyncWebServerRequest* request);
    
    /**
     * POST /api/reset - Factory reset
     */
    void handleFactoryReset(AsyncWebServerRequest* request);
    
    /**
     * GET /api/history/{id} - Get sensor temperature history
     */
    void handleGetHistory(AsyncWebServerRequest* request, uint8_t sensorIndex);

    /**
     * GET /api/ota/info - GitHub Releases OTA info
     */
    void handleGetOtaInfo(AsyncWebServerRequest* request);

    /**
     * GET /api/ota/status - OTA progress/status
     */
    void handleGetOtaStatus(AsyncWebServerRequest* request);

    /**
     * POST /api/ota/set - Manually set release info (bypasses GitHub API)
     */
    void handleSetOtaInfo(AsyncWebServerRequest* request, uint8_t* data, size_t len);

    /**
     * POST /api/ota/update - Start OTA update
     */
    void handleStartOtaUpdate(AsyncWebServerRequest* request, uint8_t* data, size_t len);
    
    /**
     * POST /api/ota/upload - Direct firmware/SPIFFS upload
     */
    void handleUploadData(AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final);
    void handleUploadComplete(AsyncWebServerRequest* request);
    
    // Upload state
    int _uploadType = 0; // 0=none, U_FLASH=firmware, U_SPIFFS=spiffs
    bool _uploadError = false;
    String _uploadErrorMsg;
    
    // ========================================================================
    // WebSocket Handlers
    // ========================================================================
    
    /**
     * WebSocket event handler
     */
    void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                   AwsEventType type, void* arg, uint8_t* data, size_t len);
    
    /**
     * Handle WebSocket message
     */
    void handleWsMessage(AsyncWebSocketClient* client, uint8_t* data, size_t len);
    
    // ========================================================================
    // Utility Methods
    // ========================================================================
    
    /**
     * Check if server has resources to handle request
     * Returns true if OK, false if 503 was sent
     */
    bool checkServerLoad(AsyncWebServerRequest* request);
    
    /**
     * Send JSON response
     */
    void sendJson(AsyncWebServerRequest* request, int code, const char* json);
    
    /**
     * Send error response
     */
    void sendError(AsyncWebServerRequest* request, int code, const char* message);
    
    /**
     * Send success response
     */
    void sendSuccess(AsyncWebServerRequest* request, const char* message = nullptr);
    
    /**
     * Build sensor JSON object
     */
    void buildSensorJson(JsonObject& obj, uint8_t sensorIndex);
};

// Global web server instance
extern WebServer webServer;

#endif // WEB_SERVER_H
