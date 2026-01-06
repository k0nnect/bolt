#ifndef TORRENTMANAGER_H
#define TORRENTMANAGER_H

#include <vector>
#include <memory>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include "torrentitem.h"

#ifdef BOLT_ENABLE_TORRENT
#include <libtorrent/session.hpp>
#endif

struct TorrentStats {
    int totalTorrents = 0;
    int activeTorrents = 0;
    int seedingTorrents = 0;
    int64_t totalDownloaded = 0;
    int64_t totalUploaded = 0;
    double downloadSpeed = 0.0;
    double uploadSpeed = 0.0;
};

class TorrentManager {
public:
    TorrentManager();
    ~TorrentManager();
    
    // Torrent operations
    void addTorrent(const std::string& source, const std::string& savePath = "");
    void addMagnet(const std::string& magnetUri, const std::string& savePath = "");
    void removeTorrent(size_t index, bool deleteFiles = false);
    void pauseTorrent(size_t index);
    void resumeTorrent(size_t index);
    void pauseAll();
    void resumeAll();
    
    // Settings
    void setMaxDownloadSpeed(int kbps);
    void setMaxUploadSpeed(int kbps);
    void setMaxConnections(int connections);
    void setDHTEnabled(bool enabled);
    void setPort(int port);
    
    // Getters
    size_t getTorrentCount() const { return torrents.size(); }
    size_t getActiveTorrentCount() const;
    size_t getSeedingCount() const;
    const TorrentStats& getStats() const { return stats; }
    TorrentItem* getTorrent(size_t index);
    
    // Update loop
    void update();
    
    // Rendering
    void render(bool compactMode);
    void renderAddTorrentDialog(bool* open);
    
    // Persistence
    void loadTorrents();
    void saveTorrents();
    
    // Check if torrenting is available
    static bool isAvailable();

private:
    void sessionThread();
    void updateStats();
    std::string getDefaultSavePath() const;
    
    std::vector<std::unique_ptr<TorrentItem>> torrents;
    std::mutex torrentsMutex;
    
    TorrentStats stats;
    
    std::thread sessionThreadHandle;
    std::atomic<bool> running{true};
    
    int maxDownloadSpeed = 0;  // 0 = unlimited
    int maxUploadSpeed = 0;
    int maxConnections = 200;
    bool dhtEnabled = true;
    int listenPort = 6881;
    
#ifdef BOLT_ENABLE_TORRENT
    std::unique_ptr<lt::session> session;
#endif
};

#endif // TORRENTMANAGER_H
