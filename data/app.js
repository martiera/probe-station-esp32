/**
 * ESP32 Temperature Monitor - Dashboard Application
 * Real-time temperature monitoring and configuration
 */

// ============================================================================
// Configuration
// ============================================================================

const WS_RECONNECT_INTERVAL = 5000;
const STATUS_UPDATE_INTERVAL = 30000;

// ============================================================================
// State
// ============================================================================

let websocket = null;
let sensors = [];
let systemStatus = null;
let reconnectTimer = null;
let pinnedSensorAddress = null;

function safeLocalStorageGet(key) {
    try {
        return localStorage.getItem(key);
    } catch (_) {
        return null;
    }
}

function safeLocalStorageSet(key, value) {
    try {
        localStorage.setItem(key, value);
    } catch (_) {
        // ignore (some captive-portal webviews disable storage)
    }
}

function safeLocalStorageRemove(key) {
    try {
        localStorage.removeItem(key);
    } catch (_) {
        // ignore
    }
}

pinnedSensorAddress = safeLocalStorageGet('pinnedSensor') || null;

let otaPollTimer = null;

// ============================================================================
// Initialization
// ============================================================================

document.addEventListener('DOMContentLoaded', () => {
    initTabs();
    connectWebSocket();
    loadStatus();
    loadConfigurations();
    loadOtaInfo();
    
    // Periodic status update
    setInterval(loadStatus, STATUS_UPDATE_INTERVAL);
});

// ============================================================================
// OTA (GitHub Releases)
// ============================================================================

let otaCheckTimer = null;

async function loadOtaInfo() {
    try {
        const info = await apiGet('ota/info');
        document.getElementById('otaCurrent').textContent = info.current || '--';
        document.getElementById('otaAvailable').textContent = (info.latest && info.latest.tag) ? info.latest.tag : '--';

        if (info.state === 'checking') {
            document.getElementById('otaStatus').textContent = info.statusMessage || 'Checking...';
            // Poll every 2s while checking
            if (!otaCheckTimer) {
                otaCheckTimer = setTimeout(() => {
                    otaCheckTimer = null;
                    loadOtaInfo();
                }, 2000);
            }
        } else {
            // Stop polling when check completes
            if (otaCheckTimer) {
                clearTimeout(otaCheckTimer);
                otaCheckTimer = null;
            }
            
            if (info.error) {
                document.getElementById('otaStatus').textContent = `Error: ${info.error}`;
            } else if (info.statusMessage) {
                document.getElementById('otaStatus').textContent = info.statusMessage;
            } else {
                document.getElementById('otaStatus').textContent = info.updateAvailable ? 'Update available' : 'Up to date';
            }
        }
        document.getElementById('otaProgress').textContent = '--';

        const btn = document.getElementById('otaUpdateBtn');
        btn.disabled = !info.updateAvailable;
        btn.textContent = info.updateAvailable ? '⬆️ Flash Update' : 'Up to date';
    } catch (e) {
        const msg = (e && e.message) ? e.message : 'unable to load OTA info';
        document.getElementById('otaStatus').textContent = `Error: ${msg}`;
        // Stop polling on error
        if (otaCheckTimer) {
            clearTimeout(otaCheckTimer);
            otaCheckTimer = null;
        }
    }
}

function checkOta() {
    // Show immediate feedback
    document.getElementById('otaStatus').textContent = 'Checking for updates...';
    document.getElementById('otaAvailable').textContent = '--';
    loadOtaInfo();
}

async function startOtaUpdate() {
    const confirmMsg = 'This will update firmware and web UI, then reboot the device. Continue?';
    if (!confirm(confirmMsg)) return;

    const btn = document.getElementById('otaUpdateBtn');
    btn.disabled = true;
    btn.textContent = 'Starting...';
    document.getElementById('otaStatus').textContent = 'Starting update...';

    try {
        await apiPost('ota/update', { target: 'both' });
        startOtaPolling();
    } catch (e) {
        document.getElementById('otaStatus').textContent = 'Error: failed to start OTA';
        btn.disabled = false;
        btn.textContent = '⬆️ Flash Update';
    }
}

