/*
 * ESP32 Temperature Monitoring System
 * GitHub Releases OTA Manager
 */

#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <Arduino.h>
#include <freertos/semphr.h>

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

struct OTAPartitionInfo {
    size_t firmwarePartitionSize = 0;
    size_t spiffsPartitionSize = 0;
    size_t currentFirmwareSize = 0;
    size_t freeHeap = 0;
    size_t minFreeHeap = 0;
};

class OTAManager {
public:
    OTAManager();

    // Initialize OTA manager - call from setup() after WiFi is connected
    // Checks for pending SPIFFS update from previous firmware update
    void begin();

    // Starts/refreshes GitHub release info in a background task (non-blocking).
    // Returns true if info is already fresh or a refresh task is running/started.
    bool ensureReleaseInfoFresh(bool force, String& error);

    // Snapshot of the most recently fetched release info (thread-safe).
    void getReleaseInfoCopy(OTAReleaseInfo& out) const;

    OTAProgress getProgress() const;
    bool isBusy() const;

    bool startUpdate(OTATarget target, String& error);
    
    // Call from main loop - handles periodic background checks
    void update();
    
    // Get partition and memory info for pre-flight checks
    static OTAPartitionInfo getPartitionInfo();
    
    // Check if an update is available (compared to current firmware version)
    bool isUpdateAvailable() const;
    
    // Get the available update version (empty if none)
    String getAvailableVersion() const;
    
    // Trigger a check on boot (delayed to allow WiFi to connect)
    void checkOnBoot();

private:
    OTAReleaseInfo _release;
    SemaphoreHandle_t _releaseMutex;

    mutable OTAProgress _progress;
    mutable portMUX_TYPE _mux;

    TaskHandle_t _task;
    TaskHandle_t _checkTask;
    uint32_t _lastAutoCheck = 0;

    void setProgress(OTAState state, int progressPercent, const char* message, const char* error = nullptr);

    static void taskThunk(void* arg);
    void runUpdateTask(OTATarget target);

    static void checkThunk(void* arg);
    void runCheckTask(bool force);

    bool fetchLatestReleaseFromGitHub(OTAReleaseInfo& into, String& error);
    bool fetchReadmeForTag(OTAReleaseInfo& into, const String& tag, String& error);
    
    bool downloadAndApply(const String& url, int updateCommand, const char* label, String& error);
    bool downloadAndApplySPIFFS(const String& url, const char* label, String& error);
};

extern OTAManager otaManager;

#endif // OTA_MANAGER_H
