/*
 * Display Manager for TTGO T-Display
 * Handles TFT display output for temperature readings
 */

#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>
#include "config.h"

#ifdef USE_DISPLAY
#include <TFT_eSPI.h>
#endif

// Forward declarations
class SensorManager;
class WiFiManager;
class MQTTClient;

// Display pages
enum class DisplayPage : uint8_t {
    SENSORS,      // Show temperature readings
    STATUS,       // Show WiFi/MQTT status
    ALERTS,       // Show active alerts
    INFO          // Show device info
};

class DisplayManager {
public:
    DisplayManager();
    
    // Initialize display
    void begin();
    
    // Update display (call in loop)
    void update();
    
    // Set references to other managers
    void setSensorManager(SensorManager* sm) { sensorManager = sm; }
    void setWiFiManager(WiFiManager* wm) { wifiManager = wm; }
    void setMQTTClient(MQTTClient* mc) { mqttClient = mc; }
    
    // Navigation
    void nextPage();
    void previousPage();
    void nextSensorPage();
    
    // Force refresh
    void refresh();
    
    // Set brightness (0-255)
    void setBrightness(uint8_t level);
    
    // Handle button press
    void handleButton1();  // Top button
    void handleButton2();  // Bottom button

private:
#ifdef USE_DISPLAY
    TFT_eSPI tft;
    TFT_eSprite sprite;  // For flicker-free updates
#endif
    
    SensorManager* sensorManager = nullptr;
    WiFiManager* wifiManager = nullptr;
    MQTTClient* mqttClient = nullptr;
    
    DisplayPage currentPage = DisplayPage::SENSORS;
    uint8_t sensorPageOffset = 0;  // For scrolling through sensors
    uint8_t brightness = 255;
    
    uint32_t lastUpdate = 0;
    uint32_t lastButtonPress = 0;
    bool needsRefresh = true;
    
    // Display constants
    static constexpr uint16_t DISPLAY_WIDTH = 240;
    static constexpr uint16_t DISPLAY_HEIGHT = 135;
    static constexpr uint8_t SENSORS_PER_PAGE = 4;
    static constexpr uint32_t UPDATE_INTERVAL = 500;  // ms
    static constexpr uint32_t BUTTON_DEBOUNCE = 200;  // ms
    
    // Colors
    static constexpr uint16_t COLOR_BG = 0x0000;        // Black
    static constexpr uint16_t COLOR_TEXT = 0xFFFF;      // White
    static constexpr uint16_t COLOR_HEADER = 0x07E0;    // Green
    static constexpr uint16_t COLOR_TEMP_OK = 0x07E0;   // Green
    static constexpr uint16_t COLOR_TEMP_WARN = 0xFD20; // Orange
    static constexpr uint16_t COLOR_TEMP_ALERT = 0xF800;// Red
    static constexpr uint16_t COLOR_TEMP_COLD = 0x001F; // Blue
    static constexpr uint16_t COLOR_WIFI_ON = 0x07E0;   // Green
    static constexpr uint16_t COLOR_WIFI_OFF = 0xF800;  // Red
    static constexpr uint16_t COLOR_MQTT_ON = 0x07FF;   // Cyan
    static constexpr uint16_t COLOR_MQTT_OFF = 0xF800;  // Red
    
    // Drawing methods
    void drawSensorsPage();
    void drawStatusPage();
    void drawAlertsPage();
    void drawInfoPage();
    void drawHeader(const char* title);
    void drawFooter();
    
    // Helper methods
    uint16_t getTemperatureColor(float temp, float low, float high);
    const char* getAlarmStateText(uint8_t state);
};

// Global instance
extern DisplayManager displayManager;

#endif // DISPLAY_MANAGER_H
