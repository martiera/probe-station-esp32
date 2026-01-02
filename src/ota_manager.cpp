/*
 * ESP32 Temperature Monitoring System
 * GitHub Releases OTA Manager
 * 
 * Uses WiFiClientSecure + ESP-IDF OTA APIs for memory-efficient HTTPS OTA
 */

#include "ota_manager.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <cstring>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "mqtt_client.h"
#include "web_server.h"
#include "display_manager.h"

OTAManager otaManager;

namespace {
constexpr uint32_t RELEASE_INFO_TTL_MS = 5UL * 60UL * 1000UL; // 5 min
constexpr uint32_t AUTO_CHECK_INTERVAL_MS = 24UL * 60UL * 60UL * 1000UL; // 24 hours
constexpr uint32_t HTTP_TIMEOUT_MS = 15000;
constexpr uint8_t HTTP_MAX_RETRIES = 3; // Retry up to 3 times
constexpr uint32_t HTTP_RETRY_DELAY_MS = 2000; // Initial retry delay (exponential backoff)

// Parse version string (e.g., "v1.0.7" or "1.0.7") into comparable integer
// Returns: major*10000 + minor*100 + patch
int parseVersionNumber(const String& version) {
    String v = version;
    if (v.startsWith("v") || v.startsWith("V")) {
        v = v.substring(1);
    }
    
    int major = 0, minor = 0, patch = 0;
    int dotCount = 0;
    int value = 0;
    
    for (size_t i = 0; i < v.length(); i++) {
        char c = v.charAt(i);
        if (c >= '0' && c <= '9') {
            value = value * 10 + (c - '0');
        } else if (c == '.') {
            if (dotCount == 0) major = value;
            else if (dotCount == 1) minor = value;
            value = 0;
            dotCount++;
        } else {
            // Stop at first non-numeric, non-dot character
            break;
        }
    }
    
    // Last segment is patch
    if (dotCount == 2) patch = value;
    else if (dotCount == 1) minor = value;
    else if (dotCount == 0) major = value;
    
    return major * 10000 + minor * 100 + patch;
}

// URL parsing helper (from ESP_OTA_GitHub approach)
struct UrlDetails {
    String proto;
    String host;
    int port;
    String path;
};

UrlDetails parseUrl(const String& url) {
    UrlDetails details;
    String u = url;
    
    if (u.startsWith("http://")) {
        details.proto = "http://";
        details.port = 80;
        u.replace("http://", "");
    } else {
        details.proto = "https://";
        details.port = 443;
        u.replace("https://", "");
    }
    
    int firstSlash = u.indexOf('/');
    if (firstSlash > 0) {
        details.host = u.substring(0, firstSlash);
        details.path = u.substring(firstSlash);
    } else {
        details.host = u;
        details.path = "/";
    }
    
    return details;
}

// Resolve GitHub redirects using raw socket connection (memory efficient)
// This follows the ESP_OTA_GitHub approach but with heap allocation
bool resolveRedirects(const String& startUrl, String& finalUrl, String& error) {
    finalUrl = startUrl;
    
    for (int redirectCount = 0; redirectCount < 10; redirectCount++) {
        UrlDetails url = parseUrl(finalUrl);
        
        Serial.printf("[OTA] Redirect %d: %s%s%s\n", redirectCount + 1, 
            url.proto.c_str(), url.host.c_str(), url.path.c_str());
        Serial.printf("[OTA] Free heap before connect: %u\n", ESP.getFreeHeap());
        
        // Allocate on heap to avoid stack overflow
        WiFiClientSecure* client = new WiFiClientSecure();
        if (!client) {
            error = "Out of memory for SSL client";
            return false;
        }
        
        client->setInsecure(); // Skip certificate validation to save memory
        client->setTimeout(30); // 30 second timeout
        
        Serial.printf("[OTA] Connecting to %s:%d...\n", url.host.c_str(), url.port);
        
        if (!client->connect(url.host.c_str(), url.port)) {
            error = "Connection to " + url.host + " failed";
            delete client;
            return false;
        }
        
        Serial.println("[OTA] Connected, sending request...");
        
        // Send minimal HTTP request
        client->print(String("GET ") + url.path + " HTTP/1.1\r\n" +
                     "Host: " + url.host + "\r\n" +
                     "User-Agent: probe-station-esp32\r\n" +
                     "Connection: close\r\n\r\n");
        
        bool foundLocation = false;
        String newLocation;
        int httpCode = 0;
        
        // Read headers only
        uint32_t timeout = millis() + 30000;
        while (client->connected() && millis() < timeout) {
            String line = client->readStringUntil('\n');
            line.trim();
            
            // Check HTTP status first
            if (line.startsWith("HTTP/1.")) {
                int spacePos = line.indexOf(' ');
                if (spacePos > 0) {
                    httpCode = line.substring(spacePos + 1, spacePos + 4).toInt();
                    Serial.printf("[OTA] HTTP status: %d\n", httpCode);
                }
            }
            
            // Check for Location header (case-insensitive)
            if (line.startsWith("Location: ") || line.startsWith("location: ")) {
                newLocation = line.substring(10);
                newLocation.trim();
                foundLocation = true;
                Serial.printf("[OTA] Found Location: %s\n", newLocation.c_str());
            }
            
            // Empty line means end of headers
            if (line.length() == 0) {
                break;
            }
        }
        
        // Properly close connection before stopping to avoid TCP assertion
        // Issue: tcp_recved called on listen-pcb when stop() is called
        if (client->connected()) {
            // Read and discard any remaining data to properly close connection
            while (client->available()) {
                client->read();
            }
        }
        client->stop();
        delete client;
        client = nullptr;
        
        Serial.printf("[OTA] Free heap after disconnect: %u\n", ESP.getFreeHeap());
        
        // Check if we got a success code (final URL)
        if (httpCode == 200 || httpCode == 206) {
            Serial.printf("[OTA] Final URL reached (HTTP %d)\n", httpCode);
            return true;
        }
        
        // Check for redirect
        if (httpCode >= 300 && httpCode < 400 && foundLocation && newLocation.length() > 0) {
            // Handle relative vs absolute URLs
            if (newLocation.startsWith("http://") || newLocation.startsWith("https://")) {
                finalUrl = newLocation;
            } else {
                // Relative URL - same host
                finalUrl = url.proto + url.host + newLocation;
            }
            Serial.printf("[OTA] Redirecting to: %s\n", finalUrl.c_str());
            delay(500); // Delay between redirects to allow memory cleanup
            continue;
        }
        
        // Not a redirect and not success
        if (httpCode > 0) {
            error = "HTTP " + String(httpCode);
        } else {
            error = "No HTTP response";
        }
        return false;
    }
    
    error = "Too many redirects";
    return false;
}

String normalizeTagToVersion(String tag) {
    tag.trim();
    if (tag.startsWith("refs/tags/")) tag = tag.substring(strlen("refs/tags/"));
    if (tag.startsWith("v")) return tag;
    return "v" + tag;
}

String githubApiLatestReleaseUrl() {
    String url = "https://api.github.com/repos/";
    url += GITHUB_OWNER;
    url += "/";
    url += GITHUB_REPO;
    url += "/releases/latest";
    return url;
}

// ============================================================================

bool httpGetToString(const String& url, String& out, String& error, size_t maxBytes) {
    uint8_t retries = 0;
    uint32_t retryDelay = HTTP_RETRY_DELAY_MS;
    
    while (retries <= HTTP_MAX_RETRIES) {
        if (retries > 0) {
            Serial.printf("[OTA] Retry %d/%d after %dms delay...\n", 
                retries, HTTP_MAX_RETRIES, retryDelay);
            delay(retryDelay);
            retryDelay *= 2; // Exponential backoff
        }
        
        WiFiClientSecure client;
        client.setInsecure();
        client.setTimeout(HTTP_TIMEOUT_MS / 1000);

        HTTPClient http;
        http.setTimeout(HTTP_TIMEOUT_MS);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

        if (!http.begin(client, url)) {
            error = "HTTP begin failed";
            retries++;
            continue;
        }

        http.addHeader("User-Agent", "probe-station-esp32");
        int code = http.GET();
        
        if (code <= 0) {
            error = String("HTTP GET failed: ") + http.errorToString(code);
            http.end();
            
            // Check if it's a connection error that's worth retrying
            if (code == HTTPC_ERROR_CONNECTION_REFUSED || 
                code == HTTPC_ERROR_CONNECTION_LOST ||
                code == HTTPC_ERROR_NO_HTTP_SERVER) {
                retries++;
                continue;
            }
            
            // Other errors (timeout, etc.) - don't retry
            return false;
        }
        
        if (code == HTTP_CODE_FORBIDDEN || code == HTTP_CODE_TOO_MANY_REQUESTS) {
            // GitHub rate limiting - wait longer and retry
            error = String("HTTP ") + code + " (rate limited)";
            http.end();
            retries++;
            retryDelay = 5000; // Wait 5 seconds for rate limit
            continue;
        }
        
        if (code != HTTP_CODE_OK) {
            error = String("HTTP ") + code;
            http.end();
            return false;
        }

        // Success - read response
        WiFiClient* stream = http.getStreamPtr();
        out = "";
        out.reserve((maxBytes > 1024) ? 1024 : maxBytes);

        uint8_t buf[512];
        size_t total = 0;
        uint32_t startMs = millis();
        while (http.connected() && (millis() - startMs) < HTTP_TIMEOUT_MS) {
            int avail = stream->available();
            if (avail <= 0) {
                delay(1);
                continue;
            }
            int toRead = avail;
            if (toRead > (int)sizeof(buf)) toRead = sizeof(buf);
            int r = stream->readBytes(buf, toRead);
            if (r <= 0) break;

            size_t canTake = r;
            if (total + canTake > maxBytes) {
                canTake = maxBytes - total;
            }
            out.concat((const char*)buf, canTake);
            total += canTake;
            if (total >= maxBytes) break;
        }

        http.end();
        
        if (total == 0) {
            error = "No data received";
            retries++;
            continue;
        }
        
        // Success!
        return true;
    }
    
    // All retries exhausted
    Serial.printf("[OTA] All %d retries exhausted\n", HTTP_MAX_RETRIES);
    return false;
}

} // namespace

