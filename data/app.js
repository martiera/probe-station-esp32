/**
 * ESP32 Temperature Monitor - Dashboard Application
 * Real-time temperature monitoring and configuration
 */

// ============================================================================
// Configuration
// ============================================================================

let SENSOR_POLL_INTERVAL = 5000; // Poll sensors dynamically based on readInterval (default 5s)
const STATUS_UPDATE_INTERVAL = 30000;

// ============================================================================
// State
// ============================================================================

let sensors = [];
let systemStatus = null;
let sensorPollTimer = null;
let pinnedSensorAddress = null;  // Loaded from server, not localStorage
let sensorOrder = [];  // Array of sensor addresses in display order

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

let otaPollTimer = null;
let otaLastProgress = -1;
let otaLastProgressTime = 0;
const OTA_PROGRESS_TIMEOUT_MS = 60000; // 1 minute timeout if no progress

// Parse version string (e.g., "v1.0.7" or "1.0.7") into comparable number
function parseVersion(version) {
    if (!version) return 0;
    let v = version.replace(/^v/i, ''); // Remove 'v' prefix
    let parts = v.split(/[.-]/); // Split on dot or dash
    let major = parseInt(parts[0]) || 0;
    let minor = parseInt(parts[1]) || 0;
    let patch = parseInt(parts[2]) || 0;
    return major * 10000 + minor * 100 + patch;
}

// Compare two version strings: returns >0 if v1 > v2, <0 if v1 < v2, 0 if equal
function compareVersions(v1, v2) {
    return parseVersion(v1) - parseVersion(v2);
}

// Chart state
let chartData = {}; // {address: [{time, temp}, ...]}
let chartSelectedSensor = ''; // Empty = all sensors
let chartDataLoaded = false; // Track if initial history was loaded from API
let lastChartRedraw = 0; // Track last redraw time

// ============================================================================
// Initialization
// ============================================================================

document.addEventListener('DOMContentLoaded', async () => {
    // Check if OTA was in progress (for cross-client/tab sync)
    checkAndRestoreOtaOverlay();
    initTabs();
    initChart();
    
    // Initial data load - load config first to get pinned sensor before first sensor display
    await loadConfigurations();
    
    // Then stagger the rest to avoid overwhelming ESP32
    loadSensors();
    setTimeout(() => {
        loadStatus().then(() => {
            // Restore collapsed states after status is loaded
            restoreCollapsedStates();
        });
    }, 200);
    setTimeout(() => loadOtaInfo(), 400);
    
    // Check for updates and show banner if available
    setTimeout(() => checkForUpdates(), 600);
    
    // Start polling (chart history loaded in first loadSensors call)
    sensorPollTimer = setInterval(loadSensors, SENSOR_POLL_INTERVAL);
    setInterval(loadStatus, STATUS_UPDATE_INTERVAL);
});

// ============================================================================
// OTA (GitHub Releases)
// ============================================================================

let otaCheckTimer = null;
let otaUpdateAvailable = false;

async function loadOtaInfo(force = false) {
    try {
        const url = force ? 'ota/info?force=1' : 'ota/info';
        const info = await apiGet(url);
        document.getElementById('otaCurrent').textContent = info.current || '--';
        document.getElementById('otaAvailable').textContent = (info.latest && info.latest.tag) ? info.latest.tag : '--';

        const btn = document.getElementById('otaUpdateBtn');
        
        if (info.state === 'checking') {
            document.getElementById('otaStatus').textContent = 'Checking for updates...';
            btn.disabled = true;
            btn.textContent = '🔄 Checking...';
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
            
            otaUpdateAvailable = info.updateAvailable;
            
            if (info.error) {
                document.getElementById('otaStatus').textContent = info.error;
                btn.disabled = false;
                btn.textContent = '🔄 Check for Updates';
            } else if (info.updateAvailable) {
                document.getElementById('otaStatus').textContent = `New version ${info.latest.tag} available`;
                btn.disabled = false;
                btn.textContent = '⬆️ Install Update';
            } else if (info.latest && info.latest.tag) {
                // Latest version is fetched but not newer
                if (compareVersions(info.current, info.latest.tag) > 0) {
                    document.getElementById('otaStatus').textContent = `You have a newer version (${info.current} > ${info.latest.tag})`;
                } else {
                    document.getElementById('otaStatus').textContent = 'You have the latest version';
                }
                btn.disabled = false;
                btn.textContent = '🔄 Check for Updates';
            } else {
                document.getElementById('otaStatus').textContent = 'No release information available';
                btn.disabled = false;
                btn.textContent = '🔄 Check for Updates';
            }
        }
        
        // If new update available, clear dismissed flag so banner shows
        if (info.updateAvailable) {
            safeLocalStorageRemove('updateBannerDismissed');
            showUpdateBannerIfAvailable();
        }
    } catch (e) {
        const msg = (e && e.message) ? e.message : 'Unable to check for updates';
        document.getElementById('otaStatus').textContent = msg;
        document.getElementById('otaUpdateBtn').disabled = false;
        document.getElementById('otaUpdateBtn').textContent = '🔄 Check for Updates';
        // Stop polling on error
        if (otaCheckTimer) {
            clearTimeout(otaCheckTimer);
            otaCheckTimer = null;
        }
    }
}

function handleOtaButton() {
    if (otaUpdateAvailable) {
        startOtaUpdate();
    } else {
        checkOta();
    }
}

