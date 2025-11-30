# ESP32 Temperature Monitoring System

A professional-grade temperature monitoring solution for heating and hot water pipe monitoring using ESP32 and DS18B20 sensors.

## 🌟 Features

### Core Functionality
- **Multi-sensor Support**: Connect 5-10 DS18B20 temperature sensors on a single bus
- **Real-time Monitoring**: Live temperature updates via WebSocket
- **Web Dashboard**: Modern, responsive interface accessible from any device
- **Temperature History**: Track temperature trends over time

### Connectivity
- **WiFi Manager**: Auto-connect to configured network or create Access Point for setup
- **MQTT Integration**: Publish temperatures and alarms to any MQTT broker
- **Home Assistant Auto-Discovery**: Sensors automatically appear in Home Assistant
- **OTA Updates**: Update firmware wirelessly

### Alerting
- **Configurable Thresholds**: Set high/low temperature limits per sensor
- **Hysteresis**: Prevents rapid alarm toggling at threshold boundaries
- **MQTT Alarms**: Instant notification when thresholds are exceeded
- **Web Notifications**: Real-time alerts in the dashboard

### Calibration
- **Offset-based Calibration**: Calibrate all sensors to a known reference temperature
- **Per-sensor Adjustment**: Fine-tune individual sensor readings
- **Persistent Storage**: Calibration data saved to flash memory

### Configuration
- **Sensor Naming**: Assign meaningful names to each sensor location
- **Customizable Intervals**: Adjust reading and publish frequencies
- **Static IP Support**: Optional fixed IP address configuration

## 📋 Hardware Requirements

### Components
| Component | Quantity | Notes |
|-----------|----------|-------|
| ESP32-WROOM-32 | 1 | Any ESP32 dev board works |
| DS18B20 Sensors | 5-10 | Waterproof probe recommended |
| 4.7kΩ Resistor | 1 | Pull-up for OneWire bus |
| Jumper Wires | As needed | |

### Wiring Diagram

```
                    4.7kΩ
ESP32 GPIO4 ────┬───/\/\/───── 3.3V
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
   # Build
   pio run
   
   # Upload to ESP32
   pio run --target upload
   ```

4. **Upload Web Interface**
   ```bash
   # Upload SPIFFS filesystem
   pio run --target uploadfs
   ```

5. **Connect to Device**
   - On first boot, ESP32 creates WiFi AP: `TempMonitor-Setup`
   - Password: `tempmonitor123`
   - Open browser: `http://192.168.4.1`

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
│   ├── main.cpp           # Main application
│   ├── config.h           # Hardware configuration
│   ├── config_manager.h/cpp   # Settings persistence
│   ├── sensor_manager.h/cpp   # DS18B20 handling
│   ├── wifi_manager.h/cpp     # WiFi/AP management
│   ├── mqtt_client.h/cpp      # MQTT publishing
│   └── web_server.h/cpp       # HTTP server & API
├── data/
│   ├── index.html         # Web dashboard
│   ├── style.css          # Dashboard styling
│   └── app.js             # Dashboard JavaScript
└── docs/
    └── images/            # Documentation images
```

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

## 📞 Support

- Create an issue for bug reports
- Pull requests welcome!
- Star the repo if you find it useful

---

Made with ❤️ for heating and hot water monitoring
