/*
 * Display Manager for TTGO T-Display
 * Handles TFT display output for temperature readings
 */

#include "display_manager.h"
#include "sensor_manager.h"
#include "wifi_manager.h"
#include "mqtt_client.h"
#include <WiFi.h>

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
    tft.init();
    tft.setRotation(1);  // Landscape mode
    tft.fillScreen(COLOR_BG);
    
    // Initialize sprite for flicker-free updates
    sprite.createSprite(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    sprite.setTextDatum(TL_DATUM);
    
    // Set backlight pin
    pinMode(TFT_BL, OUTPUT);
    setBrightness(brightness);
    
    // Show boot screen
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_CYAN, COLOR_BG);
    tft.drawString("Probe Station", DISPLAY_WIDTH/2, DISPLAY_HEIGHT/2 - 20, 4);
    tft.setTextColor(TFT_WHITE, COLOR_BG);
    tft.drawString("Initializing...", DISPLAY_WIDTH/2, DISPLAY_HEIGHT/2 + 20, 2);
#endif
}

void DisplayManager::setOtaMode(bool enabled) {
#ifdef USE_DISPLAY
    otaMode = enabled;
    if (enabled) {
        // Free the sprite buffer to save ~65KB RAM
        sprite.deleteSprite();
        
        // Show OTA message on display
        tft.fillScreen(COLOR_BG);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_YELLOW, COLOR_BG);
        tft.drawString("OTA Update", DISPLAY_WIDTH/2, DISPLAY_HEIGHT/2 - 20, 4);
        tft.setTextColor(TFT_WHITE, COLOR_BG);
        tft.drawString("Please wait...", DISPLAY_WIDTH/2, DISPLAY_HEIGHT/2 + 20, 2);
        
        Serial.printf("[Display] OTA mode enabled, sprite freed. Heap: %u\n", ESP.getFreeHeap());
    } else {
        // Recreate sprite
        sprite.createSprite(DISPLAY_WIDTH, DISPLAY_HEIGHT);
        sprite.setTextDatum(TL_DATUM);
        needsRefresh = true;
        
        Serial.printf("[Display] OTA mode disabled, sprite restored. Heap: %u\n", ESP.getFreeHeap());
    }
#endif
}

void DisplayManager::update() {
#ifdef USE_DISPLAY
    // Skip updates during OTA
    if (otaMode) {
        return;
    }
    
    uint32_t now = millis();
    
    // Auto-rotate in focus mode
    if (autoRotate && currentPage == DisplayPage::FOCUS && sensorManager != nullptr) {
        if (now - lastAutoRotate >= AUTO_ROTATE_INTERVAL) {
            uint8_t count = sensorManager->getSensorCount();
            if (count > 0) {
                focusSensorIndex = (focusSensorIndex + 1) % count;
                needsRefresh = true;
            }
            lastAutoRotate = now;
        }
    }
    
    // Update display at interval
    if (needsRefresh || (now - lastUpdate >= UPDATE_INTERVAL)) {
        sprite.fillSprite(COLOR_BG);
        
        drawStatusBar();
        
        switch (currentPage) {
            case DisplayPage::FOCUS:
                drawFocusPage();
                break;
            case DisplayPage::SENSORS:
                drawSensorsPage();
                break;
            case DisplayPage::STATUS:
                drawStatusPage();
                break;
            case DisplayPage::ALERTS:
                drawAlertsPage();
                break;
        }
        
        drawFooter();
        
        // Push sprite to display
        sprite.pushSprite(0, 0);
        
        lastUpdate = now;
        needsRefresh = false;
    }
#endif
}

void DisplayManager::nextPage() {
    uint32_t now = millis();
    if (now - lastButtonPress < BUTTON_DEBOUNCE) return;
    lastButtonPress = now;
    
    switch (currentPage) {
        case DisplayPage::FOCUS:
            currentPage = DisplayPage::SENSORS;
            sensorPageOffset = 0;
            break;
        case DisplayPage::SENSORS:
            currentPage = DisplayPage::STATUS;
            break;
        case DisplayPage::STATUS:
            currentPage = DisplayPage::ALERTS;
            break;
        case DisplayPage::ALERTS:
            currentPage = DisplayPage::FOCUS;
            break;
    }
    needsRefresh = true;
}