function checkOta() {
    const btn = document.getElementById('otaUpdateBtn');
    btn.disabled = true;
    btn.textContent = '🔄 Checking...';
    document.getElementById('otaStatus').textContent = 'Checking for updates...';
    document.getElementById('otaAvailable').textContent = '--';
    loadOtaInfo(true); // Force check
}

async function startOtaUpdate() {
    const btn = document.getElementById('otaUpdateBtn');
    btn.disabled = true;
    btn.textContent = '⬆️ Installing...';
    document.getElementById('otaStatus').textContent = 'Starting update...';

    try {
        await apiPost('ota/update', { target: 'both' });
        showOtaOverlay();
        startOtaPolling();
    } catch (e) {
        const msg = (e && e.message) ? e.message : 'Failed to start update';
        document.getElementById('otaStatus').textContent = msg;
        btn.disabled = false;
        btn.textContent = '⬆️ Install Update';
    }
}

function showOtaOverlay() {
    document.getElementById('otaOverlay').style.display = 'flex';
    document.getElementById('otaModalStatus').textContent = 'Preparing...';
    document.getElementById('otaModalPercent').textContent = '0%';
    document.getElementById('otaProgressFill').style.width = '0%';
    // Mark OTA in progress for cross-client sync
    safeLocalStorageSet('otaInProgress', 'true');
    safeLocalStorageSet('otaStartTime', Date.now().toString());
    // Reset progress tracking
    otaLastProgress = -1;
    otaLastProgressTime = Date.now();
}

function hideOtaOverlay() {
    document.getElementById('otaOverlay').style.display = 'none';
    // Clear OTA progress flag
    safeLocalStorageRemove('otaInProgress');
    safeLocalStorageRemove('otaStartTime');
}

// Check if OTA was in progress when page loaded (for cross-client sync)
function checkAndRestoreOtaOverlay() {
    const inProgress = safeLocalStorageGet('otaInProgress');
    if (inProgress === 'true') {
        // OTA was in progress, show overlay and start polling
        document.getElementById('otaOverlay').style.display = 'flex';
        document.getElementById('otaModalStatus').textContent = 'Connecting...';
        otaLastProgress = -1;
        otaLastProgressTime = Date.now();
        startOtaPolling();
    }
}

function updateOtaOverlay(status, progress) {
    document.getElementById('otaModalStatus').textContent = status;
    document.getElementById('otaModalPercent').textContent = `${progress}%`;
    document.getElementById('otaProgressFill').style.width = `${progress}%`;
}

function startOtaPolling() {
    stopOtaPolling();
    otaPollTimer = setInterval(loadOtaStatus, 1000); // Poll every 1 second for smoother updates
    loadOtaStatus();
}

function stopOtaPolling() {
    if (otaPollTimer) {
        clearInterval(otaPollTimer);
        otaPollTimer = null;
    }
}

let otaRebootCheckTimer = null;

async function loadOtaStatus() {
    try {
        const st = await apiGet('ota/status');
        
        // Map internal states to user-friendly messages
        let userStatus = st.state || 'Unknown';
        let overlayStatus = st.message || 'Updating...';
        const progress = (typeof st.progress === 'number') ? st.progress : 0;
        
        // User-friendly status mapping
        if (st.state === 'updating_spiffs') {
            userStatus = 'Updating web interface...';
            overlayStatus = 'Updating web interface...';
        } else if (st.state === 'updating_firmware') {
            userStatus = 'Updating firmware...';
            overlayStatus = 'Updating firmware...';
        } else if (st.state === 'rebooting') {
            userStatus = 'Restarting device...';
            overlayStatus = 'Restarting device...';
        } else if (st.state === 'checking') {
            userStatus = 'Checking for updates...';
        } else if (st.state === 'ready') {
            userStatus = 'Ready';
        } else if (st.state === 'idle') {
            userStatus = 'Idle';
        }
        
        document.getElementById('otaStatus').textContent = userStatus;
        updateOtaOverlay(overlayStatus, progress);

        // Check for progress timeout (1 minute without progress change)
        if (progress > otaLastProgress) {
            // Progress increased, reset timeout
            otaLastProgress = progress;
            otaLastProgressTime = Date.now();
        } else if (st.state && (st.state.includes('updating') || st.state.includes('download'))) {
            // Only check timeout during update phase
            const elapsed = Date.now() - otaLastProgressTime;
            if (elapsed > OTA_PROGRESS_TIMEOUT_MS) {
                // No progress for 1 minute, likely stuck
                document.getElementById('otaStatus').textContent = 'Update stalled - please try again';
                updateOtaOverlay('Update stalled', progress);
                stopOtaPolling();
                setTimeout(hideOtaOverlay, 5000);
                await loadOtaInfo();
                return;
            }
        }

        if (st.error) {
            document.getElementById('otaStatus').textContent = st.error;
            updateOtaOverlay(st.error, progress);
            stopOtaPolling();
            // Hide overlay after 3 seconds on error
            setTimeout(hideOtaOverlay, 3000);
            await loadOtaInfo();
            return;
        }

        if (st.state === 'idle' || st.state === 'ready') {
            stopOtaPolling();
            hideOtaOverlay();
            await loadOtaInfo();
            return;
        }

        if (st.state === 'rebooting') {
            stopOtaPolling();
            updateOtaOverlay('Restarting device...', 100);
            document.getElementById('otaStatus').textContent = 'Restarting device...';
            // Start checking if device comes back online
            startRebootCheck();
        }
    } catch (e) {
        // During reboot the device will go offline; start checking for comeback
        stopOtaPolling();
        updateOtaOverlay('Device restarting...', 100);
        startRebootCheck();
    }
}