OTAManager::OTAManager() : _mux(portMUX_INITIALIZER_UNLOCKED), _task(nullptr) {
    _releaseMutex = xSemaphoreCreateMutex();
    _checkTask = nullptr;
    _progress.state = OTAState::IDLE;
    _progress.target = OTATarget::BOTH;
    _progress.progressPercent = 0;
    _progress.message[0] = '\0';
    _progress.error[0] = '\0';
}

void OTAManager::begin() {
    Serial.println("[OTA] OTA ready");
}

void OTAManager::checkOnBoot() {
    // Trigger initial check after delay to ensure network stack is fully ready
    _lastAutoCheck = millis() - AUTO_CHECK_INTERVAL_MS + 90000; // Check in 90 seconds
    Serial.println("[OTA] Boot check scheduled in 90 seconds");
}

bool OTAManager::isUpdateAvailable() const {
    if (_releaseMutex && xSemaphoreTake(_releaseMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        bool available = false;
        if (_release.tag.length() > 0) {
            int currentVersion = parseVersionNumber(String(FIRMWARE_VERSION));
            int latestVersion = parseVersionNumber(_release.tag);
            available = (latestVersion > currentVersion);
        }
        xSemaphoreGive(_releaseMutex);
        return available;
    }
    return false;
}

String OTAManager::getAvailableVersion() const {
    if (_releaseMutex && xSemaphoreTake(_releaseMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        String version = _release.tag;
        xSemaphoreGive(_releaseMutex);
        return version;
    }
    return String();
}

void OTAManager::getReleaseInfoCopy(OTAReleaseInfo& out) const {
    if (_releaseMutex && xSemaphoreTake(_releaseMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        out = _release;
        xSemaphoreGive(_releaseMutex);
        return;
    }
    out = OTAReleaseInfo{};
}

void OTAManager::setProgress(OTAState state, int progressPercent, const char* message, const char* error) {
    portENTER_CRITICAL(&_mux);
    _progress.state = state;
    _progress.progressPercent = progressPercent;
    if (message) {
        strlcpy(_progress.message, message, sizeof(_progress.message));
    } else {
        _progress.message[0] = '\0';
    }
    if (error) {
        strlcpy(_progress.error, error, sizeof(_progress.error));
    } else {
        _progress.error[0] = '\0';
    }
    portEXIT_CRITICAL(&_mux);
}

OTAProgress OTAManager::getProgress() const {
    OTAProgress copy;
    portENTER_CRITICAL(&_mux);
    copy = _progress;
    portEXIT_CRITICAL(&_mux);
    return copy;
}

bool OTAManager::isBusy() const {
    OTAProgress p = getProgress();
    return p.state == OTAState::CHECKING || p.state == OTAState::UPDATING_FIRMWARE || p.state == OTAState::UPDATING_SPIFFS || p.state == OTAState::REBOOTING;
}

OTAPartitionInfo OTAManager::getPartitionInfo() {
    OTAPartitionInfo info;
    
    const esp_partition_t* ota_partition = esp_ota_get_next_update_partition(NULL);
    if (ota_partition) {
        info.firmwarePartitionSize = ota_partition->size;
    }
    
    const esp_partition_t* spiffs_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
    if (spiffs_partition) {
        info.spiffsPartitionSize = spiffs_partition->size;
    }
    
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running) {
        esp_app_desc_t app_desc;
        if (esp_ota_get_partition_description(running, &app_desc) == ESP_OK) {
            info.currentFirmwareSize = running->size;
        }
    }
    
    info.freeHeap = ESP.getFreeHeap();
    info.minFreeHeap = ESP.getMinFreeHeap();
    
    return info;
}

void OTAManager::update() {
    uint32_t now = millis();
    
    // Daily auto-check for updates
    if (_lastAutoCheck == 0) {
        _lastAutoCheck = now;
        return;
    }
    
    if (now - _lastAutoCheck < AUTO_CHECK_INTERVAL_MS) {
        return;
    }
    
    if (isBusy()) {
        return;
    }
    
    _lastAutoCheck = now;
    String err;
    ensureReleaseInfoFresh(true, err);
    
    if (err.length() > 0) {
        Serial.printf("[OTA] Auto-check failed: %s\n", err.c_str());
    } else {
        Serial.println("[OTA] Daily auto-check initiated");
    }
}

bool OTAManager::ensureReleaseInfoFresh(bool force, String& error) {
    OTAProgress p = getProgress();
    if (p.state == OTAState::UPDATING_FIRMWARE || p.state == OTAState::UPDATING_SPIFFS || p.state == OTAState::REBOOTING) {
        error = "OTA busy";
        return false;
    }

    if (p.state == OTAState::CHECKING) {
        return true;
    }

    uint32_t fetchedAt = 0;
    if (_releaseMutex && xSemaphoreTake(_releaseMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        fetchedAt = _release.fetchedAtMs;
        xSemaphoreGive(_releaseMutex);
    }
    if (!force && fetchedAt != 0 && (millis() - fetchedAt) < RELEASE_INFO_TTL_MS) {
        return true;
    }

    if (_checkTask != nullptr) {
        return true;
    }

    setProgress(OTAState::CHECKING, 0, "Checking GitHub releases...");
    // 8KB stack is sufficient now that we removed body/readme fetching
    BaseType_t ok = xTaskCreatePinnedToCore(checkThunk, "ota_check", 8192, this, 1, &_checkTask, 0);
    if (ok != pdPASS) {
        _checkTask = nullptr;
        error = "Failed to start OTA check task";
        setProgress(OTAState::ERROR, 0, "OTA: failed to check releases", error.c_str());
        return false;
    }
    return true;
}

void OTAManager::checkThunk(void* arg) {
    OTAManager* self = reinterpret_cast<OTAManager*>(arg);
    self->runCheckTask(false);
    vTaskDelete(nullptr);
}

void OTAManager::runCheckTask(bool force) {
    (void)force;
    String err;
    OTAReleaseInfo next;

    Serial.println("[OTA] Check task started");

    if (!fetchLatestReleaseFromGitHub(next, err)) {
        Serial.printf("[OTA] GitHub fetch failed: %s\n", err.c_str());
        setProgress(OTAState::ERROR, 0, "Failed to fetch release", err.c_str());
        _checkTask = nullptr;
        return;
    }

    Serial.printf("[OTA] Found release: %s\n", next.tag.c_str());

    next.fetchedAtMs = millis();
    if (_releaseMutex && xSemaphoreTake(_releaseMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
        _release = next;
        xSemaphoreGive(_releaseMutex);
    }

    Serial.println("[OTA] Check complete");
    setProgress(OTAState::READY, 0, "Update info ready");
    _checkTask = nullptr;
}

bool OTAManager::fetchLatestReleaseFromGitHub(OTAReleaseInfo& into, String& error) {
    String payload;
    // Reduced buffer from 32KB to 8KB - we only need version and asset URLs
    if (!httpGetToString(githubApiLatestReleaseUrl(), payload, error, 8 * 1024)) {
        return false;
    }

    JsonDocument filter;
    filter["tag_name"] = true;
    filter["name"] = true;
    // Removed body field - not needed
    filter["assets"][0]["name"] = true;
    filter["assets"][0]["browser_download_url"] = true;

    JsonDocument doc;
    DeserializationError derr = deserializeJson(doc, payload, DeserializationOption::Filter(filter));

    if (derr) {
        error = String("JSON parse error: ") + derr.c_str();
        return false;
    }

    String tag = doc["tag_name"] | "";
    if (tag.length() == 0) {
        error = "Missing tag_name";
        return false;
    }

    into.tag = normalizeTagToVersion(tag);
    into.name = (const char*)(doc["name"] | "");
    into.firmwareUrl = "";
    into.spiffsUrl = "";

    if (doc["assets"].is<JsonArrayConst>()) {
        for (JsonObjectConst a : doc["assets"].as<JsonArrayConst>()) {
            String aname = (const char*)(a["name"] | "");
            String aurl = (const char*)(a["browser_download_url"] | "");
            aname.toLowerCase();
            if (aname == "firmware.bin") {
                into.firmwareUrl = aurl;
            } else if (aname == "spiffs.bin") {
                into.spiffsUrl = aurl;
            }
        }
    }
    return true;
}

bool OTAManager::downloadAndApply(const String& url, int updateCommand, const char* label, String& error) {
    Serial.printf("[OTA] %s: Starting download from %s\n", label, url.c_str());
    Serial.printf("[OTA] %s: Free heap: %u bytes\n", label, ESP.getFreeHeap());
    
    // For SPIFFS, use dedicated function
    if (updateCommand == U_SPIFFS) {
        return downloadAndApplySPIFFS(url, label, error);
    }
    
    // For firmware: Use WiFiClientSecure + ESP-IDF OTA APIs for maximum memory efficiency
    // This streams directly to flash in small chunks
    OTAState progressState = OTAState::UPDATING_FIRMWARE;
    
    // Get the OTA partition before starting
    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(nullptr);
    if (!update_partition) {
        error = String(label) + ": No OTA partition found";
        return false;
    }
    Serial.printf("[OTA] %s: Target partition: %s (size: %u)\n", 
        label, update_partition->label, update_partition->size);
    
    // Allocate SSL client on heap
    WiFiClientSecure* client = new WiFiClientSecure();
    if (!client) {
        error = String(label) + ": Out of memory for SSL client";
        return false;
    }
    
    // Configure for minimal memory usage
    client->setInsecure();  // Skip certificate validation
    client->setTimeout(60); // 60 second timeout
    client->setHandshakeTimeout(30); // 30 second handshake timeout
    
    Serial.printf("[OTA] %s: Free heap after SSL client setup: %u\n", label, ESP.getFreeHeap());
    
    // Use HTTPClient for redirect handling
    HTTPClient http;
    http.setTimeout(60000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setReuse(false);
    
    Serial.printf("[OTA] %s: Connecting...\n", label);
    Serial.printf("[OTA] %s: Free heap before connect: %u\n", label, ESP.getFreeHeap());
    
    if (!http.begin(*client, url)) {
        error = String(label) + ": HTTP begin failed";
        delete client;
        return false;
    }
    
    http.addHeader("User-Agent", "probe-station-esp32");
    http.addHeader("Accept", "application/octet-stream");
    
    Serial.printf("[OTA] %s: Sending GET request...\n", label);
    
    // Reset watchdog before potentially long SSL handshake
    esp_task_wdt_reset();
    
    int httpCode = http.GET();
    
    // Reset watchdog after connection
    esp_task_wdt_reset();
    
    Serial.printf("[OTA] %s: HTTP response: %d, free heap: %u\n", label, httpCode, ESP.getFreeHeap());
    
    if (httpCode != HTTP_CODE_OK) {
        if (httpCode <= 0) {
            char errBuf[128];
            int sslErr = client->lastError(errBuf, sizeof(errBuf));
            error = String(label) + ": HTTP " + String(httpCode) + " - " + http.errorToString(httpCode);
            if (sslErr != 0) {
                error += String(" (SSL: ") + String(errBuf) + ")";
            }
        } else {
            error = String(label) + ": HTTP " + String(httpCode);
        }
        http.end();
        delete client;
        return false;
    }
    
    int contentLength = http.getSize();
    Serial.printf("[OTA] %s: Content-Length: %d bytes\n", label, contentLength);
    
    if (contentLength <= 0) {
        error = String(label) + ": Invalid content length";
        http.end();
        delete client;
        return false;
    }
    
    if ((size_t)contentLength > update_partition->size) {
        error = String(label) + ": Firmware too large for partition";
        http.end();
        delete client;
        return false;
    }
    
    // Initialize ESP-IDF OTA
    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        error = String(label) + ": esp_ota_begin failed: " + String(esp_err_to_name(err));
        http.end();
        delete client;
        return false;
    }
    
    Serial.printf("[OTA] %s: OTA started, streaming to flash...\n", label);
    
    // Stream directly to flash in small chunks
    constexpr size_t CHUNK_SIZE = 1024;  // Small chunks to minimize RAM
    uint8_t buffer[CHUNK_SIZE];
    size_t totalWritten = 0;
    int lastProgress = -1;
    uint32_t lastProgressTime = millis();
    
    WiFiClient* stream = http.getStreamPtr();
    
    while (totalWritten < (size_t)contentLength) {
        // Check for timeout (no progress in 30 seconds)
        if (millis() - lastProgressTime > 30000) {
            error = String(label) + ": Download timeout";
            esp_ota_abort(ota_handle);
            http.end();
            delete client;
            return false;
        }
        
        size_t remaining = contentLength - totalWritten;
        size_t toRead = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;
        
        int bytesRead = stream->readBytes(buffer, toRead);
        if (bytesRead <= 0) {
            // Wait a bit and retry
            delay(10);
            continue;
        }
        
        lastProgressTime = millis();  // Reset timeout
        
        err = esp_ota_write(ota_handle, buffer, bytesRead);
        if (err != ESP_OK) {
            error = String(label) + ": esp_ota_write failed: " + String(esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            http.end();
            delete client;
            return false;
        }
        
        totalWritten += bytesRead;
        
        // Update progress
        int progress = (totalWritten * 100) / contentLength;
        if (progress != lastProgress) {
            lastProgress = progress;
            setProgress(progressState, progress, "Downloading firmware...");
            if (progress % 10 == 0) {
                Serial.printf("[OTA] %s: %d%% (%u/%d bytes), heap: %u\n", 
                    label, progress, totalWritten, contentLength, ESP.getFreeHeap());
            }
            esp_task_wdt_reset();  // Feed watchdog on progress updates
        }
        
        vTaskDelay(1);  // Yield to other tasks
    }
    
    // Clean up HTTP before finishing OTA
    http.end();
    delete client;
    client = nullptr;
    
    Serial.printf("[OTA] %s: Download complete, finalizing...\n", label);
    
    // Finish OTA
    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            error = String(label) + ": Firmware validation failed (bad signature)";
        } else {
            error = String(label) + ": esp_ota_end failed: " + String(esp_err_to_name(err));
        }
        return false;
    }
    
    // Set the new partition as boot partition
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        error = String(label) + ": esp_ota_set_boot_partition failed: " + String(esp_err_to_name(err));
        return false;
    }
    
    // Reboot immediately after setting boot partition
    // The old code in memory may become invalid, so reboot now before any issues
    Serial.printf("[OTA] %s: Firmware update successful! %u bytes written. Rebooting...\n", label, totalWritten);
    Serial.flush();
    delay(100);
    ESP.restart();
    
    // Never reaches here
    return true;
}

// SPIFFS update using Arduino Update library (esp_https_ota doesn't support SPIFFS)
bool OTAManager::downloadAndApplySPIFFS(const String& url, const char* label, String& error) {
    Serial.printf("[OTA] %s: Starting SPIFFS download from %s\n", label, url.c_str());
    Serial.printf("[OTA] %s: Free heap: %u bytes\n", label, ESP.getFreeHeap());
    
    OTAState progressState = OTAState::UPDATING_SPIFFS;
    
    WiFiClientSecure* client = new WiFiClientSecure();
    if (!client) {
        error = String(label) + ": Out of memory for SSL client";
        return false;
    }
    
    client->setInsecure();
    client->setTimeout(60);
    client->setHandshakeTimeout(30);
    
    Serial.printf("[OTA] %s: Free heap after SSL client setup: %u\n", label, ESP.getFreeHeap());
    
    HTTPClient http;
    http.setTimeout(60000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setReuse(false);
    
    Serial.printf("[OTA] %s: Connecting...\n", label);
    
    if (!http.begin(*client, url)) {
        error = String(label) + ": HTTP begin failed";
        delete client;
        return false;
    }
    
    http.addHeader("User-Agent", "probe-station-esp32");
    
    Serial.printf("[OTA] %s: Sending GET request...\n", label);
    
    // Reset watchdog before potentially long SSL handshake
    esp_task_wdt_reset();
    
    int httpCode = http.GET();
    
    // Reset watchdog after connection
    esp_task_wdt_reset();
    
    Serial.printf("[OTA] %s: HTTP response: %d\n", label, httpCode);
    
    if (httpCode != HTTP_CODE_OK) {
        if (httpCode <= 0) {
            char errBuf[128];
            int sslErr = client->lastError(errBuf, sizeof(errBuf));
            error = String(label) + ": HTTP " + String(httpCode) + " - " + http.errorToString(httpCode);
            if (sslErr != 0) {
                error += String(" (SSL: ") + String(errBuf) + ")";
            }
        } else {
            error = String(label) + ": HTTP " + String(httpCode);
        }
        http.end();
        delete client;
        return false;
    }
    
    int contentLength = http.getSize();
    Serial.printf("[OTA] %s: Content-Length: %d bytes\n", label, contentLength);
    
    if (contentLength <= 0) {
        error = String(label) + ": Invalid content length";
        http.end();
        delete client;
        return false;
    }
    
    Update.abort();
    delay(100);
    
    if (!Update.begin(contentLength, U_SPIFFS)) {
        error = String(label) + ": Update.begin failed - " + Update.errorString();
        http.end();
        delete client;
        return false;
    }
    
    // Stream in chunks with watchdog feeding
    WiFiClient* stream = http.getStreamPtr();
    constexpr size_t CHUNK_SIZE = 1024;
    uint8_t buffer[CHUNK_SIZE];
    size_t written = 0;
    int lastProgress = -1;
    uint32_t lastProgressTime = millis();
    
    while (written < (size_t)contentLength) {
        // Watchdog timeout check
        if (millis() - lastProgressTime > 30000) {
            error = String(label) + ": Download timeout";
            Update.abort();
            http.end();
            delete client;
            return false;
        }
        
        size_t remaining = contentLength - written;
        size_t toRead = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;
        
        int bytesRead = stream->readBytes(buffer, toRead);
        if (bytesRead <= 0) {
            delay(10);
            continue;
        }
        
        lastProgressTime = millis();
        
        size_t bytesWritten = Update.write(buffer, bytesRead);
        if (bytesWritten != (size_t)bytesRead) {
            error = String(label) + ": Write failed - " + Update.errorString();
            Update.abort();
            http.end();
            delete client;
            return false;
        }
        
        written += bytesWritten;
        
        // Progress and watchdog
        int progress = (written * 100) / contentLength;
        if (progress != lastProgress) {
            lastProgress = progress;
            setProgress(progressState, progress, "Downloading SPIFFS...");
            if (progress % 10 == 0) {
                Serial.printf("[OTA] %s: %d%% (%u/%d bytes)\n", 
                    label, progress, written, contentLength);
            }
            esp_task_wdt_reset();  // Feed watchdog
        }
        
        vTaskDelay(1);  // Yield
    }
    
    http.end();
    delete client;
    
    if (written != (size_t)contentLength) {
        error = String(label) + ": Incomplete write (" + String(written) + "/" + String(contentLength) + ")";
        Update.abort();
        return false;
    }
    
    if (!Update.end(true)) {
        error = String(label) + ": Update.end failed - " + Update.errorString();
        return false;
    }
    
    Serial.printf("[OTA] %s: SPIFFS update successful! %u bytes written\n", label, written);
    return true;
}

bool OTAManager::startUpdate(OTATarget target, String& error) {
    if (isBusy()) {
        error = "OTA already in progress";
        return false;
    }

    OTAProgress p = getProgress();
    if (p.state == OTAState::CHECKING) {
        error = "Checking for updates, please wait";
        return false;
    }

    OTAReleaseInfo info;
    getReleaseInfoCopy(info);
    if (info.tag.length() == 0) {
        String tmp;
        ensureReleaseInfoFresh(false, tmp);
        error = "Update info not ready. Press Check first.";
        return false;
    }

    if (String(FIRMWARE_VERSION) == info.tag) {
        error = "Already up to date";
        return false;
    }

    if ((target == OTATarget::FIRMWARE || target == OTATarget::BOTH) && info.firmwareUrl.length() == 0) {
        error = "Release missing firmware.bin asset";
        return false;
    }
    if ((target == OTATarget::SPIFFS || target == OTATarget::BOTH) && info.spiffsUrl.length() == 0) {
        error = "Release missing spiffs.bin asset";
        return false;
    }

    if (target == OTATarget::FIRMWARE || target == OTATarget::BOTH) {
        const esp_partition_t* ota_partition = esp_ota_get_next_update_partition(NULL);
        if (ota_partition == NULL) {
            error = "No OTA partition found";
            Serial.println("[OTA] Error: No OTA partition available");
            return false;
        }
        
        Serial.printf("[OTA] OTA partition: %s, size: %u bytes\n", 
            ota_partition->label, ota_partition->size);
    }
    
    size_t freeHeap = ESP.getFreeHeap();
    constexpr size_t MIN_HEAP_FOR_OTA = 50000;
    if (freeHeap < MIN_HEAP_FOR_OTA) {
        error = "Not enough memory for OTA (need 50KB, have " + String(freeHeap / 1024) + "KB)";
        Serial.printf("[OTA] Error: Insufficient heap - need %u, have %u\n", MIN_HEAP_FOR_OTA, freeHeap);
        return false;
    }
    Serial.printf("[OTA] Pre-flight check: %u bytes free heap (minimum %u)\n", freeHeap, MIN_HEAP_FOR_OTA);

    if (_releaseMutex && xSemaphoreTake(_releaseMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
        _release = info;
        xSemaphoreGive(_releaseMutex);
    }

    portENTER_CRITICAL(&_mux);
    _progress.state = OTAState::UPDATING_FIRMWARE;
    _progress.target = target;
    _progress.progressPercent = 0;
    _progress.message[0] = '\0';
    _progress.error[0] = '\0';
    portEXIT_CRITICAL(&_mux);

    BaseType_t ok = xTaskCreatePinnedToCore(taskThunk, "ota_update", 32768, this, 1, &_task, 0);
    if (ok != pdPASS) {
        setProgress(OTAState::ERROR, 0, "Failed to start OTA task");
        error = "Failed to start OTA task";
        return false;
    }

    return true;
}

void OTAManager::taskThunk(void* arg) {
    OTAManager* self = reinterpret_cast<OTAManager*>(arg);
    OTATarget target = self->getProgress().target;
    self->runUpdateTask(target);
    vTaskDelete(nullptr);
}

void OTAManager::runUpdateTask(OTATarget target) {
    Serial.println("[OTA] Update task started");
    
    // CRITICAL: Wait for HTTP response to be fully sent before cleanup
    // This prevents stack corruption in async_tcp task
    vTaskDelay(pdMS_TO_TICKS(500));
    
    Serial.printf("[OTA] Target: %s\n", 
        (target == OTATarget::FIRMWARE) ? "firmware" : 
        (target == OTATarget::SPIFFS) ? "spiffs" : "both");
    Serial.printf("[OTA] Firmware URL: %s\n", _release.firmwareUrl.c_str());
    Serial.printf("[OTA] SPIFFS URL: %s\n", _release.spiffsUrl.c_str());
    
    // Free up as much memory as possible for OTA
    Serial.printf("[OTA] Free heap before cleanup: %u bytes\n", ESP.getFreeHeap());
    
    // Disable MQTT (frees ~26KB)
    mqttClient.setOtaMode(true);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Close WebSocket connections
    webServer.setOtaMode(true);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Free display sprite buffer (~65KB on TTGO T-Display)
    Serial.println("[OTA] Freeing display sprite...");
    displayManager.setOtaMode(true);
    vTaskDelay(pdMS_TO_TICKS(500));
    
    Serial.printf("[OTA] Free heap after cleanup: %u bytes\n", ESP.getFreeHeap());

    String err;

    // For "BOTH" target: Update SPIFFS first, then firmware
    // SPIFFS is smaller and doesn't require reboot, so we do it first
    // Then firmware update + reboot completes the process in one go
    
    if (target == OTATarget::BOTH) {
        // Phase 1: SPIFFS (smaller, no reboot needed)
        if (_release.spiffsUrl.length() > 0) {
            Serial.println("[OTA] Phase 1: Starting SPIFFS update (smaller, faster)...");
            setProgress(OTAState::UPDATING_SPIFFS, 0, "Updating SPIFFS...");
            
            if (!downloadAndApply(_release.spiffsUrl, U_SPIFFS, "SPIFFS", err)) {
                Serial.printf("[OTA] SPIFFS update failed: %s\n", err.c_str());
                setProgress(OTAState::ERROR, 0, "SPIFFS update failed", err.c_str());
                mqttClient.setOtaMode(false);
                webServer.setOtaMode(false);
                displayManager.setOtaMode(false);
                return;
            }
            Serial.println("[OTA] SPIFFS update successful!");
            vTaskDelay(pdMS_TO_TICKS(500));  // Brief pause between updates
        }
        
        // Phase 2: Firmware (requires reboot)
        Serial.println("[OTA] Phase 2: Starting firmware update...");
        setProgress(OTAState::UPDATING_FIRMWARE, 0, "Updating firmware...");
        
        if (!downloadAndApply(_release.firmwareUrl, U_FLASH, "Firmware", err)) {
            Serial.printf("[OTA] Firmware update failed: %s\n", err.c_str());
            setProgress(OTAState::ERROR, 0, "Firmware update failed", err.c_str());
            mqttClient.setOtaMode(false);
            webServer.setOtaMode(false);
            displayManager.setOtaMode(false);
            return;
        }
        
        Serial.println("[OTA] Both updates complete! Rebooting...");
        setProgress(OTAState::REBOOTING, 100, "Update complete. Rebooting...");
        Serial.flush();
        delay(1000);
        ESP.restart();
        return;  // Never reaches here
    }

    if (target == OTATarget::FIRMWARE) {
        Serial.println("[OTA] Starting firmware-only update...");
        setProgress(OTAState::UPDATING_FIRMWARE, 0, "Starting firmware download...");
        
        if (!downloadAndApply(_release.firmwareUrl, U_FLASH, "Firmware", err)) {
            Serial.printf("[OTA] Firmware update failed: %s\n", err.c_str());
            setProgress(OTAState::ERROR, 0, "Firmware update failed", err.c_str());
            mqttClient.setOtaMode(false);
            webServer.setOtaMode(false);
            displayManager.setOtaMode(false);
            return;
        }
        
        Serial.println("[OTA] Firmware update successful! Rebooting...");
        setProgress(OTAState::REBOOTING, 100, "Firmware updated. Rebooting...");
        Serial.flush();
        delay(1000);
        ESP.restart();
        return;  // Never reaches here
    }

    if (target == OTATarget::SPIFFS) {
        Serial.println("[OTA] Starting SPIFFS-only update...");
        setProgress(OTAState::UPDATING_SPIFFS, 0, "Starting SPIFFS download...");
        
        if (!downloadAndApply(_release.spiffsUrl, U_SPIFFS, "SPIFFS", err)) {
            Serial.printf("[OTA] SPIFFS update failed: %s\n", err.c_str());
            setProgress(OTAState::ERROR, 0, "SPIFFS update failed", err.c_str());
            mqttClient.setOtaMode(false);
            webServer.setOtaMode(false);
            displayManager.setOtaMode(false);
            return;
        }
        Serial.println("[OTA] SPIFFS update successful!");
    }

    Serial.println("[OTA] All updates complete. Rebooting in 1.5 seconds...");
    setProgress(OTAState::REBOOTING, 100, "Update complete. Rebooting...");
    delay(1500);
    ESP.restart();
}
