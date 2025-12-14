/*
 * ESP32 Temperature Monitoring System
 * Configuration and Constants
 * 
 * This file contains all hardware configurations, pin mappings,
 * and system constants used throughout the application.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================================================
// Debug Configuration
// ============================================================================

// DEBUG_SERIAL is set via build_flags in platformio.ini
// Set to 1 for debug builds, 0 for release builds
#ifndef DEBUG_SERIAL
#define DEBUG_SERIAL 1
#endif

#if DEBUG_SERIAL
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(...)
#endif

// ============================================================================
// Hardware Configuration
// ============================================================================

// DS18B20 OneWire bus pin
// Note: GPIO4 is used for TFT backlight on TTGO T-Display, use GPIO27 instead
constexpr uint8_t ONEWIRE_PIN = 27;  // GPIO27 - Connect all DS18B20 data pins here

// Status LED pin (not available on TTGO T-Display, display used instead)
constexpr uint8_t LED_PIN = 2;

// TTGO T-Display button pins
constexpr uint8_t BUTTON_1_PIN = 35;  // Top button
constexpr uint8_t BUTTON_2_PIN = 0;   // Bottom button

// ============================================================================
// Sensor Configuration
// ============================================================================

// Maximum number of sensors supported
constexpr uint8_t MAX_SENSORS = 10;

// Sensor name maximum length
constexpr uint8_t SENSOR_NAME_MAX_LEN = 32;

// Sensor address string length (16 hex chars + null terminator)
constexpr uint8_t SENSOR_ADDR_STR_LEN = 17;

// Temperature reading resolution (9-12 bits)
// 12-bit = 0.0625°C resolution, ~750ms conversion time
constexpr uint8_t SENSOR_RESOLUTION = 12;

// Invalid temperature marker
constexpr float TEMP_INVALID = -127.0f;

// ============================================================================
// WiFi Configuration
// ============================================================================

// Access Point settings (when no WiFi available)
constexpr char AP_SSID[] = "TempMonitor-Setup";
constexpr char AP_PASSWORD[] = "tempmonitor123";
constexpr uint8_t AP_CHANNEL = 1;
constexpr uint8_t AP_MAX_CONNECTIONS = 4;

// WiFi connection timeout (ms)
constexpr uint32_t WIFI_CONNECT_TIMEOUT = 15000;

// WiFi retry interval (ms)
constexpr uint32_t WIFI_RETRY_INTERVAL = 30000;

// ============================================================================
// MQTT Configuration
// ============================================================================

// Default MQTT port
constexpr uint16_t MQTT_DEFAULT_PORT = 1883;

// MQTT topic base
constexpr char MQTT_TOPIC_BASE[] = "tempmonitor";

// MQTT client ID prefix
constexpr char MQTT_CLIENT_PREFIX[] = "esp32-temp-";

// MQTT reconnect interval (ms)
constexpr uint32_t MQTT_RECONNECT_INTERVAL = 5000;

// MQTT keep alive (seconds)
constexpr uint16_t MQTT_KEEP_ALIVE = 60;

// ============================================================================
// Web Server Configuration
// ============================================================================

// Web server port
constexpr uint16_t WEB_SERVER_PORT = 80;

// ============================================================================
// Timing Configuration
// ============================================================================

// Temperature reading interval (ms)
constexpr uint32_t TEMP_READ_INTERVAL = 2000;

// Sensor discovery interval (ms) - check for new/removed sensors
constexpr uint32_t SENSOR_DISCOVERY_INTERVAL = 60000;

// MQTT publish interval (ms)
constexpr uint32_t MQTT_PUBLISH_INTERVAL = 10000;

// Status LED blink interval (ms)
constexpr uint32_t LED_BLINK_INTERVAL = 1000;

// Configuration save debounce (ms)
constexpr uint32_t CONFIG_SAVE_DEBOUNCE = 5000;

// ============================================================================
// History Configuration
// ============================================================================

// Number of historical temperature readings to store per sensor
constexpr uint16_t TEMP_HISTORY_SIZE = 30;  // 30 readings = ~1 minute at 2s interval (saves ~1.2KB/sensor)

// ============================================================================
// Threshold Configuration
// ============================================================================

// Default threshold values
constexpr float DEFAULT_THRESHOLD_LOW = 10.0f;
constexpr float DEFAULT_THRESHOLD_HIGH = 80.0f;

// Threshold hysteresis to prevent rapid alarm toggling
constexpr float THRESHOLD_HYSTERESIS = 1.0f;

// ============================================================================
// File Paths (SPIFFS)
// ============================================================================

constexpr char CONFIG_FILE_PATH[] = "/config.json";

// ============================================================================
// NTP Configuration
// ============================================================================

constexpr char NTP_SERVER[] = "pool.ntp.org";
constexpr long NTP_UTC_OFFSET = 0;  // UTC offset in seconds

// ============================================================================
// Version Information
// ============================================================================

// NOTE: For releases, CI can override this via -DFW_VERSION=vX.Y.Z
#ifndef FW_VERSION
#define FW_VERSION v1.0.0
#endif

#define FW_VERSION_STR_HELPER(x) #x
#define FW_VERSION_STR(x) FW_VERSION_STR_HELPER(x)

constexpr char FIRMWARE_VERSION[] = FW_VERSION_STR(FW_VERSION);
constexpr char DEVICE_NAME[] = "ESP32 Temperature Monitor";

// ============================================================================
// GitHub OTA (Releases)
// ============================================================================

constexpr char GITHUB_OWNER[] = "martiera";
constexpr char GITHUB_REPO[] = "probe-station-esp32";

#endif // CONFIG_H