function startRebootCheck() {
    if (otaRebootCheckTimer) return;
    
    let attempts = 0;
    const maxAttempts = 60; // Check for up to 60 seconds
    
    otaRebootCheckTimer = setInterval(async () => {
        attempts++;
        try {
            const status = await apiGet('status');
            // Device is back online!
            clearInterval(otaRebootCheckTimer);
            otaRebootCheckTimer = null;
            
            updateOtaOverlay('Update complete!', 100);
            setTimeout(() => {
                hideOtaOverlay();
                showToast('Firmware updated successfully!', 'success');
                loadOtaInfo();
                loadStatus();
            }, 1500);
        } catch (e) {
            // Still offline
            updateOtaOverlay(`Waiting for device... (${attempts}s)`, 100);
            
            if (attempts >= maxAttempts) {
                clearInterval(otaRebootCheckTimer);
                otaRebootCheckTimer = null;
                updateOtaOverlay('Device not responding. Please refresh page.', 100);
            }
        }
    }, 1000);
}

// ============================================================================
// Sensor Data Polling
// ============================================================================

async function loadSensors() {
    try {
        const data = await apiGet('sensors');
        sensors = data;
        
        // Apply custom order
        applySensorOrder();
        
        updateSensorDisplay();
        
        // Calculate summary
        const summary = {
            avg: sensorManager_getAverage(),
            min: sensorManager_getMin(),
            max: sensorManager_getMax(),
            alarms: sensorManager_getAlarmCount()
        };
        updateSummary(summary);
        updateChartData();
        
        // Load history on first load
        if (!chartDataLoaded) {
            await loadChartHistory();
        }
    } catch (error) {
        console.error('Error loading sensors:', error);
    }
}

function sensorManager_getAverage() {
    let sum = 0, count = 0;
    sensors.forEach(s => {
        if (s.connected && s.temperature !== null) {
            sum += s.temperature;
            count++;
        }
    });
    return count > 0 ? sum / count : null;
}

function sensorManager_getMin() {
    let min = null;
    sensors.forEach(s => {
        if (s.connected && s.temperature !== null) {
            if (min === null || s.temperature < min) min = s.temperature;
        }
    });
    return min;
}

function sensorManager_getMax() {
    let max = null;
    sensors.forEach(s => {
        if (s.connected && s.temperature !== null) {
            if (max === null || s.temperature > max) max = s.temperature;
        }
    });
    return max;
}

function sensorManager_getAlarmCount() {
    return sensors.filter(s => s.alarm === 'low' || s.alarm === 'high').length;
}

// ============================================================================
// Update Banner
// ============================================================================

function checkForUpdates() {
    // Check for updates in background (shown on dashboard)
    loadOtaInfo().then(() => {
        showUpdateBannerIfAvailable();
    }).catch(err => {
        console.error('Failed to check for updates:', err);
    });
}

function showUpdateBannerIfAvailable() {
    // Check if update is available from cached OTA info
    apiGet('ota/info').then(info => {
        if (info.updateAvailable && info.latest && info.latest.tag) {
            showUpdateBanner(info.latest.tag);
        }
    }).catch(() => {
        // Silently ignore errors
    });
}

function showUpdateBanner(version) {
    // Check if this specific version was dismissed
    const dismissedVersion = safeLocalStorageGet('updateBannerDismissedVersion');
    if (dismissedVersion === version) {
        console.log('Banner dismissed for version:', version);
        return;
    }
    
    console.log('Showing update banner for version:', version);
    const banner = document.getElementById('updateBanner');
    const versionEl = document.getElementById('updateVersion');
    
    if (!banner || !versionEl) {
        console.error('Banner elements not found!');
        return;
    }
    
    versionEl.textContent = version;
    banner.style.display = 'block';
}

function dismissUpdateBanner() {
    const banner = document.getElementById('updateBanner');
    const version = document.getElementById('updateVersion').textContent;
    banner.style.display = 'none';
    // Store the dismissed version so banner reappears if a newer version becomes available
    safeLocalStorageSet('updateBannerDismissedVersion', version);
    // Clean up old key
    safeLocalStorageRemove('updateBannerDismissed');
}

function goToUpdatePage() {
    // Switch to settings tab
    switchToTab('settings');
    // Scroll to OTA section if needed
    setTimeout(() => {
        const otaSection = document.querySelector('#tab-settings .section:last-of-type');
        if (otaSection) {
            otaSection.scrollIntoView({ behavior: 'smooth', block: 'start' });
        }
    }, 100);
}

// ============================================================================
// Tab Navigation
// ============================================================================

function switchToTab(tabId) {
    const tabs = document.querySelectorAll('.tab');
    const tabButton = document.querySelector(`.tab[data-tab="${tabId}"]`);
    
    if (tabButton) {
        tabButton.click();
    }
}

function initTabs() {
    // Get tabs from both desktop nav and mobile nav
    const tabs = document.querySelectorAll('.tab');
    
    tabs.forEach(tab => {
        tab.addEventListener('click', () => {
            const tabId = tab.dataset.tab;
            switchToTab(tabId);
        });
    });
}