void DisplayManager::previousPage() {
    uint32_t now = millis();
    if (now - lastButtonPress < BUTTON_DEBOUNCE) return;
    lastButtonPress = now;
    
    switch (currentPage) {
        case DisplayPage::FOCUS:
            currentPage = DisplayPage::ALERTS;
            break;
        case DisplayPage::SENSORS:
            currentPage = DisplayPage::FOCUS;
            break;
        case DisplayPage::STATUS:
            currentPage = DisplayPage::SENSORS;
            sensorPageOffset = 0;
            break;
        case DisplayPage::ALERTS:
            currentPage = DisplayPage::STATUS;
            break;
    }
    needsRefresh = true;
}

void DisplayManager::nextSensorPage() {
    uint32_t now = millis();
    if (now - lastButtonPress < BUTTON_DEBOUNCE) return;
    lastButtonPress = now;
    
    if (sensorManager == nullptr) return;
    
    if (currentPage == DisplayPage::FOCUS) {
        // Cycle through sensors in focus mode
        uint8_t count = sensorManager->getSensorCount();
        if (count > 0) {
            focusSensorIndex = (focusSensorIndex + 1) % count;
            lastAutoRotate = millis();  // Reset auto-rotate timer
        }
    } else if (currentPage == DisplayPage::SENSORS) {
        // Scroll through sensor pages
        uint8_t count = sensorManager->getSensorCount();
        sensorPageOffset += SENSORS_PER_PAGE;
        if (sensorPageOffset >= count) {
            sensorPageOffset = 0;
        }
    }
    needsRefresh = true;
}

void DisplayManager::refresh() {
    needsRefresh = true;
}

void DisplayManager::setBrightness(uint8_t level) {
    brightness = level;
#ifdef USE_DISPLAY
    analogWrite(TFT_BL, brightness);
#endif
}

void DisplayManager::handleButton1() {
    // Top button short press - next sensor/scroll
    nextSensorPage();
}

void DisplayManager::handleButton1LongPress() {
    // Top button long press - toggle auto-rotate (only on FOCUS page)
    if (currentPage == DisplayPage::FOCUS) {
        toggleAutoRotate();
    }
}

void DisplayManager::handleButton2() {
    // Bottom button - next page
    nextPage();
}

void DisplayManager::drawStatusBar() {
#ifdef USE_DISPLAY
    // Top status bar (20px high)
    const int16_t barHeight = 20;
    
    // Determine status bar color based on alarm state
    uint16_t barColor = COLOR_HEADER;
    if (sensorManager != nullptr) {
        // Check for any alarms
        for (uint8_t i = 0; i < sensorManager->getSensorCount(); i++) {
            const SensorData* sensor = sensorManager->getSensorData(i);
            if (sensor != nullptr) {
                if (sensor->alarmState == AlarmState::ABOVE_HIGH) {
                    barColor = COLOR_TEMP_ALERT;
                    break;
                } else if (sensor->alarmState == AlarmState::BELOW_LOW) {
                    barColor = COLOR_TEMP_COLD;
                    break;
                } else if (sensor->alarmState == AlarmState::SENSOR_ERROR) {
                    barColor = COLOR_TEMP_WARN;
                }
            }
        }
    }
    
    // Draw top bar
    sprite.fillRect(0, 0, DISPLAY_WIDTH, barHeight, barColor);
    
    // WiFi indicator (left)
    sprite.setTextDatum(ML_DATUM);
    sprite.setTextColor(COLOR_TEXT, barColor);
    
    if (WiFi.status() == WL_CONNECTED) {
        int8_t rssi = WiFi.RSSI();
        const char* wifiIcon = rssi > -50 ? "WiFi" : rssi > -70 ? "WiFi" : "WiFi!";
        sprite.drawString(wifiIcon, 4, barHeight/2, 2);
    } else {
        sprite.drawString("AP", 4, barHeight/2, 2);
    }
    
    // Page name (center) - show AUTO/MAN for FOCUS page
    sprite.setTextDatum(MC_DATUM);
    const char* pageName = "";
    switch (currentPage) {
        case DisplayPage::FOCUS:   pageName = autoRotate ? "FOCUS-A" : "FOCUS-M"; break;
        case DisplayPage::SENSORS: pageName = "SENSORS"; break;
        case DisplayPage::STATUS:  pageName = "STATUS"; break;
        case DisplayPage::ALERTS:  pageName = "ALERTS"; break;
    }
    sprite.drawString(pageName, DISPLAY_WIDTH/2, barHeight/2, 2);
    
    // BTN1 action (right) - shown as button style
    sprite.setTextDatum(MR_DATUM);
    const char* btn1Text = "";
    switch (currentPage) {
        case DisplayPage::FOCUS:
            btn1Text = "[SENSOR]";
            break;
        case DisplayPage::SENSORS:
            btn1Text = "[SCROLL]";
            break;
        default:
            btn1Text = "";
            break;
    }
    if (btn1Text[0] != '\0') {
        sprite.drawString(btn1Text, DISPLAY_WIDTH - 4, barHeight/2, 2);
    }
#endif
}