function startOtaPolling() {
    stopOtaPolling();
    otaPollTimer = setInterval(loadOtaStatus, 2000);
    loadOtaStatus();
}

function stopOtaPolling() {
    if (otaPollTimer) {
        clearInterval(otaPollTimer);
        otaPollTimer = null;
    }
}

async function loadOtaStatus() {
    try {
        const st = await apiGet('ota/status');
        document.getElementById('otaStatus').textContent = st.state || '--';
        document.getElementById('otaProgress').textContent = (typeof st.progress === 'number') ? `${st.progress}%` : '--';

        if (st.error) {
            document.getElementById('otaStatus').textContent = `Error: ${st.error}`;
            stopOtaPolling();
            await loadOtaInfo();
            return;
        }

        if (st.state === 'idle' || st.state === 'ready') {
            stopOtaPolling();
            await loadOtaInfo();
            return;
        }

        if (st.state === 'rebooting') {
            stopOtaPolling();
            document.getElementById('otaStatus').textContent = 'Rebooting...';
            document.getElementById('otaProgress').textContent = '100%';
        }
    } catch (e) {
        // During reboot the device will go offline; stop polling quietly.
        stopOtaPolling();
    }
}

// ============================================================================
// Tab Navigation
// ============================================================================

function initTabs() {
    const tabs = document.querySelectorAll('.tab');
    
    tabs.forEach(tab => {
        tab.addEventListener('click', () => {
            // Update tab buttons
            tabs.forEach(t => t.classList.remove('active'));
            tab.classList.add('active');
            
            // Update tab content
            document.querySelectorAll('.tab-content').forEach(content => {
                content.classList.remove('active');
            });
            
            const tabId = tab.dataset.tab;
            document.getElementById(`tab-${tabId}`).classList.add('active');
            
            // Load tab-specific data
            if (tabId === 'settings') {
                scanWifi();
            }
        });
    });
}

// ============================================================================
// WebSocket Connection
// ============================================================================

