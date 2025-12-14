/*
 * ESP32 Temperature Monitoring System
 * GitHub Releases OTA Manager
 */

#include "ota_manager.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <cstring>

#include "config.h"
#include "mqtt_client.h"

OTAManager otaManager;

namespace {
constexpr uint32_t RELEASE_INFO_TTL_MS = 5UL * 60UL * 1000UL; // 5 min
constexpr uint32_t HTTP_TIMEOUT_MS = 15000;

String normalizeTagToVersion(String tag) {
    tag.trim();
    if (tag.startsWith("refs/tags/")) tag = tag.substring(strlen("refs/tags/"));
    if (tag.startsWith("v")) return tag;
    // allow bare semver; we still want v-prefix for consistency
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

String githubRawReadmeUrl(const String& tag) {
    String url = "https://raw.githubusercontent.com/";
    url += GITHUB_OWNER;
    url += "/";
    url += GITHUB_REPO;
    url += "/";
    url += tag;
    url += "/README.md";
    return url;
}

bool httpGetToString(const String& url, String& out, String& error, size_t maxBytes) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(HTTP_TIMEOUT_MS / 1000);

    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    if (!http.begin(client, url)) {
        error = "HTTP begin failed";
        return false;
    }

    http.addHeader("User-Agent", "probe-station-esp32");
    int code = http.GET();
    if (code <= 0) {
        error = String("HTTP GET failed: ") + http.errorToString(code);
        http.end();
        return false;
    }
    if (code != HTTP_CODE_OK) {
        error = String("HTTP ") + code;
        http.end();
        return false;
    }

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
        return false;
    }
    return true;
}
}

OTAManager::OTAManager() : _mux(portMUX_INITIALIZER_UNLOCKED), _task(nullptr) {
    _releaseMutex = xSemaphoreCreateMutex();
    _checkTask = nullptr;
    _progress.state = OTAState::IDLE;
    _progress.target = OTATarget::BOTH;
    _progress.progressPercent = 0;
    _progress.message[0] = '\0';
    _progress.error[0] = '\0';
}