void DisplayManager::drawFooter() {
#ifdef USE_DISPLAY
    // Minimal footer - just navigation hint and page dots
    const int16_t footerY = DISPLAY_HEIGHT - 16;

    // Firmware version (left bottom)
    sprite.setTextDatum(ML_DATUM);
    sprite.setTextColor(COLOR_GRAY, COLOR_BG);
    sprite.drawString(FIRMWARE_VERSION, 4, footerY, 2);
    
    // Page indicator dots (center bottom) ● ○ ○ ○
    sprite.setTextDatum(MC_DATUM);
    sprite.setTextColor(COLOR_GRAY, COLOR_BG);
    
    char dots[16];
    uint8_t pageIdx = static_cast<uint8_t>(currentPage);
    snprintf(dots, sizeof(dots), "%c %c %c %c",
        pageIdx == 0 ? 'O' : 'o',
        pageIdx == 1 ? 'O' : 'o',
        pageIdx == 2 ? 'O' : 'o',
        pageIdx == 3 ? 'O' : 'o');
    sprite.drawString(dots, DISPLAY_WIDTH/2, footerY, 2);
    
    // Navigation arrow (right bottom)
    sprite.setTextDatum(MR_DATUM);
    sprite.setTextColor(COLOR_TEXT, COLOR_BG);
    sprite.drawString(">>", DISPLAY_WIDTH - 4, footerY, 2);
#endif
}

