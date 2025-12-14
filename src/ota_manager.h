/*
 * ESP32 Temperature Monitoring System
 * GitHub Releases OTA Manager
 */

#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <Arduino.h>

enum class OTATarget : uint8_t {
    FIRMWARE,
    SPIFFS,
    BOTH
};

enum class OTAState : uint8_t {
    IDLE,
    CHECKING,
    READY,
    UPDATING_FIRMWARE,
    UPDATING_SPIFFS,
    REBOOTING,
    ERROR
};

struct OTAReleaseInfo {
    String tag;
    String name;
    String body;
    String readme;
    String firmwareUrl;
    String spiffsUrl;
    uint32_t fetchedAtMs = 0;
};

struct OTAProgress {
    OTAState state = OTAState::IDLE;
    OTATarget target = OTATarget::BOTH;
    int progressPercent = 0;
    char message[96] = {0};
    char error[96] = {0};
};

class OTAManager {
public:
    OTAManager();

    bool refreshReleaseInfo(String& error);
    const OTAReleaseInfo& getReleaseInfo() const { return _release; }

    OTAProgress getProgress() const;
    bool isBusy() const;

    bool startUpdate(OTATarget target, String& error);

private:
    OTAReleaseInfo _release;

    mutable OTAProgress _progress;
    mutable portMUX_TYPE _mux;

    TaskHandle_t _task;

    void setProgress(OTAState state, int progressPercent, const char* message, const char* error = nullptr);

    static void taskThunk(void* arg);
    void runUpdateTask(OTATarget target);

    bool fetchLatestReleaseFromGitHub(String& error);
    bool fetchReadmeForTag(const String& tag, String& error);

    bool downloadAndApply(const String& url, int updateCommand, const char* label, String& error);
};

extern OTAManager otaManager;

#endif // OTA_MANAGER_H
