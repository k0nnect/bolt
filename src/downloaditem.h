#ifndef DOWNLOADITEM_H
#define DOWNLOADITEM_H

#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <fstream>
#include <memory>
#include <vector>
#include <chrono>
#include <deque>
#include "settings.h"

enum class DownloadState {
    Queued,
    Downloading,
    Paused,
    Completed,
    Cancelled,
    Error,
    Waiting
};

enum class FileCategory {
    Video,
    Audio,
    Document,
    Archive,
    Program,
    Image,
    Other
};

struct DownloadSegment {
    uint64_t startByte;
    uint64_t endByte;
    uint64_t currentByte;
    std::atomic<bool> completed{false};
    std::atomic<bool> active{false};
    std::thread workerThread;
};

class DownloadItem {
public:
    DownloadItem(const std::string& url, const std::string& filePath);
    ~DownloadItem();
    
    void start();
    void pause();
    void resume();
    void cancel();
    void retry();
    void setSpeedLimit(int kbps) { speedLimitKBps = kbps; }
    void setPriority(int p) { priority = p; }
    void setCustomFileName(const std::string& name) { customFileName = name; }
    void setExpectedChecksum(const std::string& type, const std::string& checksum) { 
        checksumType = type; expectedChecksum = checksum; 
    }
    
    // Verification
    bool verifyChecksum();
    std::string calculateMD5();
    std::string calculateSHA256();
    
    bool isActive() const { return state == DownloadState::Downloading; }
    bool isCompleted() const { return state == DownloadState::Completed; }
    bool isPaused() const { return state == DownloadState::Paused; }
    bool isCancelled() const { return state == DownloadState::Cancelled; }
    bool isQueued() const { return state == DownloadState::Queued; }
    bool hasError() const { return state == DownloadState::Error; }
    
    void render(size_t index, bool compactMode = false);
    void renderCompact(size_t index);
    void renderDetailed(size_t index);
    
    std::string getFileName() const;
    std::string getUrl() const { return url; }
    std::string getFilePath() const { return filePath; }
    double getProgress() const { return progress; }
    std::string getStatus() const;
    std::string getSpeed() const;
    std::string getSize() const;
    std::string getETA() const;
    FileCategory getCategory() const;
    std::string getCategoryName() const;
    int getPriority() const { return priority; }
    uint64_t getBytesReceived() const { return bytesReceived; }
    uint64_t getBytesTotal() const { return bytesTotal; }
    double getCurrentSpeed() const { return currentSpeed; }
    std::chrono::system_clock::time_point getStartTime() const { return startTime; }
    std::chrono::system_clock::time_point getEndTime() const { return endTime; }
    std::string getErrorMessage() const { return errorMessage; }
    std::string getChecksumResult() const { return checksumResult; }
    bool isChecksumVerified() const { return checksumVerified; }
    std::string getAddedDate() const;
    
    // Serialization for history/export
    std::string serialize() const;
    static std::unique_ptr<DownloadItem> deserialize(const std::string& data);
    
    // Speed history for graph
    const std::deque<float>& getSpeedHistory() const { return speedHistory; }
    
    // Multi-segment info
    int getActiveSegments() const;
    int getTotalSegments() const { return (int)segments.size(); }

private:
    void downloadThread();
    void segmentDownloadThread(int segmentIndex);
    void initializeSegments();
    bool supportsRangeRequests();
    std::string formatBytes(uint64_t bytes) const;
    std::string formatSpeed(double bytesPerSecond) const;
    std::string formatTime(int seconds) const;
    std::string getFileNameFromPath(const std::string& path) const;
    FileCategory determineCategory() const;
    void updateSpeedHistory();
    
    std::string url;
    std::string filePath;
    std::atomic<DownloadState> state;
    std::atomic<bool> shouldStop;
    std::atomic<bool> shouldPause;
    
    std::vector<std::unique_ptr<DownloadSegment>> segments;
    std::thread downloadThreadHandle;
    std::mutex fileMutex;
    std::unique_ptr<std::fstream> file;
    
    std::atomic<uint64_t> bytesReceived{0};
    std::atomic<uint64_t> bytesTotal{0};
    uint64_t resumeOffset;
    std::atomic<double> progress{0.0};
    std::atomic<double> currentSpeed{0.0};
    std::string errorMessage;
    int priority = 0;  // Higher = more priority
    int speedLimitKBps = 0;
    
    std::chrono::steady_clock::time_point lastUpdateTime;
    std::chrono::system_clock::time_point startTime;
    std::chrono::system_clock::time_point endTime;
    uint64_t lastBytesReceived;
    
    // Speed history for graph (last 60 samples)
    std::deque<float> speedHistory;
    std::chrono::steady_clock::time_point lastHistoryUpdate;
    
    // For ETA calculation
    double avgSpeed = 0.0;
    int etaSeconds = -1;
    
    // Checksum verification
    std::string checksumType;  // "MD5" or "SHA256"
    std::string expectedChecksum;
    std::string checksumResult;
    bool checksumVerified = false;
    
    // Custom filename
    std::string customFileName;
    
    // Date added
    std::chrono::system_clock::time_point addedTime;
};

#endif // DOWNLOADITEM_H