void DisplayManager::drawFocusPage() {
#ifdef USE_DISPLAY
    if (sensorManager == nullptr || sensorManager->getSensorCount() == 0) {
        sprite.setTextDatum(MC_DATUM);
        sprite.setTextColor(TFT_YELLOW, COLOR_BG);
        sprite.drawString("No Sensors", DISPLAY_WIDTH/2, DISPLAY_HEIGHT/2, 4);
        return;
    }
    
    uint8_t count = sensorManager->getSensorCount();
    if (focusSensorIndex >= count) {
        focusSensorIndex = 0;
    }
    
    const SensorData* sensor = sensorManager->getSensorData(focusSensorIndex);
    if (sensor == nullptr) return;
    
    // Get sensor config for name and thresholds
    String sensorName = String("Sensor ") + String(focusSensorIndex + 1);
    float lowThreshold = DEFAULT_THRESHOLD_LOW;
    float highThreshold = DEFAULT_THRESHOLD_HIGH;
    
    const SensorConfig* cfg = configManager.getSensorConfigByAddress(sensor->addressStr);
    if (cfg != nullptr) {
        sensorName = cfg->name;
        lowThreshold = cfg->thresholdLow;
        highThreshold = cfg->thresholdHigh;
    }
    
    // Sensor name (top, medium font)
    sprite.setTextDatum(TC_DATUM);
    sprite.setTextColor(TFT_CYAN, COLOR_BG);
    sprite.drawString(sensorName.c_str(), DISPLAY_WIDTH/2, 24, 2);
    
    // Temperature (center, BIG font)
    sprite.setTextDatum(MC_DATUM);
    
    if (!sensor->connected) {
        sprite.setTextColor(COLOR_TEMP_ALERT, COLOR_BG);
        sprite.drawString("ERROR", DISPLAY_WIDTH/2, DISPLAY_HEIGHT/2 + 5, 4);
    } else {
        uint16_t tempColor = getTemperatureColor(sensor->temperature, lowThreshold, highThreshold);
        sprite.setTextColor(tempColor, COLOR_BG);
        
        // Draw temperature with Font 6 (48px numbers)
        char tempStr[16];
        snprintf(tempStr, sizeof(tempStr), "%.1f", sensor->temperature);
        sprite.drawString(tempStr, DISPLAY_WIDTH/2 - 15, DISPLAY_HEIGHT/2 + 5, 6);
        
        // Draw °C with smaller font
        sprite.setTextDatum(ML_DATUM);
        sprite.drawString("C", DISPLAY_WIDTH/2 + 55, DISPLAY_HEIGHT/2 + 5, 4);
    }
    
    // Sensor index indicator - position above bottom bar
    sprite.setTextDatum(MC_DATUM);
    sprite.setTextColor(COLOR_GRAY, COLOR_BG);
    char idxStr[16];
    snprintf(idxStr, sizeof(idxStr), "< %d/%d >", focusSensorIndex + 1, count);
    sprite.drawString(idxStr, DISPLAY_WIDTH/2, DISPLAY_HEIGHT - 28, 2);
#endif
}

void DisplayManager::drawSensorsPage() {
#ifdef USE_DISPLAY
    if (sensorManager == nullptr || sensorManager->getSensorCount() == 0) {
        sprite.setTextDatum(MC_DATUM);
        sprite.setTextColor(TFT_YELLOW, COLOR_BG);
        sprite.drawString("No Sensors", DISPLAY_WIDTH/2, DISPLAY_HEIGHT/2, 4);
        return;
    }
    
    uint8_t count = sensorManager->getSensorCount();
    uint8_t totalPages = (count + SENSORS_PER_PAGE - 1) / SENSORS_PER_PAGE;
    uint8_t currentPageNum = sensorPageOffset / SENSORS_PER_PAGE;
    
    // Content area: 20px to 115px (95px total for 2 sensors)
    int16_t contentY = 24;
    int16_t rowHeight = 45;  // Bigger rows for 2 sensors
    
    for (uint8_t i = 0; i < SENSORS_PER_PAGE && (sensorPageOffset + i) < count; i++) {
        const SensorData* sensor = sensorManager->getSensorData(sensorPageOffset + i);
        if (sensor == nullptr) continue;
        
        int16_t y = contentY + (i * rowHeight);
        
        // Get sensor config
        String sensorName = String("Sensor ") + String(sensorPageOffset + i + 1);
        float lowThreshold = DEFAULT_THRESHOLD_LOW;
        float highThreshold = DEFAULT_THRESHOLD_HIGH;
        
        const SensorConfig* cfg = configManager.getSensorConfigByAddress(sensor->addressStr);
        if (cfg != nullptr) {
            sensorName = cfg->name;
            lowThreshold = cfg->thresholdLow;
            highThreshold = cfg->thresholdHigh;
        }
        
        // Sensor name (left, truncated if needed)
        sprite.setTextDatum(TL_DATUM);
        sprite.setTextColor(TFT_CYAN, COLOR_BG);
        
        String displayName = sensorName;
        if (displayName.length() > 12) {
            displayName = displayName.substring(0, 10) + "..";
        }
        sprite.drawString(displayName.c_str(), 4, y, 2);
        
        // Temperature (right, large font 4)
        sprite.setTextDatum(TR_DATUM);
        
        if (!sensor->connected) {
            sprite.setTextColor(COLOR_TEMP_ALERT, COLOR_BG);
            sprite.drawString("ERR", DISPLAY_WIDTH - 4, y, 4);
        } else {
            uint16_t tempColor = getTemperatureColor(sensor->temperature, lowThreshold, highThreshold);
            sprite.setTextColor(tempColor, COLOR_BG);
            
            char tempStr[16];
            snprintf(tempStr, sizeof(tempStr), "%.1fC", sensor->temperature);
            sprite.drawString(tempStr, DISPLAY_WIDTH - 4, y, 4);
        }
        
        // Separator line
        if (i < SENSORS_PER_PAGE - 1 && (sensorPageOffset + i + 1) < count) {
            sprite.drawLine(4, y + rowHeight - 4, DISPLAY_WIDTH - 4, y + rowHeight - 4, COLOR_GRAY);
        }
    }
    
    // Page indicator - position above bottom bar
    sprite.setTextDatum(MC_DATUM);
    sprite.setTextColor(COLOR_GRAY, COLOR_BG);
    char pageStr[16];
    snprintf(pageStr, sizeof(pageStr), "%d/%d", currentPageNum + 1, totalPages);
    sprite.drawString(pageStr, DISPLAY_WIDTH/2, DISPLAY_HEIGHT - 28, 2);
#endif
}