function connectWebSocket() {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.host}/ws`;
    
    console.log('Connecting to WebSocket:', wsUrl);
    
    websocket = new WebSocket(wsUrl);
    
    websocket.onopen = () => {
        console.log('WebSocket connected');
        clearTimeout(reconnectTimer);
    };
    
    websocket.onmessage = (event) => {
        try {
            const data = JSON.parse(event.data);
            handleWebSocketMessage(data);
        } catch (e) {
            console.error('Error parsing WebSocket message:', e);
        }
    };
    
    websocket.onclose = () => {
        console.log('WebSocket disconnected');
        reconnectTimer = setTimeout(connectWebSocket, WS_RECONNECT_INTERVAL);
    };
    
    websocket.onerror = (error) => {
        console.error('WebSocket error:', error);
    };
}

function handleWebSocketMessage(data) {
    switch (data.type) {
        case 'sensors':
            sensors = data.data;
            updateSensorDisplay();
            updateSummary(data.summary);
            break;
            
        case 'notification':
            showToast(data.message, data.level);
            break;
    }
}

// ============================================================================
// API Calls
// ============================================================================

async function apiGet(endpoint) {
    const response = await fetch(`/api/${endpoint}`);
    const text = await response.text();
    let payload = null;
    try {
        payload = text ? JSON.parse(text) : null;
    } catch (_) {
        payload = null;
    }
    if (!response.ok) {
        const msg = (payload && payload.message) ? payload.message : `API error: ${response.status}`;
        throw new Error(msg);
    }
    return payload;
}

async function apiPost(endpoint, data = {}) {
    const response = await fetch(`/api/${endpoint}`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(data)
    });
    const text = await response.text();
    let payload = null;
    try {
        payload = text ? JSON.parse(text) : null;
    } catch (_) {
        payload = null;
    }
    if (!response.ok) {
        const msg = (payload && payload.message) ? payload.message : `API error: ${response.status}`;
        throw new Error(msg);
    }
    return payload;
}

async function apiPut(endpoint, data) {
    const response = await fetch(`/api/${endpoint}`, {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(data)
    });
    const text = await response.text();
    let payload = null;
    try {
        payload = text ? JSON.parse(text) : null;
    } catch (_) {
        payload = null;
    }
    if (!response.ok) {
        const msg = (payload && payload.message) ? payload.message : `API error: ${response.status}`;
        throw new Error(msg);
    }
    return payload;
}

// ============================================================================
// Status & Data Loading
// ============================================================================

async function loadStatus() {
    try {
        systemStatus = await apiGet('status');
        updateStatusDisplay();
    } catch (error) {
        console.error('Error loading status:', error);
    }
}

function updateStatusDisplay() {
    if (!systemStatus) return;
    
    // Device name
    document.getElementById('deviceName').textContent = systemStatus.device.name;
    
    // WiFi status
    const wifiStatus = document.getElementById('wifiStatus');
    wifiStatus.classList.toggle('connected', systemStatus.wifi.status === 'connected');
    
    // MQTT status
    const mqttStatus = document.getElementById('mqttStatus');
    mqttStatus.classList.toggle('connected', systemStatus.mqtt.connected);
    
    // System info
    document.getElementById('uptime').textContent = formatUptime(systemStatus.device.uptime);
    document.getElementById('ipAddress').textContent = systemStatus.wifi.ip;
    document.getElementById('wifiSignal').textContent = `${systemStatus.wifi.signal}%`;
    document.getElementById('freeHeap').textContent = formatBytes(systemStatus.device.freeHeap);
    document.getElementById('firmware').textContent = systemStatus.device.firmware;
    document.getElementById('sensorCount').textContent = systemStatus.sensors.count;
}

async function loadConfigurations() {
    try {
        // Load WiFi config
        const wifiConfig = await apiGet('config/wifi');
        populateWifiForm(wifiConfig);
        
        // Load MQTT config
        const mqttConfig = await apiGet('config/mqtt');
        populateMqttForm(mqttConfig);
        
        // Load System config
        const sysConfig = await apiGet('config/system');
        populateSystemForm(sysConfig);
    } catch (error) {
        console.error('Error loading configurations:', error);
    }
}

// ============================================================================
// Sensor Display
// ============================================================================

function updateSensorDisplay() {
    updateSensorGrid();
    updateSensorList();
    updateCalibrationList();
}

function updateSensorGrid() {
    const grid = document.getElementById('sensorGrid');
    
    if (sensors.length === 0) {
        grid.innerHTML = '<div class="loading">No sensors found. Click Rescan to search.</div>';
        return;
    }
    
    grid.innerHTML = sensors.map((sensor, index) => `
        <div class="sensor-card ${getAlarmClass(sensor)}">
            <div class="sensor-header">
                <div>
                    <div class="sensor-name">${escapeHtml(sensor.name || `Sensor ${index + 1}`)}</div>
                    <div class="sensor-address">${sensor.address}</div>
                </div>
                <span class="sensor-status ${sensor.alarm}">${sensor.alarm}</span>
            </div>
            <div class="sensor-temperature">
                ${sensor.connected ? formatTemp(sensor.temperature) : '--'}
                <span class="unit">°C</span>
            </div>
            <div class="sensor-thresholds">
                <span>Low: ${sensor.thresholdLow}°C</span>
                <span>High: ${sensor.thresholdHigh}°C</span>
            </div>
        </div>
    `).join('');
}

function updateSensorList() {
    const list = document.getElementById('sensorList');
    
    if (sensors.length === 0) {
        list.innerHTML = '<div class="loading">No sensors configured</div>';
        return;
    }
    
    list.innerHTML = sensors.map((sensor, index) => `
        <div class="sensor-list-item">
            <div class="sensor-list-info">
                <div class="sensor-list-name">${escapeHtml(sensor.name || `Sensor ${index + 1}`)}</div>
                <div class="sensor-list-details">
                    ${sensor.address} | Thresholds: ${sensor.thresholdLow}°C - ${sensor.thresholdHigh}°C
                    ${sensor.alertEnabled ? '' : '| Alerts disabled'}
                </div>
            </div>
            <button class="btn btn-secondary" onclick="editSensor(${index})">Edit</button>
        </div>
    `).join('');
}

function updateCalibrationList() {
    const list = document.getElementById('offsetList');
    
    if (sensors.length === 0) {
        list.innerHTML = '<div class="loading">No sensors to calibrate</div>';
        return;
    }
    
    list.innerHTML = sensors.map((sensor, index) => `
        <div class="offset-item">
            <span>${escapeHtml(sensor.name || `Sensor ${index + 1}`)}</span>
            <span class="offset-value">${formatOffset(sensor.calibrationOffset)}</span>
        </div>
    `).join('');
}

function updateSummary(summary) {
    if (!summary) return;
    
    // Find min and max sensors
    let minSensor = null;
    let maxSensor = null;
    let pinnedSensor = null;
    
    for (const sensor of sensors) {
        if (!sensor.connected || sensor.temperature === null) continue;
        
        if (minSensor === null || sensor.temperature < minSensor.temperature) {
            minSensor = sensor;
        }
        if (maxSensor === null || sensor.temperature > maxSensor.temperature) {
            maxSensor = sensor;
        }
        if (sensor.address === pinnedSensorAddress) {
            pinnedSensor = sensor;
        }
    }
    
    // Update min display
    if (minSensor) {
        document.getElementById('minTemp').textContent = formatTemp(minSensor.temperature);
        document.getElementById('minName').textContent = minSensor.name || 'Minimum';
    } else {
        document.getElementById('minTemp').textContent = '--';
        document.getElementById('minName').textContent = 'Minimum';
    }
    
    // Update max display
    if (maxSensor) {
        document.getElementById('maxTemp').textContent = formatTemp(maxSensor.temperature);
        document.getElementById('maxName').textContent = maxSensor.name || 'Maximum';
    } else {
        document.getElementById('maxTemp').textContent = '--';
        document.getElementById('maxName').textContent = 'Maximum';
    }
    
    // Update pinned sensor display
    if (pinnedSensor) {
        document.getElementById('pinnedTemp').textContent = formatTemp(pinnedSensor.temperature);
        document.getElementById('pinnedName').textContent = pinnedSensor.name || 'Pinned';
        document.getElementById('pinnedCard').classList.remove('no-pin');
    } else {
        document.getElementById('pinnedTemp').textContent = '--';
        document.getElementById('pinnedName').textContent = 'Click to pin';
        document.getElementById('pinnedCard').classList.add('no-pin');
    }
    
    // Update alarm count
    document.getElementById('alarmCount').textContent = summary.alarms;
    
    // Highlight alarm card if there are alarms
    const alarmCard = document.getElementById('alarmCard');
    alarmCard.classList.toggle('alarm', summary.alarms > 0);
}

function getAlarmClass(sensor) {
    if (!sensor.connected) return 'disconnected';
    if (sensor.alarm === 'high') return 'alarm-high';
    if (sensor.alarm === 'low') return 'alarm-low';
    return '';
}

function selectPinnedSensor() {
    if (sensors.length === 0) {
        showToast('No sensors available', 'warning');
        return;
    }
    
    // Create a simple selection dialog
    const options = sensors
        .filter(s => s.connected)
        .map(s => `<option value="${s.address}"${s.address === pinnedSensorAddress ? ' selected' : ''}>${s.name || 'Sensor'} (${formatTemp(s.temperature)})</option>`)
        .join('');
    
    const dialog = document.createElement('div');
    dialog.className = 'modal active';
    dialog.innerHTML = `
        <div class="modal-content">
            <h3>Select Pinned Sensor</h3>
            <div class="form-group">
                <label>Sensor</label>
                <select id="pinnedSensorSelect">
                    <option value="">-- None --</option>
                    ${options}
                </select>
            </div>
            <div class="button-group">
                <button class="btn btn-primary" onclick="savePinnedSensor()">Save</button>
                <button class="btn btn-secondary" onclick="closePinDialog()">Cancel</button>
            </div>
        </div>
    `;
    dialog.id = 'pinDialog';
    document.body.appendChild(dialog);
}

function savePinnedSensor() {
    const select = document.getElementById('pinnedSensorSelect');
    pinnedSensorAddress = select.value || null;
    
    if (pinnedSensorAddress) {
        safeLocalStorageSet('pinnedSensor', pinnedSensorAddress);
    } else {
        safeLocalStorageRemove('pinnedSensor');
    }
    
    closePinDialog();
    
    // Force summary update
    if (sensors.length > 0) {
        updateSummary({ alarms: document.getElementById('alarmCount').textContent });
    }
    
    showToast('Pinned sensor updated', 'success');
}

function closePinDialog() {
    const dialog = document.getElementById('pinDialog');
    if (dialog) dialog.remove();
}

// ============================================================================
// Sensor Configuration
// ============================================================================

function editSensor(index) {
    const sensor = sensors[index];
    if (!sensor) return;
    
    document.getElementById('editSensorIndex').value = index;
    document.getElementById('editSensorName').value = sensor.name || '';
    document.getElementById('editThresholdLow').value = sensor.thresholdLow;
    document.getElementById('editThresholdHigh').value = sensor.thresholdHigh;
    document.getElementById('editAlertEnabled').checked = sensor.alertEnabled;
    
    document.getElementById('sensorModal').classList.add('active');
}

function closeModal() {
    document.getElementById('sensorModal').classList.remove('active');
}

async function saveSensor() {
    const index = parseInt(document.getElementById('editSensorIndex').value);
    
    const data = {
        index: index,
        name: document.getElementById('editSensorName').value,
        thresholdLow: parseFloat(document.getElementById('editThresholdLow').value),
        thresholdHigh: parseFloat(document.getElementById('editThresholdHigh').value),
        alertEnabled: document.getElementById('editAlertEnabled').checked
    };
    
    try {
        await apiPost('sensors/update', data);
        showToast('Sensor updated successfully', 'success');
        closeModal();
    } catch (error) {
        showToast('Error updating sensor', 'error');
    }
}

async function rescanSensors() {
    try {
        await apiPost('rescan');
        showToast('Sensor rescan initiated', 'success');
    } catch (error) {
        showToast('Error initiating rescan', 'error');
    }
}

// ============================================================================
// Calibration
// ============================================================================

async function calibrateAll() {
    const refTemp = parseFloat(document.getElementById('refTemp').value);
    
    if (isNaN(refTemp)) {
        showToast('Please enter a reference temperature', 'warning');
        return;
    }
    
    try {
        await apiPost('calibrate', { referenceTemp: refTemp });
        showToast('All sensors calibrated', 'success');
    } catch (error) {
        showToast('Error during calibration', 'error');
    }
}

async function calibrateNew() {
    const refTemp = parseFloat(document.getElementById('refTemp').value);
    
    if (isNaN(refTemp)) {
        showToast('Please enter a reference temperature', 'warning');
        return;
    }
    
    try {
        const result = await apiPost('calibrate/new', { referenceTemp: refTemp });
        showToast(result.message || 'New sensors calibrated', 'success');
    } catch (error) {
        showToast('Error during calibration', 'error');
    }
}

async function resetCalibration() {
    if (!confirm('Reset calibration offsets for all sensors?')) {
        return;
    }
    
    try {
        await apiPost('calibrate', { referenceTemp: null, reset: true });
        showToast('Calibration reset', 'success');
    } catch (error) {
        showToast('Error resetting calibration', 'error');
    }
}

// ============================================================================
// WiFi Configuration
// ============================================================================

async function scanWifi(retryCount = 0) {
    const select = document.getElementById('wifiSsid');
    select.innerHTML = '<option>Scanning...</option>';
    
    try {
        const response = await fetch('/api/wifi/scan');
        const data = await response.json();
        
        // Check if scan is still in progress
        if (response.status === 202 || data.status === 'scanning') {
            if (retryCount < 5) {
                select.innerHTML = `<option>Scanning... (attempt ${retryCount + 1}/5)</option>`;
                // Retry after 2 seconds
                setTimeout(() => scanWifi(retryCount + 1), 2000);
                return;
            } else {
                select.innerHTML = '<option>Scan timeout, try again</option>';
                return;
            }
        }
        
        if (!response.ok) {
            throw new Error(data.error || 'Scan failed');
        }
        
        const networks = Array.isArray(data) ? data : [];
        
        // Sort by signal strength
        networks.sort((a, b) => b.rssi - a.rssi);
        
        // Remove duplicates
        const unique = [];
        const seen = new Set();
        for (const net of networks) {
            if (!seen.has(net.ssid) && net.ssid) {
                seen.add(net.ssid);
                unique.push(net);
            }
        }
        
        if (unique.length === 0) {
            select.innerHTML = '<option value="">No networks found</option>';
        } else {
            select.innerHTML = unique.map(net => `
                <option value="${escapeHtml(net.ssid)}">
                    ${escapeHtml(net.ssid)} (${net.signal}%${net.encrypted ? ' 🔒' : ''})
                </option>
            `).join('');
        }
        
        // Add manual entry option
        select.innerHTML += '<option value="">-- Enter manually --</option>';
    } catch (error) {
        console.error('WiFi scan error:', error);
        select.innerHTML = '<option>Error scanning</option>';
        showToast('Error scanning WiFi networks: ' + error.message, 'error');
    }
}

function populateWifiForm(config) {
    const select = document.getElementById('wifiSsid');
    if (config.ssid) {
        const option = document.createElement('option');
        option.value = config.ssid;
        option.textContent = config.ssid;
        option.selected = true;
        select.appendChild(option);
    }
    
    document.getElementById('wifiDhcp').checked = config.dhcp;
    document.getElementById('staticIp').value = config.staticIP || '';
    document.getElementById('gateway').value = config.gateway || '';
    document.getElementById('subnet').value = config.subnet || '';
    document.getElementById('dns').value = config.dns || '';
    
    toggleStaticIP();
}

function toggleStaticIP() {
    const dhcp = document.getElementById('wifiDhcp').checked;
    document.getElementById('staticIpFields').style.display = dhcp ? 'none' : 'grid';
}

async function saveWifiConfig() {
    const data = {
        ssid: document.getElementById('wifiSsid').value,
        password: document.getElementById('wifiPassword').value,
        dhcp: document.getElementById('wifiDhcp').checked,
        staticIP: document.getElementById('staticIp').value,
        gateway: document.getElementById('gateway').value,
        subnet: document.getElementById('subnet').value,
        dns: document.getElementById('dns').value
    };
    
    try {
        await apiPost('config/wifi', data);
        showToast('WiFi configuration saved. Reconnecting...', 'success');
    } catch (error) {
        showToast('Error saving WiFi configuration', 'error');
    }
}

// ============================================================================
// MQTT Configuration
// ============================================================================

function populateMqttForm(config) {
    document.getElementById('mqttEnabled').checked = config.enabled;
    document.getElementById('mqttServer').value = config.server || '';
    document.getElementById('mqttPort').value = config.port || 1883;
    document.getElementById('mqttUser').value = config.username || '';
    document.getElementById('mqttTopic').value = config.topicPrefix || 'tempmonitor';
    document.getElementById('mqttInterval').value = config.publishInterval || 10;
}

async function saveMqttConfig() {
    const data = {
        enabled: document.getElementById('mqttEnabled').checked,
        server: document.getElementById('mqttServer').value,
        port: parseInt(document.getElementById('mqttPort').value),
        username: document.getElementById('mqttUser').value,
        password: document.getElementById('mqttPassword').value,
        topicPrefix: document.getElementById('mqttTopic').value,
        publishInterval: parseInt(document.getElementById('mqttInterval').value)
    };
    
    try {
        await apiPost('config/mqtt', data);
        showToast('MQTT configuration saved', 'success');
    } catch (error) {
        showToast('Error saving MQTT configuration', 'error');
    }
}

// ============================================================================
// System Configuration
// ============================================================================

function populateSystemForm(config) {
    document.getElementById('deviceNameInput').value = config.deviceName || '';
    document.getElementById('readInterval').value = config.readInterval || 2;
    document.getElementById('celsiusUnits').checked = config.celsiusUnits;
}

async function saveSystemConfig() {
    const data = {
        deviceName: document.getElementById('deviceNameInput').value,
        readInterval: parseInt(document.getElementById('readInterval').value),
        celsiusUnits: document.getElementById('celsiusUnits').checked
    };
    
    try {
        await apiPost('config/system', data);
        showToast('System configuration saved', 'success');
        loadStatus();
    } catch (error) {
        showToast('Error saving system configuration', 'error');
    }
}

// ============================================================================
// Device Actions
// ============================================================================

async function rebootDevice() {
    if (!confirm('Are you sure you want to reboot the device?')) {
        return;
    }
    
    try {
        await apiPost('reboot');
        showToast('Device is rebooting...', 'info');
    } catch (error) {
        // Expected - device will disconnect
    }
}

async function factoryReset() {
    if (!confirm('⚠️ This will erase all settings!\n\nAre you sure you want to factory reset?')) {
        return;
    }
    
    try {
        await apiPost('reset');
        showToast('Factory reset complete. Device is rebooting...', 'warning');
    } catch (error) {
        // Expected - device will disconnect
    }
}

function openSettings() {
    document.querySelector('.tab[data-tab="settings"]').click();
}

// ============================================================================
// Utility Functions
// ============================================================================

function formatTemp(temp) {
    if (temp === null || temp === undefined || temp === -127) {
        return '--';
    }
    return temp.toFixed(1);
}

function formatOffset(offset) {
    if (offset === null || offset === undefined) {
        return '0.00°C';
    }
    const sign = offset >= 0 ? '+' : '';
    return `${sign}${offset.toFixed(2)}°C`;
}

function formatUptime(seconds) {
    const days = Math.floor(seconds / 86400);
    const hours = Math.floor((seconds % 86400) / 3600);
    const mins = Math.floor((seconds % 3600) / 60);
    
    if (days > 0) {
        return `${days}d ${hours}h ${mins}m`;
    }
    if (hours > 0) {
        return `${hours}h ${mins}m`;
    }
    return `${mins}m`;
}

function formatBytes(bytes) {
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1048576) return (bytes / 1024).toFixed(1) + ' KB';
    return (bytes / 1048576).toFixed(1) + ' MB';
}

function escapeHtml(text) {
    if (!text) return '';
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

// ============================================================================
// Toast Notifications
// ============================================================================

function showToast(message, type = 'info') {
    const container = document.getElementById('toastContainer');
    
    const toast = document.createElement('div');
    toast.className = `toast ${type}`;
    toast.textContent = message;
    
    container.appendChild(toast);
    
    // Remove after 5 seconds
    setTimeout(() => {
        toast.style.animation = 'slideIn 0.3s ease reverse';
        setTimeout(() => toast.remove(), 300);
    }, 5000);
}
