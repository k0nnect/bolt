#ifndef DOWNLOADMANAGER_H
#define DOWNLOADMANAGER_H

#include <vector>
#include <memory>
#include <string>
#include <mutex>
#include <deque>
#include <map>
#include "downloaditem.h"
#include "torrentmanager.h"

#ifdef _WIN32
#include <windows.h>
#include <shobjidl.h>
#include <comdef.h>
#endif

struct DownloadStats {
    uint64_t totalBytesDownloaded = 0;
    uint64_t sessionBytesDownloaded = 0;
    int totalDownloadsCompleted = 0;
    int sessionDownloadsCompleted = 0;
    double currentTotalSpeed = 0.0;
    double peakSpeed = 0.0;
    std::deque<float> speedHistory;  // For graph
    
    // Extended stats
    uint64_t allTimeBytesDownloaded = 0;
    int allTimeDownloadsCompleted = 0;
    double averageSpeed = 0.0;
    std::chrono::steady_clock::time_point sessionStart;
};

struct HistoryEntry {
    std::string url;
    std::string fileName;
    std::string filePath;
    uint64_t fileSize;
    std::string dateCompleted;
    std::string checksum;
    FileCategory category;
};

enum class FilterType {
    All,
    Downloading,
    Completed,
    Queued,
    Paused,
    Error
};

enum class SortType {
    DateAdded,
    Name,
    Size,
    Progress,
    Priority
};

class DownloadManager {
public:
    DownloadManager();
    ~DownloadManager();
    
    void addDownload(const std::string& url, const std::string& customPath = "");
    void addDownloads(const std::vector<std::string>& urls);
    void pauseDownload(size_t index);
    void resumeDownload(size_t index);
    void cancelDownload(size_t index);
    void removeDownload(size_t index);
    void retryDownload(size_t index);
    void clearCompleted();
    void clearAll();
    void pauseAll();
    void resumeAll();
    
    // Batch operations
    void pauseSelected();
    void resumeSelected();
    void cancelSelected();
    void removeSelected();
    
    // Selection
    void selectDownload(size_t index, bool selected);
    void selectAll();
    void deselectAll();
    bool isSelected(size_t index) const;
    size_t getSelectedCount() const;
    
    // Filtering and sorting
    void setFilter(FilterType filter) { currentFilter = filter; }
    void setSort(SortType sort) { currentSort = sort; }
    void setSortAscending(bool asc) { sortAscending = asc; }
    void setSearchQuery(const std::string& query) { searchQuery = query; }
    
    // Rendering
    void render();
    void renderSidebar();
    void renderToolbar();
    void renderDownloadList();
    void renderStatistics();
    void renderSpeedGraph();
    void renderSettingsPanel(bool* open);
    void renderAddDownloadDialog(bool* open, char* urlBuffer, size_t bufferSize);
    void renderBatchImportDialog(bool* open);
    void renderHistoryPanel(bool* open);
    void renderLinkGrabberDialog(bool* open);
    void renderDownloadDetailsPanel(bool* open, size_t index);
    void renderSchedulerPanel(bool* open);
    void renderContextMenu(size_t index);
    void renderTorrentPanel(bool* open);
    void renderAddTorrentDialog(bool* open);
    
    // Statistics
    const DownloadStats& getStats() const { return stats; }
    void updateStats();
    
    // Clipboard monitoring
    void checkClipboard();
    bool hasClipboardUrl() const { return !clipboardUrl.empty(); }
    std::string getClipboardUrl() const { return clipboardUrl; }
    void clearClipboardUrl() { clipboardUrl.clear(); }
    
    // Getters
    size_t getDownloadCount() const { return downloads.size(); }
    size_t getActiveCount() const;
    size_t getCompletedCount() const;
    size_t getQueuedCount() const;
    size_t getPausedCount() const;
    size_t getErrorCount() const;
    
    // History
    void loadHistory();
    void saveHistory();
    void addToHistory(const DownloadItem& item);
    const std::vector<HistoryEntry>& getHistory() const { return history; }
    void clearHistory();
    
    // Export/Import
    void exportDownloads(const std::string& path);
    void importDownloads(const std::string& path);
    void exportHistory(const std::string& path);
    
    // Queue management
    void moveUp(size_t index);
    void moveDown(size_t index);
    void moveToTop(size_t index);
    void moveToBottom(size_t index);
    void setPriority(size_t index, int priority);
    
    // Link grabber
    void grabLinksFromUrl(const std::string& pageUrl);
    void grabLinksFromText(const std::string& text);
    std::vector<std::string> getGrabbedLinks() const { return grabbedLinks; }
    void clearGrabbedLinks() { grabbedLinks.clear(); }
    
    // Auto-shutdown
    void setShutdownOnComplete(bool enable) { shutdownOnComplete = enable; }
    bool getShutdownOnComplete() const { return shutdownOnComplete; }
    
    // Scheduler
    bool isSchedulerActive() const;
    bool canDownloadNow() const;
    
    // Keyboard shortcuts handler
    void handleKeyboardShortcuts();
    
    // Verification
    void verifyAllChecksums();

private:
    std::vector<std::unique_ptr<DownloadItem>> downloads;
    std::vector<bool> selectedDownloads;
    std::mutex downloadsMutex;
    int maxConcurrentDownloads;
    int activeDownloads;
    
    FilterType currentFilter = FilterType::All;
    SortType currentSort = SortType::DateAdded;
    bool sortAscending = false;
    std::string searchQuery;
    
    DownloadStats stats;
    std::string clipboardUrl;
    std::string lastClipboardContent;
    
    void processQueue();
    std::string getFileNameFromUrl(const std::string& url);
    std::string getDownloadPath();
    std::string getCategoryPath(FileCategory category);
    std::vector<size_t> getFilteredIndices();
    bool matchesFilter(const DownloadItem& item) const;
    bool matchesSearch(const DownloadItem& item) const;
    bool isValidUrl(const std::string& url) const;
    void checkAutoShutdown();
    void performShutdown();
    std::vector<std::string> extractLinksFromHtml(const std::string& html);
    
    // History
    std::vector<HistoryEntry> history;
    
    // Link grabber
    std::vector<std::string> grabbedLinks;
    bool isGrabbing = false;
    std::string grabStatus;
    
    // Auto-shutdown
    bool shutdownOnComplete = false;
    bool shutdownTriggered = false;
    
    // Context menu state
    int contextMenuIndex = -1;
    
    // Details panel state
    int detailsIndex = -1;
    
    // Torrent manager
    TorrentManager torrentManager;
public:
    TorrentManager& getTorrentManager() { return torrentManager; }
};

#endif // DOWNLOADMANAGER_H