void DisplayManager::drawStatusPage() {
#ifdef USE_DISPLAY
    int16_t y = 26;
    int16_t lineHeight = 32;
    
    // WiFi Status - IP with bigger font
    sprite.setTextDatum(TL_DATUM);
    sprite.setTextColor(COLOR_TEXT, COLOR_BG);
    sprite.drawString("WiFi:", 8, y, 2);
    
    sprite.setTextDatum(TR_DATUM);
    if (WiFi.status() == WL_CONNECTED) {
        sprite.setTextColor(COLOR_WIFI_ON, COLOR_BG);
        sprite.drawString(WiFi.localIP().toString().c_str(), DISPLAY_WIDTH - 8, y, 4);
    } else {
        sprite.setTextColor(COLOR_WIFI_OFF, COLOR_BG);
        sprite.drawString("192.168.4.1", DISPLAY_WIDTH - 8, y, 4);
    }
    y += lineHeight;
    
    // MQTT Status
    sprite.setTextDatum(TL_DATUM);
    sprite.setTextColor(COLOR_TEXT, COLOR_BG);
    sprite.drawString("MQTT:", 8, y, 2);
    
    sprite.setTextDatum(TR_DATUM);
    if (mqttClient != nullptr && mqttClient->isConnected()) {
        sprite.setTextColor(COLOR_MQTT_ON, COLOR_BG);
        sprite.drawString("Connected", DISPLAY_WIDTH - 8, y, 2);
    } else {
        sprite.setTextColor(COLOR_GRAY, COLOR_BG);
        sprite.drawString("Disconnected", DISPLAY_WIDTH - 8, y, 2);
    }
    y += lineHeight;
    
    // Uptime
    sprite.setTextDatum(TL_DATUM);
    sprite.setTextColor(COLOR_TEXT, COLOR_BG);
    sprite.drawString("Up:", 8, y, 2);
    
    sprite.setTextDatum(TR_DATUM);
    uint32_t uptime = millis() / 1000;
    uint16_t days = uptime / 86400;
    uint8_t hours = (uptime % 86400) / 3600;
    uint8_t mins = (uptime % 3600) / 60;
    
    char uptimeStr[24];
    if (days > 0) {
        snprintf(uptimeStr, sizeof(uptimeStr), "%dd %02dh %02dm", days, hours, mins);
    } else {
        snprintf(uptimeStr, sizeof(uptimeStr), "%02dh %02dm", hours, mins);
    }
    sprite.setTextColor(COLOR_TEMP_OK, COLOR_BG);
    sprite.drawString(uptimeStr, DISPLAY_WIDTH - 8, y, 2);
#endif
}

