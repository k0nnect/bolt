#ifndef TORRENTITEM_H
#define TORRENTITEM_H

#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <memory>
#include <chrono>
#include <deque>
#include <functional>

#ifdef BOLT_ENABLE_TORRENT
#include <libtorrent/session.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/alert_types.hpp>
#endif

enum class TorrentState {
    Queued,
    Checking,
    Downloading,
    Seeding,
    Paused,
    Completed,
    Error
};

struct TorrentFile {
    std::string path;
    int64_t size;
    float progress;
    int priority;  // 0 = don't download, 1-7 = priority
    bool selected;
};

class TorrentItem {
public:
    TorrentItem(const std::string& source, const std::string& savePath);
    ~TorrentItem();
    
    void start();
    void pause();
    void resume();
    void cancel();
    void setFilePriority(size_t index, int priority);
    void setAllPriorities(int priority);
    
    // Getters
    std::string getName() const;
    std::string getSavePath() const { return savePath; }
    std::string getSource() const { return source; }
    TorrentState getState() const { return state.load(); }
    std::string getStateString() const;
    double getProgress() const { return progress.load(); }
    int64_t getTotalSize() const { return totalSize; }
    int64_t getDownloaded() const { return downloaded.load(); }
    int64_t getUploaded() const { return uploaded.load(); }
    double getDownloadSpeed() const { return downloadSpeed.load(); }
    double getUploadSpeed() const { return uploadSpeed.load(); }
    int getSeeds() const { return seeds.load(); }
    int getPeers() const { return peers.load(); }
    std::string getETA() const;
    std::string getInfoHash() const { return infoHash; }
    const std::vector<TorrentFile>& getFiles() const { return files; }
    std::string getErrorMessage() const { return errorMessage; }
    
    // Speed history for graph
    const std::deque<float>& getSpeedHistory() const { return speedHistory; }
    
    // Check if this is a magnet link
    bool isMagnet() const;
    
    // Render in UI
    void render(size_t index, bool compactMode = false);
    void renderDetailed(size_t index);
    void renderCompact(size_t index);
    
    // Serialization
    std::string serialize() const;
    static std::unique_ptr<TorrentItem> deserialize(const std::string& data);

private:
    void updateThread();
    void parseMetadata();
    std::string formatBytes(int64_t bytes) const;
    std::string formatSpeed(double bytesPerSecond) const;
    std::string formatTime(int seconds) const;
    
    std::string source;  // .torrent file path or magnet link
    std::string savePath;
    std::string name;
    std::string infoHash;
    std::string errorMessage;
    
    std::atomic<TorrentState> state{TorrentState::Queued};
    std::atomic<double> progress{0.0};
    std::atomic<int64_t> downloaded{0};
    std::atomic<int64_t> uploaded{0};
    std::atomic<double> downloadSpeed{0.0};
    std::atomic<double> uploadSpeed{0.0};
    std::atomic<int> seeds{0};
    std::atomic<int> peers{0};
    int64_t totalSize = 0;
    
    std::vector<TorrentFile> files;
    std::mutex filesMutex;
    
    std::thread updateThreadHandle;
    std::atomic<bool> shouldStop{false};
    
    // Speed history for graph
    std::deque<float> speedHistory;
    std::chrono::steady_clock::time_point lastHistoryUpdate;
    
#ifdef BOLT_ENABLE_TORRENT
    lt::torrent_handle handle;
#endif
};

#endif // TORRENTITEM_H
