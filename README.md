# ESP32 Temperature Monitoring System

## 🚦 Quick Start

1. **Power on the device** (connect USB or power supply)
2. **Connect your phone or PC to the WiFi AP**:
   - SSID: `TempMonitor-XXXXXX` (no password)
3. **Open a browser** (on the same device) and go to: [http://192.168.4.1](http://192.168.4.1)
   - You will be automatically redirected to the WiFi settings page
4. **Connect the device to your home/office WiFi**:
   - Enter your WiFi credentials and save
   - The device will reboot and join your network
5. **Find the device's new IP address**:
   - Check the TFT screen (STATUS page) for the current IP
   - Or check your router's connected devices
6. **Access the web dashboard**:
   - Open a browser and go to the device's IP address (shown on the screen)

**Tip:** You can always reset WiFi by holding the reset button or reflashing if needed.

A professional-grade temperature monitoring solution for heating and hot water pipe monitoring using TTGO T-Display (ESP32 with TFT) and DS18B20 sensors.

## 🌟 Features

### Core Functionality
- **Multi-sensor Support**: Connect 5-10 DS18B20 temperature sensors on a single bus
- **Real-time Monitoring**: Live temperature updates via WebSocket
- **TFT Display**: Built-in 1.14" color display showing temperatures, status, and alerts
- **Web Dashboard**: Modern, responsive interface accessible from any device
- **Pinned Sensor**: Pin your most important sensor for quick viewing on dashboard
- **Min/Max Display**: Dashboard shows coldest and hottest sensors with names
- **Temperature History**: Track temperature trends over time

### TFT Display Interface

The 1.14" TFT display provides a clean, readable interface with 4 pages:

| Page | Description | BTN1 Action |
|------|-------------|-------------|
| **FOCUS** | Single sensor with large temperature (48px) | `[SENSOR]` - Next sensor |
| **SENSORS** | List view showing 2 sensors per screen | `[SCROLL]` - Scroll list |
| **STATUS** | WiFi IP, MQTT status, uptime | - |
| **ALERTS** | Active alarms or "All Normal" | - |

**Button Controls:**
- **BTN1 (Top)**: Short press = action shown in top-right bracket
- **BTN1 Long Press**: Toggle AUTO/MANUAL mode on FOCUS page
- **BTN2 (Bottom)**: Next page (indicated by `>>` symbol)

**Status Bar (Top):**
- Left: WiFi status ("WiFi" or "AP")
- Center: Page name (FOCUS-A/FOCUS-M, SENSORS, STATUS, ALERTS)
- Right: Context action `[SENSOR]` or `[SCROLL]`
- Bar color: Green=normal, Red=high alarm, Blue=low alarm, Orange=sensor error

**Footer:**
- Center: Page indicator dots (● ○ ○ ○)
- Right: Navigation arrow `>>`

**FOCUS Page Modes:**
- **AUTO (FOCUS-A)**: Automatically cycles through sensors every 4 seconds
- **MANUAL (FOCUS-M)**: Stays on selected sensor until you press BTN1

### Connectivity
- **WiFi Manager**: Auto-connects to configured network or creates an **open Access Point (no password)** for setup
- **Captive Portal**: Any device connecting to AP is automatically redirected to the WiFi settings page
- **Async WiFi Scanning**: Non-blocking network scanning prevents device reboots
- **MQTT Integration**: Publish temperatures and alarms to any MQTT broker
- **Home Assistant Auto-Discovery**: Sensors automatically appear in Home Assistant
- **OTA Updates**: Update firmware wirelessly (OTA manager is skipped in AP mode to save memory)

### Web Dashboard (Modern UI)
- **Responsive, mobile-friendly interface**
- **Update Banner**: Notifies when a new firmware version is available; auto-hides when up-to-date
- **Tab Navigation**: Dashboard, Sensors, Settings (auto-expands WiFi config if redirected from captive portal)
- **Real-time Data**: Live temperature, alarms, and status via WebSocket
- **Sensor Management**: Name, calibrate, reorder, and set thresholds for each sensor
- **WiFi Setup**: Scan and connect to WiFi, supports static IP
- **MQTT Setup**: Enable/disable, set broker, topic, and credentials
- **OTA**: One-click firmware update from web UI (when not in AP mode)
- **Calibration**: Calibrate all or individual sensors from the web
- **Alarm Notifications**: Banner and color-coded status for alarms

### TFT Display Interface (TTGO T-Display)
- **4 Pages**: FOCUS (auto/manual), SENSORS (list), STATUS, ALERTS
- **Button Controls**:
  - BTN1 (Top): Next sensor or action (short/long press for auto/manual)
  - BTN2 (Bottom): Next page
- **Status Bar**: Shows WiFi (STA/AP), page, and context action
- **Footer**: Page indicator dots and navigation arrow
- **Color Logic**:
  - Green: Normal
  - Red: High alarm
  - Blue: Low alarm
  - Orange: Sensor error
- **FOCUS Page**: Auto-cycles sensors unless in manual mode
- **ALERTS Page**: Shows all active alarms or "All Normal"

### WiFi/AP Setup (Updated)
- On first boot or if WiFi fails, device creates an **open AP** (SSID: `TempMonitor-XXXXXX`, no password)
- Captive portal auto-redirects to WiFi settings in the web dashboard
- After WiFi is configured, device switches to STA mode and disables AP

## 📋 Hardware Requirements

### Components
| Component | Quantity | Notes |
|-----------|----------|-------|
| TTGO T-Display | 1 | ESP32 with 1.14" ST7789 TFT |
| DS18B20 Sensors | 5-10 | Waterproof probe recommended |
| 4.7kΩ Resistor | 1 | Pull-up for OneWire bus |
| Jumper Wires | As needed | |

### TTGO T-Display Pinout
```
┌─────────────────────────────────────┐
│          TTGO T-Display             │
│  ┌─────────────────────────────┐    │
│  │                             │    │
│  │      1.14" TFT Display      │    │
│  │        (240x135)            │    │
│  │                             │    │
│  └─────────────────────────────┘    │
│                                     │
│  [BTN1]                    [BTN2]   │
│  GPIO35                    GPIO0    │
└─────────────────────────────────────┘

Available GPIOs:
  - GPIO27: DS18B20 Data (recommended)
  - GPIO26, GPIO25, GPIO33, GPIO32: Available
  
Reserved for Display:
  - GPIO4:  Backlight
  - GPIO5:  CS
  - GPIO16: DC
  - GPIO18: SCLK
  - GPIO19: MOSI
  - GPIO23: RST
```

### Wiring Diagram

```
                    4.7kΩ
TTGO GPIO27 ────┬───/\/\/───── 3.3V
                │
                ├──── DS18B20 #1 Data (Yellow)
                ├──── DS18B20 #2 Data (Yellow)
                ├──── DS18B20 #3 Data (Yellow)
                │     ... (up to 10 sensors)
                │
GND ────────────┴──── All DS18B20 GND (Black)
3.3V ─────────────── All DS18B20 VCC (Red)
```

### DS18B20 Pinout
```
   ┌─────────┐
   │  ╭───╮  │
   │  │   │  │
   │  ╰───╯  │
   └─┬─┬─┬───┘
     │ │ │
    GND DQ VDD
   (Blk)(Yel)(Red)
```

## 🚀 Getting Started

### Prerequisites
- [PlatformIO](https://platformio.org/) (VS Code extension recommended)
- USB cable for initial flash
- WiFi network (optional - can use AP mode)

### Installation

1. **Clone the Repository**
   ```bash
   git clone https://github.com/martiera/probe-station-esp32.git
   cd probe-station-esp32
   ```

2. **Open in PlatformIO**
   - Open VS Code with PlatformIO extension
   - File → Open Folder → Select project folder

3. **Build and Upload Firmware**
   ```bash
   # Build and upload (release environment is default)
   ./scripts/upload.sh --erase --fs
   ```

4. **Connect to Device**
   - On first boot, ESP32 creates open WiFi AP: `TempMonitor-XXXXXX` (no password)
   - Open browser: `http://192.168.4.1` (auto-redirects to WiFi settings)

### Initial Configuration

1. **Connect to WiFi**
   - Go to Settings tab
   - Scan for networks
   - Enter password and save

2. **Configure Sensors**
   - Go to Sensors tab
   - Name each sensor based on location
   - Set appropriate thresholds

3. **Configure MQTT (Optional)**
   - Enable MQTT in Settings
   - Enter broker address and credentials
   - Topics will be created automatically

## 📡 MQTT Topics

### Published Topics

```
tempmonitor/{device_name}/status              # Device status (online/offline)
tempmonitor/{device_name}/sensor/{name}/temperature  # Temperature readings
tempmonitor/{device_name}/sensor/{name}/alarm        # Alarm notifications
```

### Temperature Payload
```json
{
  "temperature": 23.5,
  "raw_temperature": 23.45,
  "unit": "C",
  "alarm": "normal",
  "connected": true,
  "name": "Hot Water Supply",
  "address": "28FF123456789012"
}
```

### Alarm Payload
```json
{
  "alarm": "high",
  "temperature": 82.5,
  "timestamp": 1234567890,
  "name": "Hot Water Supply",
  "threshold_low": 10.0,
  "threshold_high": 80.0
}
```

### Command Topics (Subscribe)
```
tempmonitor/{device_name}/cmd/calibrate   # Trigger calibration
tempmonitor/{device_name}/cmd/rescan      # Rescan sensors
tempmonitor/{device_name}/cmd/reboot      # Reboot device
```

## 🏠 Home Assistant Integration

The system automatically publishes Home Assistant MQTT discovery messages. Sensors will appear in Home Assistant without manual configuration.

### Example Lovelace Card
```yaml
type: entities
title: Temperature Monitoring
entities:
  - entity: sensor.tempmonitor_hot_water_supply
  - entity: sensor.tempmonitor_hot_water_return
  - entity: sensor.tempmonitor_heating_flow
  - entity: sensor.tempmonitor_heating_return
```

### Automation Example
```yaml
automation:
  - alias: "Hot Water Temperature Alert"
    trigger:
      platform: mqtt
      topic: "tempmonitor/TempMonitor/sensor/hot_water_supply/alarm"
    condition:
      condition: template
      value_template: "{{ trigger.payload_json.alarm != 'normal' }}"
    action:
      service: notify.mobile_app
      data:
        title: "Temperature Alert!"
        message: >
          {{ trigger.payload_json.name }}: {{ trigger.payload_json.temperature }}°C
          ({{ trigger.payload_json.alarm }})
```

## 🔧 REST API

### Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/status` | System status |
| GET | `/api/sensors` | All sensor data |
| GET | `/api/sensors/{id}` | Single sensor |
| POST | `/api/sensors/update` | Update sensor config |
| GET | `/api/config/wifi` | WiFi configuration |
| POST | `/api/config/wifi` | Update WiFi config |
| GET | `/api/config/mqtt` | MQTT configuration |
| POST | `/api/config/mqtt` | Update MQTT config |
| GET | `/api/config/system` | System configuration |
| POST | `/api/config/system` | Update system config |
| GET | `/api/wifi/scan` | Scan WiFi networks |
| POST | `/api/calibrate` | Calibrate sensors |
| POST | `/api/rescan` | Rescan for sensors |
| POST | `/api/reboot` | Reboot device |
| POST | `/api/reset` | Factory reset |
| GET | `/api/history/{id}` | Sensor history |

### WebSocket

Connect to `/ws` for real-time updates:

```javascript
const ws = new WebSocket('ws://192.168.1.100/ws');

ws.onmessage = (event) => {
  const data = JSON.parse(event.data);
  if (data.type === 'sensors') {
    // data.data contains array of sensor readings
    // data.summary contains avg, min, max, alarms
  }
};
```

## 📊 Calibration Guide

For accurate readings, calibrate sensors when they're at a known temperature:

### Ice Bath Method (0°C Reference)
1. Fill container with crushed ice and water
2. Stir well and let stabilize for 5 minutes
3. Insert all sensors into the ice bath
4. Wait for readings to stabilize
5. Enter `0.0` as reference temperature
6. Click "Calibrate All Sensors"

### Room Temperature Method
1. Place all sensors together in a room
2. Wait 30 minutes for thermal equilibrium
3. Use a reference thermometer to measure ambient temperature
4. Enter the reference temperature
5. Click "Calibrate All Sensors"

## 🗂️ Project Structure

```
probe-station-esp32/
├── platformio.ini          # PlatformIO configuration
├── README.md               # This file
├── src/
│   ├── main.cpp            # Main application
│   ├── config.h            # Hardware configuration
│   ├── config_manager.h/cpp    # Settings persistence
│   ├── sensor_manager.h/cpp    # DS18B20 handling
│   ├── wifi_manager.h/cpp      # WiFi/AP management
│   ├── mqtt_client.h/cpp       # MQTT publishing
│   ├── web_server.h/cpp        # HTTP server & API
│   └── display_manager.h/cpp   # TFT display handling
├── data/
│   ├── index.html          # Web dashboard
│   ├── style.css           # Dashboard styling
│   └── app.js              # Dashboard JavaScript
└── docs/
    └── images/             # Documentation images
```

## 📺 Display Interface

The TFT display shows 4 pages (use buttons to navigate):

| Page | Content |
|------|--------|
| **Sensors** | Temperature readings with color-coded status |
| **Status** | WiFi, MQTT connection, sensor count |
| **Alerts** | Active alarms with sensor names |
| **Info** | Device name, firmware, uptime, free memory |

### Button Controls
- **Button 1 (Top)**: Scroll through sensors / Navigate pages
- **Button 2 (Bottom)**: Switch to next page

### Temperature Colors
- 🟢 **Green**: Normal range
- 🟠 **Orange**: Near threshold (±5°C)
- 🔴 **Red**: Above high threshold
- 🔵 **Blue**: Below low threshold

## ⚠️ Troubleshooting

### Sensors Not Detected
- Check wiring connections
- Verify 4.7kΩ pull-up resistor is installed
- Ensure sensors are getting 3.3V power
- Try shorter cable lengths

### WiFi Connection Issues
- Check SSID and password
- Move closer to router
- Try a different WiFi channel
- Use 2.4GHz network (ESP32 doesn't support 5GHz)

### MQTT Not Connecting
- Verify broker address and port
- Check username/password
- Ensure broker allows external connections
- Check firewall settings

### Inaccurate Readings
- Perform calibration
- Check for heat sources near sensors
- Ensure good thermal contact
- Allow time for thermal stabilization

## 🔐 Security Considerations

- Change default AP password in production
- Use MQTT authentication
- Consider enabling HTTPS (requires certificate)
- Restrict network access if needed

## 📝 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 🙏 Acknowledgments

- [DallasTemperature Library](https://github.com/milesburton/Arduino-Temperature-Control-Library)
- [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer)
- [ArduinoJson](https://arduinojson.org/)
- [PubSubClient](https://pubsubclient.knolleary.net/)
- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) - Display driver for TTGO T-Display

## 📞 Support

- Create an issue for bug reports
- Pull requests welcome!
- Star the repo if you find it useful

---

Made with ❤️ for heating and hot water monitoring