void DisplayManager::drawAlertsPage() {
#ifdef USE_DISPLAY
    if (sensorManager == nullptr) {
        sprite.setTextDatum(MC_DATUM);
        sprite.setTextColor(COLOR_GRAY, COLOR_BG);
        sprite.drawString("No Data", DISPLAY_WIDTH/2, DISPLAY_HEIGHT/2, 2);
        return;
    }
    
    // Count alerts
    uint8_t alertCount = 0;
    for (uint8_t i = 0; i < sensorManager->getSensorCount(); i++) {
        const SensorData* sensor = sensorManager->getSensorData(i);
        if (sensor != nullptr && sensor->alarmState != AlarmState::NORMAL) {
            alertCount++;
        }
    }
    
    if (alertCount == 0) {
        sprite.setTextDatum(MC_DATUM);
        sprite.setTextColor(COLOR_TEMP_OK, COLOR_BG);
        sprite.drawString("All Normal", DISPLAY_WIDTH/2, DISPLAY_HEIGHT/2, 4);
        return;
    }
    
    // Show alerts
    int16_t y = 26;
    int16_t lineHeight = 28;
    uint8_t shown = 0;
    
    for (uint8_t i = 0; i < sensorManager->getSensorCount() && shown < 3; i++) {
        const SensorData* sensor = sensorManager->getSensorData(i);
        if (sensor == nullptr || sensor->alarmState == AlarmState::NORMAL) continue;
        
        // Get sensor name
        String sensorName = String("Sensor ") + String(i + 1);
        const SensorConfig* cfg = configManager.getSensorConfigByAddress(sensor->addressStr);
        if (cfg != nullptr) {
            sensorName = cfg->name;
        }
        
        // Truncate name
        if (sensorName.length() > 10) {
            sensorName = sensorName.substring(0, 8) + "..";
        }
        
        // Draw alert
        sprite.setTextDatum(TL_DATUM);
        uint16_t alertColor = getAlarmColor(sensor->alarmState);
        sprite.setTextColor(alertColor, COLOR_BG);
        sprite.drawString(sensorName.c_str(), 8, y, 2);
        
        sprite.setTextDatum(TR_DATUM);
        const char* alertText = "";
        switch (sensor->alarmState) {
            case AlarmState::ABOVE_HIGH: alertText = "HIGH!"; break;
            case AlarmState::BELOW_LOW: alertText = "LOW!"; break;
            case AlarmState::SENSOR_ERROR: alertText = "ERROR"; break;
            default: alertText = "???"; break;
        }
        sprite.drawString(alertText, DISPLAY_WIDTH - 8, y, 2);
        
        y += lineHeight;
        shown++;
    }
    
    // Show count if more alerts - position above bottom bar
    if (alertCount > 3) {
        sprite.setTextDatum(MC_DATUM);
        sprite.setTextColor(COLOR_TEMP_WARN, COLOR_BG);
        char moreStr[16];
        snprintf(moreStr, sizeof(moreStr), "+%d more", alertCount - 3);
        sprite.drawString(moreStr, DISPLAY_WIDTH/2, DISPLAY_HEIGHT - 28, 2);
    }
#endif
}

uint16_t DisplayManager::getTemperatureColor(float temp, float low, float high) {
    if (temp < low) return COLOR_TEMP_COLD;
    if (temp > high) return COLOR_TEMP_ALERT;
    if (temp > high - 5) return COLOR_TEMP_WARN;  // Warning zone 5° before high
    return COLOR_TEMP_OK;
}

uint16_t DisplayManager::getAlarmColor(AlarmState state) {
    switch (state) {
        case AlarmState::ABOVE_HIGH: return COLOR_TEMP_ALERT;
        case AlarmState::BELOW_LOW: return COLOR_TEMP_COLD;
        case AlarmState::SENSOR_ERROR: return COLOR_TEMP_WARN;
        default: return COLOR_TEMP_OK;
    }
}