function switchToTab(tabId) {
    const tabs = document.querySelectorAll('.tab');
    
    // Update ALL tab buttons (both desktop and mobile)
    tabs.forEach(t => {
        if (t.dataset.tab === tabId) {
            t.classList.add('active');
        } else {
            t.classList.remove('active');
        }
    });
    
    // Update page title based on active tab
    updatePageTitle(tabId);
    
    // Load OTA info when switching to settings tab
    if (tabId === 'settings') {
        loadOtaInfo();
    }
    
    // Fade out current content
    const currentContent = document.querySelector('.tab-content.active');
    if (currentContent) {
        currentContent.style.opacity = '0';
        currentContent.style.transform = 'translateY(10px)';
        
        setTimeout(() => {
            currentContent.classList.remove('active');
            
            // Fade in new content
            const newContent = document.getElementById(`tab-${tabId}`);
            newContent.classList.add('active');
            
            // Trigger reflow
            newContent.offsetHeight;
            
            newContent.style.opacity = '1';
            newContent.style.transform = 'translateY(0)';
            
            // Scroll to top on mobile
            if (window.innerWidth < 768) {
                window.scrollTo({ top: 0, behavior: 'smooth' });
            }
        }, 150);
    }
}

function updatePageTitle(tabId) {
    // Only update title on mobile
    if (window.innerWidth >= 768) return;
    
    const titleEl = document.getElementById('pageTitle');
    const titles = {
        'dashboard': 'Temperature Monitor',
        'sensors': 'Sensors',
        'calibration': 'Calibration',
        'settings': 'Settings'
    };
    titleEl.textContent = titles[tabId] || 'Temperature Monitor';
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
    
    // MQTT status
    const mqttStatus = document.getElementById('mqttStatus');
    if (mqttStatus) {
        mqttStatus.classList.toggle('connected', systemStatus.mqtt.connected);
    }
    
    // System info
    const uptime = document.getElementById('uptime');
    const ipAddress = document.getElementById('ipAddress');
    const wifiSignal = document.getElementById('wifiSignal');
    const freeHeap = document.getElementById('freeHeap');
    const firmware = document.getElementById('firmware');
    const sensorCount = document.getElementById('sensorCount');
    
    if (uptime) uptime.textContent = formatUptime(systemStatus.device.uptime);
    if (ipAddress) ipAddress.textContent = systemStatus.wifi.ip;
    if (wifiSignal) wifiSignal.textContent = `${systemStatus.wifi.signal}%`;
    if (freeHeap) freeHeap.textContent = formatBytes(systemStatus.device.freeHeap);
    if (firmware) firmware.textContent = systemStatus.device.firmware;
    if (sensorCount) sensorCount.textContent = systemStatus.sensors.count;
}

async function loadConfigurations() {
    try {
        const wifiConfig = await apiGet('config/wifi');
        populateWifiForm(wifiConfig);
        
        const mqttConfig = await apiGet('config/mqtt');
        populateMqttForm(mqttConfig);
        
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
        grid.innerHTML = '<div class="loading">No sensors found</div>';
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
        list.innerHTML = '<div class="loading">No sensors found</div>';
        return;
    }
    
    list.innerHTML = sensors.map((sensor, index) => `
        <div class="sensor-list-item" draggable="true" data-address="${sensor.address}">
            <div class="drag-handle" title="Drag to reorder">⋮⋮</div>
            <div class="sensor-list-info">
                <div class="sensor-list-name">${escapeHtml(sensor.name || `Sensor ${index + 1}`)}</div>
                <div class="sensor-list-details">
                    <strong>${formatTemp(sensor.temperature)}</strong> | Offset: ${formatOffset(sensor.calibrationOffset)}<br>
                    ${sensor.address} | Thresholds: ${sensor.thresholdLow}°C - ${sensor.thresholdHigh}°C
                    ${sensor.alertEnabled ? '' : '| Alerts disabled'}
                </div>
            </div>
            <button class="btn btn-success" style="margin-right: 8px;" onclick="calibrateSensor(${index})">Calibrate</button>
            <button class="btn btn-warning" style="margin-right: 8px;" onclick="resetSensorCalibration(${index})">Reset</button>
            <button class="btn btn-secondary" onclick="editSensor(${index})">Edit</button>
        </div>
    `).join('');
    
    // Attach drag-and-drop event listeners
    attachDragListeners();
}

