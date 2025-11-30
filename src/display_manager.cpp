/*
 * Display Manager Implementation for TTGO T-Display
 */

#include "display_manager.h"
#include "sensor_manager.h"
#include "wifi_manager.h"
#include "mqtt_client.h"
#include "config_manager.h"

// Global instance
DisplayManager displayManager;

DisplayManager::DisplayManager()
#ifdef USE_DISPLAY
    : sprite(&tft)
#endif
{
}

void DisplayManager::begin() {
#ifdef USE_DISPLAY
    DEBUG_PRINTLN(F("[Display] Initializing TFT..."));
    
    tft.init();
    tft.setRotation(1);  // Landscape mode
    tft.fillScreen(COLOR_BG);
    
    // Initialize sprite for flicker-free updates
    sprite.createSprite(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    sprite.setTextDatum(TL_DATUM);
    
    // Set backlight
    pinMode(TFT_BL, OUTPUT);
    setBrightness(brightness);
    
    // Show splash screen
    tft.setTextColor(COLOR_HEADER);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Temperature Monitor", DISPLAY_WIDTH/2, DISPLAY_HEIGHT/2 - 15, 2);
    tft.setTextColor(COLOR_TEXT);
    tft.drawString("v" + String(FIRMWARE_VERSION), DISPLAY_WIDTH/2, DISPLAY_HEIGHT/2 + 15, 2);
    
    delay(1500);
    
    needsRefresh = true;
    DEBUG_PRINTLN(F("[Display] TFT initialized"));
#else
    DEBUG_PRINTLN(F("[Display] Display support disabled"));
#endif
}

void DisplayManager::update() {
#ifdef USE_DISPLAY
    uint32_t now = millis();
    
    // Check if update needed
    if (!needsRefresh && (now - lastUpdate < UPDATE_INTERVAL)) {
        return;
    }
    
    lastUpdate = now;
    needsRefresh = false;
    
    // Clear sprite
    sprite.fillSprite(COLOR_BG);
    
    // Draw current page
    switch (currentPage) {
        case DisplayPage::SENSORS:
            drawSensorsPage();
            break;
        case DisplayPage::STATUS:
            drawStatusPage();
            break;
        case DisplayPage::ALERTS:
            drawAlertsPage();
            break;
        case DisplayPage::INFO:
            drawInfoPage();
            break;
    }
    
    // Draw footer with navigation hint
    drawFooter();
    
    // Push sprite to display
    sprite.pushSprite(0, 0);
#endif
}

void DisplayManager::nextPage() {
    uint8_t page = static_cast<uint8_t>(currentPage);
    page = (page + 1) % 4;
    currentPage = static_cast<DisplayPage>(page);
    sensorPageOffset = 0;
    needsRefresh = true;
    DEBUG_PRINTF("[Display] Page: %d\n", page);
}

void DisplayManager::previousPage() {
    uint8_t page = static_cast<uint8_t>(currentPage);
    page = (page + 3) % 4;  // +3 is same as -1 mod 4
    currentPage = static_cast<DisplayPage>(page);
    sensorPageOffset = 0;
    needsRefresh = true;
}

void DisplayManager::nextSensorPage() {
    if (sensorManager == nullptr) return;
    
    uint8_t sensorCount = sensorManager->getSensorCount();
    if (sensorCount > SENSORS_PER_PAGE) {
        sensorPageOffset = (sensorPageOffset + SENSORS_PER_PAGE) % sensorCount;
        needsRefresh = true;
    }
}

void DisplayManager::refresh() {
    needsRefresh = true;
}

void DisplayManager::setBrightness(uint8_t level) {
#ifdef USE_DISPLAY
    brightness = level;
    // Use PWM for brightness control
    analogWrite(TFT_BL, level);
#endif
}

void DisplayManager::handleButton1() {
    uint32_t now = millis();
    if (now - lastButtonPress < BUTTON_DEBOUNCE) return;
    lastButtonPress = now;
    
    if (currentPage == DisplayPage::SENSORS) {
        nextSensorPage();
    } else {
        nextPage();
    }
}

void DisplayManager::handleButton2() {
    uint32_t now = millis();
    if (now - lastButtonPress < BUTTON_DEBOUNCE) return;
    lastButtonPress = now;
    
    nextPage();
}

#ifdef USE_DISPLAY

void DisplayManager::drawHeader(const char* title) {
    sprite.fillRect(0, 0, DISPLAY_WIDTH, 20, COLOR_HEADER);
    sprite.setTextColor(COLOR_BG);
    sprite.setTextDatum(MC_DATUM);
    sprite.drawString(title, DISPLAY_WIDTH/2, 10, 2);
    sprite.setTextDatum(TL_DATUM);
}

void DisplayManager::drawFooter() {
    sprite.setTextColor(0x7BEF);  // Gray
    sprite.setTextDatum(BC_DATUM);
    
    const char* hint;
    switch (currentPage) {
        case DisplayPage::SENSORS:
            hint = "BTN1:Scroll BTN2:Menu";
            break;
        default:
            hint = "BTN2:Next Page";
            break;
    }
    sprite.drawString(hint, DISPLAY_WIDTH/2, DISPLAY_HEIGHT - 2, 1);
    sprite.setTextDatum(TL_DATUM);
}

void DisplayManager::drawSensorsPage() {
    drawHeader("Sensors");
    
    if (sensorManager == nullptr) {
        sprite.setTextColor(COLOR_TEXT);
        sprite.drawString("No sensor manager", 10, 40, 2);
        return;
    }
    
    uint8_t sensorCount = sensorManager->getSensorCount();
    
    if (sensorCount == 0) {
        sprite.setTextColor(COLOR_TEMP_WARN);
        sprite.drawString("No sensors found", 10, 40, 2);
        sprite.setTextColor(COLOR_TEXT);
        sprite.drawString("Check connections", 10, 65, 2);
        return;
    }
    
    // Draw sensors
    uint8_t y = 25;
    uint8_t displayed = 0;
    
    for (uint8_t i = sensorPageOffset; 
         i < sensorCount && displayed < SENSORS_PER_PAGE; 
         i++, displayed++) {
        
        const SensorData* data = sensorManager->getSensorData(i);
        if (!data) continue;
        
        const SensorConfig* config = configManager.getSensorConfigByAddress(data->addressStr);
        
        // Sensor name (truncated if needed)
        String name = config ? String(config->name) : String("Sensor ") + String(i + 1);
        if (name.length() > 12) {
            name = name.substring(0, 11) + ".";
        }
        
        sprite.setTextColor(COLOR_TEXT);
        sprite.drawString(name, 5, y, 2);
        
        // Temperature value
        char tempStr[12];
        if (data->temperature > TEMP_INVALID + 1) {
            float threshLow = config ? config->thresholdLow : DEFAULT_THRESHOLD_LOW;
            float threshHigh = config ? config->thresholdHigh : DEFAULT_THRESHOLD_HIGH;
            
            uint16_t tempColor = getTemperatureColor(data->temperature, threshLow, threshHigh);
            sprite.setTextColor(tempColor);
            
            snprintf(tempStr, sizeof(tempStr), "%.1fC", data->temperature);
        } else {
            sprite.setTextColor(COLOR_TEMP_ALERT);
            strcpy(tempStr, "ERR");
        }
        
        sprite.setTextDatum(TR_DATUM);
        sprite.drawString(tempStr, DISPLAY_WIDTH - 5, y, 4);
        sprite.setTextDatum(TL_DATUM);
        
        y += 26;
    }
    
    // Page indicator if multiple pages
    if (sensorCount > SENSORS_PER_PAGE) {
        uint8_t currentPageNum = sensorPageOffset / SENSORS_PER_PAGE + 1;
        uint8_t totalPages = (sensorCount + SENSORS_PER_PAGE - 1) / SENSORS_PER_PAGE;
        
        char pageStr[10];
        snprintf(pageStr, sizeof(pageStr), "%d/%d", currentPageNum, totalPages);
        
        sprite.setTextColor(0x7BEF);
        sprite.setTextDatum(BR_DATUM);
        sprite.drawString(pageStr, DISPLAY_WIDTH - 5, DISPLAY_HEIGHT - 12, 1);
        sprite.setTextDatum(TL_DATUM);
    }
}

void DisplayManager::drawStatusPage() {
    drawHeader("Status");
    
    uint8_t y = 28;
    
    // WiFi status
    sprite.setTextColor(COLOR_TEXT);
    sprite.drawString("WiFi:", 10, y, 2);
    
    if (wifiManager != nullptr) {
        bool connected = wifiManager->isConnected();
        sprite.setTextColor(connected ? COLOR_WIFI_ON : COLOR_WIFI_OFF);
        
        if (connected) {
            sprite.drawString(wifiManager->getSSID().c_str(), 70, y, 2);
            y += 18;
            sprite.setTextColor(COLOR_TEXT);
            sprite.drawString("IP:", 10, y, 2);
            sprite.drawString(wifiManager->getIP().toString().c_str(), 70, y, 2);
        } else if (wifiManager->isAPMode()) {
            sprite.setTextColor(COLOR_TEMP_WARN);
            sprite.drawString("AP Mode", 70, y, 2);
            y += 18;
            sprite.setTextColor(COLOR_TEXT);
            sprite.drawString("SSID:", 10, y, 2);
            sprite.drawString(AP_SSID, 70, y, 2);
        } else {
            sprite.drawString("Disconnected", 70, y, 2);
        }
    } else {
        sprite.setTextColor(COLOR_WIFI_OFF);
        sprite.drawString("N/A", 70, y, 2);
    }
    
    y += 25;
    
    // MQTT status
    sprite.setTextColor(COLOR_TEXT);
    sprite.drawString("MQTT:", 10, y, 2);
    
    if (mqttClient != nullptr) {
        bool connected = mqttClient->isConnected();
        sprite.setTextColor(connected ? COLOR_MQTT_ON : COLOR_MQTT_OFF);
        sprite.drawString(connected ? "Connected" : "Disconnected", 70, y, 2);
    } else {
        sprite.setTextColor(COLOR_MQTT_OFF);
        sprite.drawString("N/A", 70, y, 2);
    }
    
    y += 25;
    
    // Sensor count
    sprite.setTextColor(COLOR_TEXT);
    sprite.drawString("Sensors:", 10, y, 2);
    
    if (sensorManager != nullptr) {
        char countStr[10];
        snprintf(countStr, sizeof(countStr), "%d", sensorManager->getSensorCount());
        sprite.setTextColor(COLOR_HEADER);
        sprite.drawString(countStr, 90, y, 2);
    }
}

void DisplayManager::drawAlertsPage() {
    drawHeader("Alerts");
    
    if (sensorManager == nullptr) {
        sprite.setTextColor(COLOR_TEXT);
        sprite.drawString("No sensor manager", 10, 40, 2);
        return;
    }
    
    uint8_t alertCount = 0;
    uint8_t y = 28;
    
    for (uint8_t i = 0; i < sensorManager->getSensorCount() && y < 110; i++) {
        const SensorData* data = sensorManager->getSensorData(i);
        if (!data) continue;
        
        if (data->alarmState != AlarmState::NORMAL) {
            alertCount++;
            
            const SensorConfig* config = configManager.getSensorConfigByAddress(data->addressStr);
            String name = config ? String(config->name) : String("Sensor ") + String(i + 1);
            if (name.length() > 10) name = name.substring(0, 9) + ".";
            
            // Color based on alarm type
            uint16_t color;
            const char* state;
            switch (data->alarmState) {
                case AlarmState::BELOW_LOW:
                    color = COLOR_TEMP_COLD;
                    state = "LOW";
                    break;
                case AlarmState::ABOVE_HIGH:
                    color = COLOR_TEMP_ALERT;
                    state = "HIGH";
                    break;
                case AlarmState::SENSOR_ERROR:
                    color = COLOR_TEMP_ALERT;
                    state = "ERR";
                    break;
                default:
                    color = COLOR_TEXT;
                    state = "?";
            }
            
            sprite.setTextColor(color);
            sprite.drawString(name, 5, y, 2);
            
            char tempStr[20];
            if (data->temperature > TEMP_INVALID + 1) {
                snprintf(tempStr, sizeof(tempStr), "%.1fC %s", data->temperature, state);
            } else {
                snprintf(tempStr, sizeof(tempStr), "--- %s", state);
            }
            
            sprite.setTextDatum(TR_DATUM);
            sprite.drawString(tempStr, DISPLAY_WIDTH - 5, y, 2);
            sprite.setTextDatum(TL_DATUM);
            
            y += 20;
        }
    }
    
    if (alertCount == 0) {
        sprite.setTextColor(COLOR_TEMP_OK);
        sprite.setTextDatum(MC_DATUM);
        sprite.drawString("All OK", DISPLAY_WIDTH/2, DISPLAY_HEIGHT/2, 4);
        sprite.setTextDatum(TL_DATUM);
    }
}

void DisplayManager::drawInfoPage() {
    drawHeader("Device Info");
    
    uint8_t y = 28;
    const SystemConfig& sysConfig = configManager.getSystemConfig();
    
    sprite.setTextColor(COLOR_TEXT);
    
    // Device name
    sprite.drawString("Name:", 10, y, 2);
    sprite.setTextColor(COLOR_HEADER);
    sprite.drawString(sysConfig.deviceName, 70, y, 2);
    y += 20;
    
    // Firmware version
    sprite.setTextColor(COLOR_TEXT);
    sprite.drawString("FW:", 10, y, 2);
    sprite.drawString(FIRMWARE_VERSION, 70, y, 2);
    y += 20;
    
    // Free heap
    sprite.drawString("Heap:", 10, y, 2);
    char heapStr[15];
    snprintf(heapStr, sizeof(heapStr), "%d KB", ESP.getFreeHeap() / 1024);
    sprite.drawString(heapStr, 70, y, 2);
    y += 20;
    
    // Uptime
    sprite.drawString("Up:", 10, y, 2);
    uint32_t uptime = millis() / 1000;
    uint32_t hours = uptime / 3600;
    uint32_t mins = (uptime % 3600) / 60;
    char uptimeStr[15];
    snprintf(uptimeStr, sizeof(uptimeStr), "%dh %dm", hours, mins);
    sprite.drawString(uptimeStr, 70, y, 2);
}

uint16_t DisplayManager::getTemperatureColor(float temp, float low, float high) {
    if (temp < low) {
        return COLOR_TEMP_COLD;
    } else if (temp > high) {
        return COLOR_TEMP_ALERT;
    } else if (temp > high - 5.0f || temp < low + 5.0f) {
        return COLOR_TEMP_WARN;
    }
    return COLOR_TEMP_OK;
}

#endif // USE_DISPLAY