void OTAManager::getReleaseInfoCopy(OTAReleaseInfo& out) const {
    if (_releaseMutex && xSemaphoreTake(_releaseMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        out = _release;
        xSemaphoreGive(_releaseMutex);
        return;
    }
    // If mutex can't be acquired quickly, return an empty snapshot.
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

bool OTAManager::ensureReleaseInfoFresh(bool force, String& error) {
    // Don't start a check while an update is running.
    OTAProgress p = getProgress();
    if (p.state == OTAState::UPDATING_FIRMWARE || p.state == OTAState::UPDATING_SPIFFS || p.state == OTAState::REBOOTING) {
        error = "OTA busy";
        return false;
    }

    // Already checking
    if (p.state == OTAState::CHECKING) {
        return true;
    }

    // If we have fresh info and not forced, nothing to do.
    uint32_t fetchedAt = 0;
    if (_releaseMutex && xSemaphoreTake(_releaseMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        fetchedAt = _release.fetchedAtMs;
        xSemaphoreGive(_releaseMutex);
    }
    if (!force && fetchedAt != 0 && (millis() - fetchedAt) < RELEASE_INFO_TTL_MS) {
        return true;
    }

    // Start background check task
    if (_checkTask != nullptr) {
        return true;
    }

    setProgress(OTAState::CHECKING, 0, "Checking GitHub releases...");
    // Pin to core 0 to avoid starving AsyncTCP (typically pinned to core 1).
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

    // Only fetch README if release body is empty (saves ~20s HTTPS fetch)
    if (next.body.length() == 0) {
        String readmeErr;
        fetchReadmeForTag(next, next.tag, readmeErr);
    }

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
    // Read into a bounded string with yields to avoid starving AsyncTCP/loop WDT.
    String payload;
    if (!httpGetToString(githubApiLatestReleaseUrl(), payload, error, 32 * 1024)) {
        return false;
    }

    // Parse only what we need
    JsonDocument filter;
    filter["tag_name"] = true;
    filter["name"] = true;
    filter["body"] = true;
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
    into.body = (const char*)(doc["body"] | "");
    into.firmwareUrl = "";
    into.spiffsUrl = "";
    into.readme = "";

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

bool OTAManager::fetchReadmeForTag(OTAReleaseInfo& into, const String& tag, String& error) {
    String out;
    if (!httpGetToString(githubRawReadmeUrl(tag), out, error, 24 * 1024)) {
        return false;
    }
    into.readme = out;
    return true;
}

bool OTAManager::downloadAndApply(const String& url, int updateCommand, const char* label, String& error) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(HTTP_TIMEOUT_MS / 1000);

    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    if (!http.begin(client, url)) {
        error = String(label) + ": HTTP begin failed";
        return false;
    }

    http.addHeader("User-Agent", "probe-station-esp32");
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        error = String(label) + ": HTTP " + code;
        http.end();
        return false;
    }

    int len = http.getSize();
    WiFiClient* stream = http.getStreamPtr();

    if (!Update.begin((len > 0) ? (size_t)len : UPDATE_SIZE_UNKNOWN, updateCommand)) {
        error = String(label) + ": Update.begin failed";
        http.end();
        return false;
    }

    size_t written = 0;
    uint8_t buf[1024];
    uint32_t lastPct = 0;

    while (http.connected()) {
        int avail = stream->available();
        if (avail <= 0) {
            delay(1);
            continue;
        }

        int toRead = avail;
        if (toRead > (int)sizeof(buf)) toRead = sizeof(buf);

        int r = stream->readBytes(buf, toRead);
        if (r <= 0) break;

        size_t w = Update.write(buf, (size_t)r);
        written += w;

        if (len > 0) {
            uint32_t pct = (uint32_t)((written * 100ULL) / (uint32_t)len);
            if (pct != lastPct) {
                lastPct = pct;
                char msg[64];
                snprintf(msg, sizeof(msg), "%s: %lu%%", label, (unsigned long)pct);
                setProgress(getProgress().state, (int)pct, msg);
            }
        }

        if (w != (size_t)r) {
            error = String(label) + ": write failed";
            Update.abort();
            http.end();
            return false;
        }

        // Yield to keep the system responsive.
        delay(0);
    }

    if (!Update.end(true)) {
        error = String(label) + ": Update.end failed";
        http.end();
        return false;
    }

    http.end();
    return true;
}

bool OTAManager::startUpdate(OTATarget target, String& error) {
    if (isBusy()) {
        error = "OTA already in progress";
        return false;
    }

    // Never do a blocking GitHub check here (this is typically called from a web handler).
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

    // Copy URLs into the shared release struct for the update task to use.
    if (_releaseMutex && xSemaphoreTake(_releaseMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
        _release = info;
        xSemaphoreGive(_releaseMutex);
    }

    portENTER_CRITICAL(&_mux);
    _progress.target = target;
    _progress.progressPercent = 0;
    _progress.error[0] = '\0';
    portEXIT_CRITICAL(&_mux);

    // Pin to core 0 to avoid starving AsyncTCP on core 1.
    BaseType_t ok = xTaskCreatePinnedToCore(taskThunk, "ota_update", 8192, this, 1, &_task, 0);
    if (ok != pdPASS) {
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
    mqttClient.disconnect();

    String err;

    if (target == OTATarget::FIRMWARE || target == OTATarget::BOTH) {
        setProgress(OTAState::UPDATING_FIRMWARE, 0, "Downloading firmware...");
        if (!downloadAndApply(_release.firmwareUrl, U_FLASH, "Firmware", err)) {
            setProgress(OTAState::ERROR, 0, "Firmware update failed", err.c_str());
            return;
        }
    }

    if (target == OTATarget::SPIFFS || target == OTATarget::BOTH) {
        setProgress(OTAState::UPDATING_SPIFFS, 0, "Downloading web UI (SPIFFS)...");
        if (!downloadAndApply(_release.spiffsUrl, U_SPIFFS, "SPIFFS", err)) {
            setProgress(OTAState::ERROR, 0, "SPIFFS update failed", err.c_str());
            return;
        }
    }

    setProgress(OTAState::REBOOTING, 100, "Update complete. Rebooting...");
    delay(1500);
    ESP.restart();
}