function updateCalibrationList() {
    const list = document.getElementById('offsetList');
    
    if (sensors.length === 0) {
        list.innerHTML = '<div class="loading">No sensors found</div>';
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
        if (!sensor.connected || sensor.temperature === null || sensor.temperature === -127) continue;
        
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

async function savePinnedSensor() {
    const select = document.getElementById('pinnedSensorSelect');
    pinnedSensorAddress = select.value || null;
    
    closePinDialog();
    
    try {
        // Save to server config
        await apiPut('config/system', {
            pinnedSensorAddress: pinnedSensorAddress || ''
        });
        
        // Force summary update
        if (sensors.length > 0) {
            updateSummary({ alarms: document.getElementById('alarmCount').textContent });
        }
        
        showToast('Pinned sensor updated', 'success');
    } catch (error) {
        console.error('Error saving pinned sensor:', error);
        showToast('Failed to save pinned sensor', 'error');
    }
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
    
    document.getElementById('editSensorAddress').value = sensor.address;
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
    const address = document.getElementById('editSensorAddress').value;
    
    // Find current backend index by address
    // Backend sensor order may differ from frontend after drag-and-drop
    let response;
    try {
        response = await fetch('/api/sensors');
        const backendSensors = await response.json();
        const backendIndex = backendSensors.findIndex(s => s.address === address);
        
        if (backendIndex === -1) {
            showToast('Sensor not found', 'error');
            return;
        }
        
        const data = {
            index: backendIndex,
            name: document.getElementById('editSensorName').value,
            thresholdLow: parseFloat(document.getElementById('editThresholdLow').value),
            thresholdHigh: parseFloat(document.getElementById('editThresholdHigh').value),
            alertEnabled: document.getElementById('editAlertEnabled').checked
        };
        
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

async function calibrateSensor(index) {
    const sensor = sensors[index];
    if (!sensor) return;
    
    const refTemp = prompt(`Calibrate "${sensor.name || 'Sensor ' + (index + 1)}"\n\nCurrent: ${formatTemp(sensor.temperature)}\nOffset: ${formatOffset(sensor.calibrationOffset)}\n\nEnter the reference temperature (°C):`);
    
    if (refTemp === null) return; // User cancelled
    
    const refTempNum = parseFloat(refTemp);
    if (isNaN(refTempNum)) {
        showToast('Invalid temperature', 'warning');
        return;
    }
    
    try {
        await apiPost(`sensors/${index}/calibrate`, { referenceTemp: refTempNum });
        showToast(`${sensor.name || 'Sensor'} calibrated`, 'success');
        setTimeout(() => loadSensors(), 500); // Reload to show new offset
    } catch (error) {
        showToast('Error during calibration', 'error');
    }
}

async function resetSensorCalibration(index) {
    const sensor = sensors[index];
    if (!sensor) return;
    
    if (!confirm(`Reset calibration for "${sensor.name || 'Sensor ' + (index + 1)}"?`)) {
        return;
    }
    
    try {
        await apiPost(`sensors/${index}/calibrate`, { referenceTemp: sensor.rawTemperature });
        showToast('Calibration reset', 'success');
        setTimeout(() => loadSensors(), 500);
    } catch (error) {
        showToast('Error resetting calibration', 'error');
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
    document.getElementById('mqttPublishOnChange').checked = config.publishOnChange !== undefined ? config.publishOnChange : true;
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
        publishOnChange: document.getElementById('mqttPublishOnChange').checked,
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
    document.getElementById('readInterval').value = config.readInterval || 5;
    document.getElementById('celsiusUnits').checked = config.celsiusUnits;
    
    // Load pinned sensor from server config
    pinnedSensorAddress = config.pinnedSensorAddress || null;
    if (pinnedSensorAddress === '') pinnedSensorAddress = null;
    
    // Update frontend poll interval based on ESP32 read interval
    updatePollInterval(config.readInterval || 5);
}

function updatePollInterval(readInterval) {
    // Set frontend poll to match ESP32 read interval (in milliseconds)
    SENSOR_POLL_INTERVAL = readInterval * 1000;
    
    // Restart sensor polling with new interval
    if (sensorPollTimer) {
        clearInterval(sensorPollTimer);
        sensorPollTimer = setInterval(loadSensors, SENSOR_POLL_INTERVAL);
    }
}

async function saveSystemConfig() {
    let readInterval = parseInt(document.getElementById('readInterval').value);
    
    // Validate read interval (minimum 5 seconds, maximum 300)
    if (isNaN(readInterval) || readInterval < 5) {
        readInterval = 5;
        document.getElementById('readInterval').value = 5;
        showToast('Read interval must be at least 5 seconds', 'warning');
    } else if (readInterval > 300) {
        readInterval = 300;
        document.getElementById('readInterval').value = 300;
        showToast('Read interval cannot exceed 300 seconds', 'warning');
    }
    
    const data = {
        deviceName: document.getElementById('deviceNameInput').value,
        readInterval: readInterval,
        celsiusUnits: document.getElementById('celsiusUnits').checked
    };
    
    try {
        await apiPost('config/system', data);
        showToast('System configuration saved', 'success');
        
        // Update frontend poll interval
        updatePollInterval(readInterval);
        
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

function toggleCollapse(header) {
    const section = header.closest('.section');
    section.classList.toggle('collapsed');
}

// Set collapsed states based on connection status
function restoreCollapsedStates() {
    document.querySelectorAll('.section.collapsible').forEach(section => {
        const header = section.querySelector('.section-header.clickable h2');
        if (header) {
            const sectionTitle = header.textContent;
            
            // WiFi: collapse if connected, expand if not connected
            if (sectionTitle.includes('WiFi')) {
                if (systemStatus?.wifi?.status === 'connected') {
                    section.classList.add('collapsed');
                } else {
                    section.classList.remove('collapsed');
                }
            }
            // MQTT: collapse if connected OR if disabled (don't need to configure it)
            else if (sectionTitle.includes('MQTT')) {
                if (systemStatus?.mqtt?.connected || !systemStatus?.mqtt?.enabled) {
                    section.classList.add('collapsed');
                } else {
                    section.classList.remove('collapsed');
                }
            }
        }
    });
}

// ============================================================================
// Chart Functions
// ============================================================================

function initChart() {
    const select = document.getElementById('chartSensor');
    if (!select) return;
    
    select.addEventListener('change', (e) => {
        chartSelectedSensor = e.target.value;
        drawChart();
    });
}

// Load initial chart history from ESP32 API (last ~1 minute of data)
async function loadChartHistory() {
    if (chartDataLoaded) return; // Only load once
    
    try {
        const sensorData = await apiGet('sensors');
        if (!sensorData || sensorData.length === 0) return;
        
        const now = Date.now();
        let hasAnyData = false;
        
        // Load history for each sensor
        const promises = sensorData.map(async (sensor, index) => {
            try {
                const history = await apiGet(`history/${index}`);
                if (!history || history.length === 0) return;
                
                // Calculate intervals based on temp changes (ESP32 logic: 1min if changed, 5min if stable)
                const intervals = [0]; // First point has no interval before it
                for (let i = 1; i < history.length; i++) {
                    const prevTemp = Math.round(history[i - 1] * 10) / 10;
                    const currTemp = Math.round(history[i] * 10) / 10;
                    const tempDiff = Math.abs(currTemp - prevTemp);
                    intervals.push(tempDiff >= 0.1 ? 60 * 1000 : 5 * 60 * 1000);
                }

                // Use lastReadMs from sensor to get accurate last reading time
                const lastReadTime = now - (sensor.lastReadMs || 0);
                
                // Calculate cumulative time backwards from 'now'
                const totalTime = intervals.reduce((sum, interval) => sum + interval, 0);
                let accumulatedTime = lastReadTime - totalTime;
                
                // Convert history array to timestamped data (Oldest -> Newest)
                const points = [];
                for (let i = 0; i < history.length; i++) {
                    const temp = Math.round(history[i] * 10) / 10; // Round to 0.1°C
                    // Skip invalid temperatures
                    if (temp === -127 || temp < -55 || temp > 125) continue;
                    
                    points.push({ time: accumulatedTime, temp });
                    if (i < intervals.length - 1) {
                        accumulatedTime += intervals[i + 1];
                    }
                }
                
                if (points.length > 0) {
                    chartData[sensor.address] = points;
                    hasAnyData = true;
                }
            } catch (err) {
                // Ignore history load errors
            }
        });
        
        await Promise.all(promises);
        chartDataLoaded = true;
        
        if (hasAnyData) {
            updateChartSensorList();
            drawChart();
        }
    } catch (error) {
        // Ignore chart history errors
    }
}

function updateChartData() {
    if (sensors.length === 0) return;
    
    const now = Date.now();
    const maxAge = 150 * 60 * 1000; // Keep 150 minutes (matches ESP32 history: 30 points × 5min max)
    const minInterval = 60 * 1000; // Minimum 1 minute between points
    const minTempChange = 0.1; // Minimum 0.1°C change to store
    
    let hasUpdates = false;
    
    sensors.forEach(sensor => {
        if (!sensor.connected || sensor.temperature === -127) return;
        
        if (!chartData[sensor.address]) {
            chartData[sensor.address] = [];
        }
        
        const data = chartData[sensor.address];
        const roundedTemp = Math.round(sensor.temperature * 10) / 10;
        
        // Store if: first point, OR 5 min passed (stable temp), OR (1 min passed AND temp changed)
        let shouldStore = false;
        
        if (data.length === 0) {
            shouldStore = true; // First point - always store
        } else {
            const lastPoint = data[data.length - 1];
            const timeSinceLastPoint = now - lastPoint.time;
            const tempDiff = Math.abs(roundedTemp - lastPoint.temp);
            
            // Store if: 5 minutes passed (keep stable temps visible) OR (1 min + temp changed)
            if (timeSinceLastPoint >= 5 * 60 * 1000) {
                shouldStore = true; // Force point every 5 minutes for stable temps
            } else if (timeSinceLastPoint >= minInterval && tempDiff >= minTempChange) {
                shouldStore = true; // Temp changed significantly
            }
        }
        
        if (shouldStore) {
            data.push({ time: now, temp: roundedTemp });
            hasUpdates = true;
        }
        
        // Remove old data
        while (data.length > 0 && now - data[0].time > maxAge) {
            data.shift();
            hasUpdates = true;
        }
    });
    
    // Redraw chart periodically (every 30 sec) or when data changes
    const timeSinceLastRedraw = now - lastChartRedraw;
    if (hasUpdates || timeSinceLastRedraw >= 30000) {
        if (hasUpdates) {
            updateChartSensorList();
        }
        drawChart();
        lastChartRedraw = now;
    }
}

function updateChartSensorList() {
    const select = document.getElementById('chartSensor');
    if (!select) return;
    
    const currentValue = chartSelectedSensor; // Use the tracked variable, not DOM
    
    // Rebuild options from chartData (works even when sensors array is not populated yet)
    let html = '<option value="">All Sensors</option>';
    
    const addresses = Object.keys(chartData);
    addresses.forEach(address => {
        if (chartData[address] && chartData[address].length > 0) {
            // Try to find sensor name from sensors array
            const sensor = sensors.find(s => s.address === address);
            const name = sensor ? escapeHtml(sensor.name || 'Sensor') : 'Sensor';
            html += `<option value="${address}"${address === currentValue ? ' selected' : ''}>${name}</option>`;
        }
    });
    
    select.innerHTML = html;
    
    // Verify selection is still valid, otherwise reset to "All Sensors"
    if (currentValue && !addresses.includes(currentValue)) {
        chartSelectedSensor = '';
        select.value = '';
    }
}

function drawChart() {
    const svg = document.getElementById('tempChart');
    if (!svg) return;
    
    // Responsive sizing - use smaller width on mobile for better proportions
    const isMobile = window.innerWidth < 768;
    const width = isMobile ? 400 : 800;
    const height = isMobile ? 400 : 300;
    const padding = { top: 20, right: isMobile ? 60 : 80, bottom: 30, left: 50 };
    const chartWidth = width - padding.left - padding.right;
    const chartHeight = height - padding.top - padding.bottom;
    
    // Update SVG viewBox
    svg.setAttribute('viewBox', `0 0 ${width} ${height}`);
    
    // Clear
    svg.innerHTML = '';
    
    // Get data to display
    let dataToPlot = [];
    let commonStartTime = 0;
    
    if (chartSelectedSensor) {
        // Single sensor: show all its data
        const data = chartData[chartSelectedSensor];
        if (data && data.length > 0) {
            const sensor = sensors.find(s => s.address === chartSelectedSensor);
            const color = '#3b82f6';
            dataToPlot.push({ sensor, data, color });
        }
    } else {
        // All sensors: find common time range (latest start time)
        const colors = ['#3b82f6', '#22c55e', '#f59e0b', '#ef4444', '#8b5cf6', '#06b6d4'];
        let colorIndex = 0;
        
        // Plot all data that exists in chartData, even if sensors array is empty
        Object.keys(chartData).forEach(address => {
            const data = chartData[address];
            if (data && data.length > 0) {
                const sensor = sensors.find(s => s.address === address) || { address, name: 'Sensor' };
                const sensorStartTime = data[0].time;
                if (sensorStartTime > commonStartTime) {
                    commonStartTime = sensorStartTime;
                }
            }
        });
        
        // Filter each sensor's data to only show from common start time
        Object.keys(chartData).forEach(address => {
            const data = chartData[address];
            if (data && data.length > 0) {
                const filteredData = data.filter(point => point.time >= commonStartTime);
                if (filteredData.length > 0) {
                    const sensor = sensors.find(s => s.address === address) || { address, name: 'Sensor' };
                    dataToPlot.push({ 
                        sensor, 
                        data: filteredData, 
                        color: colors[colorIndex % colors.length] 
                    });
                    colorIndex++;
                }
            }
        });
    }
    
    if (dataToPlot.length === 0) {
        const text = document.createElementNS('http://www.w3.org/2000/svg', 'text');
        text.setAttribute('x', width / 2);
        text.setAttribute('y', height / 2);
        text.setAttribute('text-anchor', 'middle');
        text.setAttribute('fill', '#64748b');
        text.textContent = 'No data yet';
        svg.appendChild(text);
        return;
    }
    
    // Find min/max
    let minTime = Infinity, maxTime = -Infinity;
    let minTemp = Infinity, maxTemp = -Infinity;
    
    dataToPlot.forEach(({ data }) => {
        data.forEach(point => {
            if (point.time < minTime) minTime = point.time;
            if (point.time > maxTime) maxTime = point.time;
            if (point.temp < minTemp) minTemp = point.temp;
            if (point.temp > maxTemp) maxTemp = point.temp;
        });
    });
    
    // Add padding to temp range
    const tempRange = maxTemp - minTemp;
    if (tempRange < 1) {
        minTemp -= 0.5;
        maxTemp += 0.5;
    } else {
        minTemp -= tempRange * 0.1;
        maxTemp += tempRange * 0.1;
    }
    
    const timeRange = maxTime - minTime || 1;
    
    // Scales
    const xScale = (time) => padding.left + ((time - minTime) / timeRange) * chartWidth;
    const yScale = (temp) => padding.top + chartHeight - ((temp - minTemp) / (maxTemp - minTemp)) * chartHeight;
    
    // Background
    const bg = document.createElementNS('http://www.w3.org/2000/svg', 'rect');
    bg.setAttribute('x', padding.left);
    bg.setAttribute('y', padding.top);
    bg.setAttribute('width', chartWidth);
    bg.setAttribute('height', chartHeight);
    bg.setAttribute('fill', '#0f172a');
    bg.setAttribute('stroke', '#334155');
    svg.appendChild(bg);
    
    // Grid lines (horizontal)
    const tempStep = Math.ceil((maxTemp - minTemp) / 5);
    for (let i = 0; i <= 5; i++) {
        const temp = minTemp + (maxTemp - minTemp) * (i / 5);
        const y = yScale(temp);
        
        const line = document.createElementNS('http://www.w3.org/2000/svg', 'line');
        line.setAttribute('x1', padding.left);
        line.setAttribute('y1', y);
        line.setAttribute('x2', padding.left + chartWidth);
        line.setAttribute('y2', y);
        line.setAttribute('stroke', '#334155');
        line.setAttribute('stroke-width', '1');
        svg.appendChild(line);
        
        const label = document.createElementNS('http://www.w3.org/2000/svg', 'text');
        label.setAttribute('x', padding.left - 10);
        label.setAttribute('y', y + 4);
        label.setAttribute('text-anchor', 'end');
        label.setAttribute('fill', '#64748b');
        label.setAttribute('font-size', '12');
        label.textContent = temp.toFixed(1) + '°';
        svg.appendChild(label);
    }
    
    // Draw lines
    dataToPlot.forEach(({ sensor, data, color }) => {
        if (data.length === 0) return;
        
        // Draw a dot if only one point
        if (data.length === 1) {
            const x = xScale(data[0].time);
            const y = yScale(data[0].temp);
            const circle = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
            circle.setAttribute('cx', x);
            circle.setAttribute('cy', y);
            circle.setAttribute('r', '4');
            circle.setAttribute('fill', color);
            svg.appendChild(circle);
            return;
        }
        
        let pathData = '';
        data.forEach((point, i) => {
            const x = xScale(point.time);
            const y = yScale(point.temp);
            pathData += (i === 0 ? 'M' : 'L') + x + ',' + y + ' ';
        });
        
        const path = document.createElementNS('http://www.w3.org/2000/svg', 'path');
        path.setAttribute('d', pathData);
        path.setAttribute('fill', 'none');
        path.setAttribute('stroke', color);
        path.setAttribute('stroke-width', '2');
        svg.appendChild(path);
    });
    
    // Legend
    if (!chartSelectedSensor && dataToPlot.length > 1) {
        dataToPlot.forEach(({ sensor, color }, i) => {
            const y = padding.top + i * 20;
            
            const rect = document.createElementNS('http://www.w3.org/2000/svg', 'rect');
            rect.setAttribute('x', width - padding.right + 10);
            rect.setAttribute('y', y - 8);
            rect.setAttribute('width', 12);
            rect.setAttribute('height', 12);
            rect.setAttribute('fill', color);
            svg.appendChild(rect);
            
            const label = document.createElementNS('http://www.w3.org/2000/svg', 'text');
            label.setAttribute('x', width - padding.right + 26);
            label.setAttribute('y', y + 3);
            label.setAttribute('fill', '#cbd5e1');
            label.setAttribute('font-size', '11');
            label.textContent = sensor.name || 'Sensor';
            svg.appendChild(label);
        });
    }
    
    // Time labels
    const now = new Date();
    const labelCount = 4;
    for (let i = 0; i <= labelCount; i++) {
        const time = minTime + (timeRange * i / labelCount);
        const x = xScale(time);
        const date = new Date(time);
        const timeStr = date.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit', hour12: false });
        
        const label = document.createElementNS('http://www.w3.org/2000/svg', 'text');
        label.setAttribute('x', x);
        label.setAttribute('y', height - 8);
        label.setAttribute('text-anchor', 'middle');
        label.setAttribute('fill', '#64748b');
        label.setAttribute('font-size', '12');
        label.textContent = timeStr;
        svg.appendChild(label);
    }
}

// ============================================================================
// Sensor Ordering
// ============================================================================

function loadSensorOrder() {
    const saved = safeLocalStorageGet('sensorOrder');
    if (saved) {
        try {
            sensorOrder = JSON.parse(saved);
        } catch (_) {
            sensorOrder = [];
        }
    }
}

function saveSensorOrder() {
    sensorOrder = sensors.map(s => s.address);
    safeLocalStorageSet('sensorOrder', JSON.stringify(sensorOrder));
}

function applySensorOrder() {
    if (sensorOrder.length === 0) {
        loadSensorOrder();
    }
    
    if (sensorOrder.length === 0) {
        // First time - save current order
        saveSensorOrder();
        return;
    }
    
    // Sort sensors by saved order
    sensors.sort((a, b) => {
        const indexA = sensorOrder.indexOf(a.address);
        const indexB = sensorOrder.indexOf(b.address);
        
        // If address not in order, put at end
        if (indexA === -1) return 1;
        if (indexB === -1) return -1;
        
        return indexA - indexB;
    });
    
    // Add any new sensors to the end of order
    sensors.forEach(sensor => {
        if (!sensorOrder.includes(sensor.address)) {
            sensorOrder.push(sensor.address);
        }
    });
    
    // Remove sensors that no longer exist
    sensorOrder = sensorOrder.filter(addr => 
        sensors.some(s => s.address === addr)
    );
}

let draggedElement = null;

function attachDragListeners() {
    const items = document.querySelectorAll('.sensor-list-item');
    
    items.forEach(item => {
        item.addEventListener('dragstart', handleDragStart);
        item.addEventListener('dragover', handleDragOver);
        item.addEventListener('drop', handleDrop);
        item.addEventListener('dragend', handleDragEnd);
    });
}

function handleDragStart(e) {
    draggedElement = this;
    this.style.opacity = '0.4';
    e.dataTransfer.effectAllowed = 'move';
}

function handleDragOver(e) {
    if (e.preventDefault) {
        e.preventDefault();
    }
    e.dataTransfer.dropEffect = 'move';
    return false;
}

function handleDrop(e) {
    if (e.stopPropagation) {
        e.stopPropagation();
    }
    
    if (draggedElement !== this) {
        // Get addresses
        const draggedAddr = draggedElement.getAttribute('data-address');
        const targetAddr = this.getAttribute('data-address');
        
        // Find indices in sensors array
        const draggedIdx = sensors.findIndex(s => s.address === draggedAddr);
        const targetIdx = sensors.findIndex(s => s.address === targetAddr);
        
        // Reorder sensors array
        const [removed] = sensors.splice(draggedIdx, 1);
        sensors.splice(targetIdx, 0, removed);
        
        // Save new order
        saveSensorOrder();
        
        // Update displays
        updateSensorDisplay();
    }
    
    return false;
}

function handleDragEnd(e) {
    this.style.opacity = '1';
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
