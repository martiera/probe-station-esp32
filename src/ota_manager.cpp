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
constexpr uint32_t HTTP_TIMEOUT_MS = 20000;

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
        error = "HTTP GET failed";
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

        size_t canTake = r;
        if (total + canTake > maxBytes) {
            canTake = maxBytes - total;
        }
        out.concat((const char*)buf, canTake);
        total += canTake;
        if (total >= maxBytes) break;
    }

    http.end();
    return true;
}
}

OTAManager::OTAManager() : _mux(portMUX_INITIALIZER_UNLOCKED), _task(nullptr) {
    _progress.state = OTAState::IDLE;
    _progress.target = OTATarget::BOTH;
    _progress.progressPercent = 0;
    _progress.message[0] = '\0';
    _progress.error[0] = '\0';
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

bool OTAManager::refreshReleaseInfo(String& error) {
    if (_release.fetchedAtMs != 0 && (millis() - _release.fetchedAtMs) < RELEASE_INFO_TTL_MS) {
        return true;
    }

    setProgress(OTAState::CHECKING, 0, "Checking GitHub releases...");

    if (!fetchLatestReleaseFromGitHub(error)) {
        setProgress(OTAState::ERROR, 0, "OTA: failed to check releases", error.c_str());
        return false;
    }

    String readmeErr;
    fetchReadmeForTag(_release.tag, readmeErr);

    setProgress(OTAState::READY, 0, "OTA: update info ready");
    return true;
}

bool OTAManager::fetchLatestReleaseFromGitHub(String& error) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(HTTP_TIMEOUT_MS / 1000);

    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);

    const String url = githubApiLatestReleaseUrl();
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    if (!http.begin(client, url)) {
        error = "HTTP begin failed";
        return false;
    }

    http.addHeader("User-Agent", "probe-station-esp32");
    http.addHeader("Accept", "application/vnd.github+json");

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        error = String("GitHub API HTTP ") + code;
        http.end();
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
    DeserializationError derr = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();

    if (derr) {
        error = String("JSON parse error: ") + derr.c_str();
        return false;
    }

    String tag = doc["tag_name"] | "";
    if (tag.length() == 0) {
        error = "Missing tag_name";
        return false;
    }

    _release.tag = normalizeTagToVersion(tag);
    _release.name = (const char*)(doc["name"] | "");
    _release.body = (const char*)(doc["body"] | "");
    _release.firmwareUrl = "";
    _release.spiffsUrl = "";

    if (doc["assets"].is<JsonArrayConst>()) {
        for (JsonObjectConst a : doc["assets"].as<JsonArrayConst>()) {
            String aname = (const char*)(a["name"] | "");
            String aurl = (const char*)(a["browser_download_url"] | "");
            aname.toLowerCase();
            if (aname == "firmware.bin") {
                _release.firmwareUrl = aurl;
            } else if (aname == "spiffs.bin") {
                _release.spiffsUrl = aurl;
            }
        }
    }

    _release.fetchedAtMs = millis();
    return true;
}

bool OTAManager::fetchReadmeForTag(const String& tag, String& error) {
    String out;
    if (!httpGetToString(githubRawReadmeUrl(tag), out, error, 24 * 1024)) {
        return false;
    }
    _release.readme = out;
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

    // Ensure we have release info
    String infoErr;
    if (!refreshReleaseInfo(infoErr)) {
        error = infoErr;
        return false;
    }

    if (_release.tag.length() > 0 && String(FIRMWARE_VERSION) == _release.tag) {
        error = "Already up to date";
        return false;
    }

    if ((target == OTATarget::FIRMWARE || target == OTATarget::BOTH) && _release.firmwareUrl.length() == 0) {
        error = "Release missing firmware.bin asset";
        return false;
    }
    if ((target == OTATarget::SPIFFS || target == OTATarget::BOTH) && _release.spiffsUrl.length() == 0) {
        error = "Release missing spiffs.bin asset";
        return false;
    }

    portENTER_CRITICAL(&_mux);
    _progress.target = target;
    _progress.progressPercent = 0;
    _progress.error[0] = '\0';
    portEXIT_CRITICAL(&_mux);

    BaseType_t ok = xTaskCreatePinnedToCore(taskThunk, "ota_update", 8192, this, 1, &_task, 1);
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
