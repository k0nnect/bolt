#include "downloadmanager.h"
#include "torrentmanager.h"
#include "settings.h"
#include "imgui.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <regex>
#include <cstring>
#include <set>
#include <curl/curl.h>
#include <commdlg.h>

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <powrprof.h>
#include <mmsystem.h>
#pragma comment(lib, "PowrProf.lib")
#pragma comment(lib, "winmm.lib")
#else
#include <pwd.h>
#include <unistd.h>
#endif

// Helper function to play completion sound
static void playCompletionSound() {
    Settings& settings = Settings::getInstance();
    if (!settings.playSoundOnComplete) return;
    
#ifdef _WIN32
    if (settings.completionSoundPath.empty()) {
        // Play system default notification sound
        PlaySoundA("SystemAsterisk", NULL, SND_ALIAS | SND_ASYNC);
    } else {
        // Play custom sound file
        PlaySoundA(settings.completionSoundPath.c_str(), NULL, SND_FILENAME | SND_ASYNC);
    }
#endif
}

DownloadManager::DownloadManager() 
    : maxConcurrentDownloads(Settings::getInstance().maxConcurrentDownloads)
    , activeDownloads(0)
{
    loadHistory();
    loadDashboardStats();
    stats.sessionStart = std::chrono::steady_clock::now();
}

DownloadManager::~DownloadManager() {
    saveHistory();
    for (auto& download : downloads) {
        if (download) {
            download->cancel();
        }
    }
}

std::string DownloadManager::getDownloadPath() {
    return Settings::getInstance().downloadPath;
}

std::string DownloadManager::getFileNameFromUrl(const std::string& url) {
    size_t lastSlash = url.find_last_of("/");
    if (lastSlash != std::string::npos && lastSlash < url.length() - 1) {
        std::string fileName = url.substr(lastSlash + 1);
        size_t queryPos = fileName.find("?");
        if (queryPos != std::string::npos) {
            fileName = fileName.substr(0, queryPos);
        }
        // URL decode
        std::string decoded;
        for (size_t i = 0; i < fileName.length(); ++i) {
            if (fileName[i] == '%' && i + 2 < fileName.length()) {
                int value;
                std::istringstream is(fileName.substr(i + 1, 2));
                if (is >> std::hex >> value) {
                    decoded += static_cast<char>(value);
                    i += 2;
                } else {
                    decoded += fileName[i];
                }
            } else if (fileName[i] == '+') {
                decoded += ' ';
            } else {
                decoded += fileName[i];
            }
        }
        fileName = decoded;
        
        if (!fileName.empty() && fileName.find('.') != std::string::npos) {
            return fileName;
        }
    }
    return "download";
}

bool DownloadManager::isValidUrl(const std::string& url) const {
    if (url.empty()) return false;
    
    // Basic URL validation
    std::regex urlPattern(R"(^(https?|ftp)://[^\s/$.?#].[^\s]*$)", std::regex::icase);
    return std::regex_match(url, urlPattern);
}

void DownloadManager::addDownload(const std::string& url, const std::string& customPath) {
    if (!isValidUrl(url)) {
        return;
    }
    
    std::string fileName = getFileNameFromUrl(url);
    std::string downloadDir = customPath.empty() ? getDownloadPath() : customPath;
    std::string filePath = downloadDir + "/" + fileName;
    
    // Handle duplicate filenames
    std::filesystem::path path(filePath);
    if (std::filesystem::exists(path)) {
        int counter = 1;
        std::string baseName = path.stem().string();
        std::string extension = path.extension().string();
        std::string dir = path.parent_path().string();
        
        do {
            filePath = dir + "/" + baseName + " (" + std::to_string(counter) + ")" + extension;
            path = std::filesystem::path(filePath);
            counter++;
        } while (std::filesystem::exists(path));
    }
    
    std::lock_guard<std::mutex> lock(downloadsMutex);
    auto download = std::make_unique<DownloadItem>(url, filePath);
    downloads.push_back(std::move(download));
    selectedDownloads.push_back(false);
    
    if (Settings::getInstance().autoStartDownloads) {
        processQueue();
    }
}

void DownloadManager::addDownloads(const std::vector<std::string>& urls) {
    for (const auto& url : urls) {
        addDownload(url);
    }
}

void DownloadManager::pauseDownload(size_t index) {
    std::lock_guard<std::mutex> lock(downloadsMutex);
    if (index < downloads.size() && downloads[index]) {
        downloads[index]->pause();
    }
    processQueue();
}

void DownloadManager::resumeDownload(size_t index) {
    std::lock_guard<std::mutex> lock(downloadsMutex);
    if (index < downloads.size() && downloads[index]) {
        downloads[index]->resume();
    }
}

void DownloadManager::cancelDownload(size_t index) {
    std::lock_guard<std::mutex> lock(downloadsMutex);
    if (index < downloads.size() && downloads[index]) {
        downloads[index]->cancel();
    }
    processQueue();
}

void DownloadManager::removeDownload(size_t index) {
    std::lock_guard<std::mutex> lock(downloadsMutex);
    if (index < downloads.size()) {
        if (downloads[index]) {
            downloads[index]->cancel();
        }
        downloads.erase(downloads.begin() + index);
        selectedDownloads.erase(selectedDownloads.begin() + index);
    }
}

void DownloadManager::retryDownload(size_t index) {
    std::lock_guard<std::mutex> lock(downloadsMutex);
    if (index < downloads.size() && downloads[index]) {
        downloads[index]->retry();
        processQueue();
    }
}

void DownloadManager::clearCompleted() {
    std::lock_guard<std::mutex> lock(downloadsMutex);
    for (size_t i = downloads.size(); i > 0; --i) {
        if (downloads[i-1] && downloads[i-1]->isCompleted()) {
            downloads.erase(downloads.begin() + (i-1));
            selectedDownloads.erase(selectedDownloads.begin() + (i-1));
        }
    }
}

void DownloadManager::clearAll() {
    std::lock_guard<std::mutex> lock(downloadsMutex);
    for (auto& download : downloads) {
        if (download) {
            download->cancel();
        }
    }
    downloads.clear();
    selectedDownloads.clear();
}

void DownloadManager::pauseAll() {
    std::lock_guard<std::mutex> lock(downloadsMutex);
    for (auto& download : downloads) {
        if (download && download->isActive()) {
            download->pause();
        }
    }
}

void DownloadManager::resumeAll() {
    std::lock_guard<std::mutex> lock(downloadsMutex);
    for (auto& download : downloads) {
        if (download && download->isPaused()) {
            download->resume();
        }
    }
    processQueue();
}

void DownloadManager::selectDownload(size_t index, bool selected) {
    if (index < selectedDownloads.size()) {
        selectedDownloads[index] = selected;
    }
}

void DownloadManager::selectAll() {
    for (size_t i = 0; i < selectedDownloads.size(); ++i) {
        selectedDownloads[i] = true;
    }
}

void DownloadManager::deselectAll() {
    for (size_t i = 0; i < selectedDownloads.size(); ++i) {
        selectedDownloads[i] = false;
    }
}

bool DownloadManager::isSelected(size_t index) const {
    return index < selectedDownloads.size() && selectedDownloads[index];
}

size_t DownloadManager::getSelectedCount() const {
    return std::count(selectedDownloads.begin(), selectedDownloads.end(), true);
}

void DownloadManager::pauseSelected() {
    for (size_t i = 0; i < selectedDownloads.size(); ++i) {
        if (selectedDownloads[i]) {
            pauseDownload(i);
        }
    }
}

void DownloadManager::resumeSelected() {
    for (size_t i = 0; i < selectedDownloads.size(); ++i) {
        if (selectedDownloads[i]) {
            resumeDownload(i);
        }
    }
}

void DownloadManager::cancelSelected() {
    for (size_t i = 0; i < selectedDownloads.size(); ++i) {
        if (selectedDownloads[i]) {
            cancelDownload(i);
        }
    }
}

void DownloadManager::removeSelected() {
    for (size_t i = selectedDownloads.size(); i > 0; --i) {
        if (selectedDownloads[i-1]) {
            removeDownload(i-1);
        }
    }
}

bool DownloadManager::matchesFilter(const DownloadItem& item) const {
    switch (currentFilter) {
        case FilterType::All: return true;
        case FilterType::Downloading: return item.isActive();
        case FilterType::Completed: return item.isCompleted();
        case FilterType::Queued: return item.isQueued();
        case FilterType::Paused: return item.isPaused();
        case FilterType::Error: return item.hasError();
    }
    return true;
}

bool DownloadManager::matchesSearch(const DownloadItem& item) const {
    if (searchQuery.empty()) return true;
    
    std::string fileName = item.getFileName();
    std::string query = searchQuery;
    
    // Case-insensitive search
    std::transform(fileName.begin(), fileName.end(), fileName.begin(), ::tolower);
    std::transform(query.begin(), query.end(), query.begin(), ::tolower);
    
    return fileName.find(query) != std::string::npos;
}

std::vector<size_t> DownloadManager::getFilteredIndices() {
    std::vector<size_t> indices;
    for (size_t i = 0; i < downloads.size(); ++i) {
        if (downloads[i] && matchesFilter(*downloads[i]) && matchesSearch(*downloads[i])) {
            indices.push_back(i);
        }
    }
    
    // Sort
    std::sort(indices.begin(), indices.end(), [this](size_t a, size_t b) {
        const auto& da = downloads[a];
        const auto& db = downloads[b];
        
        bool result = false;
        switch (currentSort) {
            case SortType::DateAdded:
                result = a < b;
                break;
            case SortType::Name:
                result = da->getFileName() < db->getFileName();
                break;
            case SortType::Size:
                result = da->getBytesTotal() < db->getBytesTotal();
                break;
            case SortType::Progress:
                result = da->getProgress() < db->getProgress();
                break;
            case SortType::Priority:
                result = da->getPriority() < db->getPriority();
                break;
        }
        
        return sortAscending ? result : !result;
    });
    
    return indices;
}

void DownloadManager::processQueue() {
    activeDownloads = 0;
    static std::set<std::string> notifiedCompleted;  // Track which downloads we've notified
    
    for (auto& download : downloads) {
        if (download && download->isActive()) {
            activeDownloads++;
        }
        
        // Check for newly completed downloads
        if (download && download->isCompleted()) {
            std::string key = download->getUrl();
            if (notifiedCompleted.find(key) == notifiedCompleted.end()) {
                notifiedCompleted.insert(key);
                
                // Play completion sound
                playCompletionSound();
                
                // Add to history
                addToHistory(*download);
            }
        }
    }
    
    // Start queued downloads
    for (auto& download : downloads) {
        if (download && download->isQueued() && 
            activeDownloads < maxConcurrentDownloads) {
            download->start();
            activeDownloads++;
        }
    }
}

void DownloadManager::updateStats() {
    stats.currentTotalSpeed = 0.0;
    
    for (auto& download : downloads) {
        if (download && download->isActive()) {
            stats.currentTotalSpeed += download->getCurrentSpeed();
        }
    }
    
    if (stats.currentTotalSpeed > stats.peakSpeed) {
        stats.peakSpeed = stats.currentTotalSpeed;
    }
    
    // Update speed history
    stats.speedHistory.push_back((float)stats.currentTotalSpeed);
    if (stats.speedHistory.size() > 120) {
        stats.speedHistory.pop_front();
    }
}

size_t DownloadManager::getActiveCount() const {
    size_t count = 0;
    for (const auto& d : downloads) {
        if (d && d->isActive()) count++;
    }
    return count;
}

size_t DownloadManager::getCompletedCount() const {
    size_t count = 0;
    for (const auto& d : downloads) {
        if (d && d->isCompleted()) count++;
    }
    return count;
}

size_t DownloadManager::getQueuedCount() const {
    size_t count = 0;
    for (const auto& d : downloads) {
        if (d && d->isQueued()) count++;
    }
    return count;
}

size_t DownloadManager::getPausedCount() const {
    size_t count = 0;
    for (const auto& d : downloads) {
        if (d && d->isPaused()) count++;
    }
    return count;
}

size_t DownloadManager::getErrorCount() const {
    size_t count = 0;
    for (const auto& d : downloads) {
        if (d && d->hasError()) count++;
    }
    return count;
}

void DownloadManager::checkClipboard() {
    if (!Settings::getInstance().clipboardMonitoring) {
        return;
    }
    
#ifdef _WIN32
    if (!OpenClipboard(nullptr)) return;
    
    HANDLE hData = GetClipboardData(CF_TEXT);
    if (hData != nullptr) {
        char* pszText = static_cast<char*>(GlobalLock(hData));
        if (pszText != nullptr) {
            std::string content(pszText);
            GlobalUnlock(hData);
            
            if (content != lastClipboardContent) {
                lastClipboardContent = content;
                if (isValidUrl(content)) {
                    clipboardUrl = content;
                }
            }
        }
    }
    CloseClipboard();
#endif
}

void DownloadManager::render() {
    std::lock_guard<std::mutex> lock(downloadsMutex);
    updateStats();
    processQueue();
    
    Settings& settings = Settings::getInstance();
    auto indices = getFilteredIndices();
    
    if (indices.empty()) {
        // Empty state
        ImGui::BeginChild("empty_state", ImVec2(-1, -1), false);
        float windowWidth = ImGui::GetWindowWidth();
        float windowHeight = ImGui::GetWindowHeight();
        
        ImGui::SetCursorPos(ImVec2(windowWidth / 2 - 100, windowHeight / 2 - 40));
        ImGui::BeginGroup();
        ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "no downloads");
        ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.3f, 1.0f), "add a url to start downloading");
        ImGui::EndGroup();
        
        ImGui::EndChild();
        return;
    }
    
    // Download list
    ImGui::BeginChild("download_list", ImVec2(-1, -1), false);
    
    for (size_t i : indices) {
        if (downloads[i]) {
            downloads[i]->render(i, settings.compactMode);
        }
    }
    
    ImGui::EndChild();
}

void DownloadManager::renderSidebar() {
    ImGui::BeginChild("sidebar", ImVec2(-1, -1), true);
    
    // Logo/Title with lightning bolt
    ImGui::Spacing();
    ImGui::Spacing();
    
    // Draw lightning bolt logo
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    ImU32 boltColor = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.85f, 0.0f, 1.0f));
    
    // Lightning bolt
    ImVec2 points[7];
    float size = 24.0f;
    float offsetX = cursorPos.x + 10;
    float offsetY = cursorPos.y;
    points[0] = ImVec2(offsetX + size * 0.5f, offsetY);
    points[1] = ImVec2(offsetX + size * 0.2f, offsetY + size * 0.5f);
    points[2] = ImVec2(offsetX + size * 0.4f, offsetY + size * 0.5f);
    points[3] = ImVec2(offsetX, offsetY + size);
    points[4] = ImVec2(offsetX + size * 0.3f, offsetY + size * 0.6f);
    points[5] = ImVec2(offsetX + size * 0.2f, offsetY + size * 0.6f);
    points[6] = ImVec2(offsetX + size * 0.5f, offsetY);
    drawList->AddConvexPolyFilled(points, 7, boltColor);
    
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 45);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
    ImGui::TextColored(ImVec4(0.40f, 0.70f, 1.0f, 1.0f), "bolt");
    
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "download manager");
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Spacing();
    
    // Filter buttons - LARGER with better styling
    const char* filterNames[] = { "all", "active", "done", "queued", "paused", "errors" };
    FilterType filters[] = { FilterType::All, FilterType::Downloading, FilterType::Completed, 
                            FilterType::Queued, FilterType::Paused, FilterType::Error };
    size_t counts[] = { downloads.size(), getActiveCount(), getCompletedCount(), 
                       getQueuedCount(), getPausedCount(), getErrorCount() };
    
    // Colors for each filter
    ImVec4 filterColors[] = {
        ImVec4(0.5f, 0.5f, 0.5f, 1.0f),  // all - gray
        ImVec4(0.40f, 0.70f, 1.0f, 1.0f),  // downloading - blue
        ImVec4(0.4f, 1.0f, 0.4f, 1.0f),  // completed - green
        ImVec4(0.6f, 0.6f, 0.6f, 1.0f),  // queued - gray
        ImVec4(1.0f, 0.8f, 0.3f, 1.0f),  // paused - yellow
        ImVec4(1.0f, 0.4f, 0.4f, 1.0f)   // errors - red
    };
    
    for (int i = 0; i < 6; ++i) {
        bool selected = (currentFilter == filters[i]);
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.28f, 0.32f, 0.40f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, filterColors[i]);
        }
        
        char label[64];
        snprintf(label, sizeof(label), "%s (%zu)", filterNames[i], counts[i]);
        
        if (ImGui::Button(label, ImVec2(-1, 40))) {
            currentFilter = filters[i];
        }
        
        if (selected) {
            ImGui::PopStyleColor(2);
        }
        
        ImGui::Spacing();
    }
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Spacing();
    
    // Quick stats - LARGER
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "current speed");
    ImGui::Spacing();
    
    char speedBuf[64];
    if (stats.currentTotalSpeed >= 1024 * 1024) {
        snprintf(speedBuf, sizeof(speedBuf), "%.2f mb/s", stats.currentTotalSpeed / (1024 * 1024));
    } else if (stats.currentTotalSpeed >= 1024) {
        snprintf(speedBuf, sizeof(speedBuf), "%.2f kb/s", stats.currentTotalSpeed / 1024);
    } else {
        snprintf(speedBuf, sizeof(speedBuf), "%.0f b/s", stats.currentTotalSpeed);
    }
    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s", speedBuf);
    
    ImGui::Spacing();
    ImGui::Spacing();
    
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "peak speed");
    ImGui::Spacing();
    
    if (stats.peakSpeed >= 1024 * 1024) {
        snprintf(speedBuf, sizeof(speedBuf), "%.2f mb/s", stats.peakSpeed / (1024 * 1024));
    } else if (stats.peakSpeed >= 1024) {
        snprintf(speedBuf, sizeof(speedBuf), "%.2f kb/s", stats.peakSpeed / 1024);
    } else {
        snprintf(speedBuf, sizeof(speedBuf), "%.0f b/s", stats.peakSpeed);
    }
    ImGui::Text("%s", speedBuf);
    
    ImGui::EndChild();
}

// Helper to create auto-sized button with padding
static bool AutoButton(const char* label, float minWidth = 0, float height = 32) {
    float textWidth = ImGui::CalcTextSize(label).x;
    float buttonWidth = (minWidth > textWidth + 24) ? minWidth : (textWidth + 24);  // 12px padding on each side
    return ImGui::Button(label, ImVec2(buttonWidth, height));
}

void DownloadManager::renderToolbar() {
    // Search bar
    static char searchBuf[256] = "";
    ImGui::PushItemWidth(250);
    if (ImGui::InputTextWithHint("##search", "search...", searchBuf, sizeof(searchBuf))) {
        searchQuery = searchBuf;
    }
    ImGui::PopItemWidth();
    
    ImGui::SameLine();
    
    // Toolbar buttons - auto-sized
    if (AutoButton("pause all")) {
        pauseAll();
    }
    ImGui::SameLine();
    if (AutoButton("resume all")) {
        resumeAll();
    }
    ImGui::SameLine();
    if (AutoButton("clear done")) {
        clearCompleted();
    }
    
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(20, 0));
    ImGui::SameLine();
    
    // View mode toggle
    Settings& settings = Settings::getInstance();
    if (ImGui::Checkbox("compact view", &settings.compactMode)) {
        settings.save();
    }
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
}

void DownloadManager::renderStatistics() {
    ImGui::BeginChild("stats", ImVec2(-1, 100), true);
    
    ImGui::Columns(4, nullptr, false);
    
    // Active downloads
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "active");
    ImGui::Spacing();
    size_t active = getActiveCount();
    if (active > 0) {
        ImGui::TextColored(ImVec4(0.45f, 0.72f, 1.0f, 1.0f), "%zu", active);
    } else {
        ImGui::Text("%zu", active);
    }
    ImGui::NextColumn();
    
    // Completed
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "completed");
    ImGui::Spacing();
    size_t completed = getCompletedCount();
    if (completed > 0) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%zu", completed);
    } else {
        ImGui::Text("%zu", completed);
    }
    ImGui::NextColumn();
    
    // Current speed
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "speed");
    ImGui::Spacing();
    char speedBuf[64];
    if (stats.currentTotalSpeed >= 1024 * 1024) {
        snprintf(speedBuf, sizeof(speedBuf), "%.2f mb/s", stats.currentTotalSpeed / (1024 * 1024));
    } else {
        snprintf(speedBuf, sizeof(speedBuf), "%.2f kb/s", stats.currentTotalSpeed / 1024);
    }
    if (stats.currentTotalSpeed > 0) {
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s", speedBuf);
    } else {
        ImGui::Text("%s", speedBuf);
    }
    ImGui::NextColumn();
    
    // Peak speed
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "peak");
    ImGui::Spacing();
    if (stats.peakSpeed >= 1024 * 1024) {
        snprintf(speedBuf, sizeof(speedBuf), "%.2f mb/s", stats.peakSpeed / (1024 * 1024));
    } else {
        snprintf(speedBuf, sizeof(speedBuf), "%.2f kb/s", stats.peakSpeed / 1024);
    }
    ImGui::Text("%s", speedBuf);
    
    ImGui::Columns(1);
    ImGui::EndChild();
}

void DownloadManager::renderSpeedGraph() {
    if (!Settings::getInstance().showSpeedGraph || stats.speedHistory.empty()) {
        return;
    }
    
    ImGui::BeginChild("speed_graph", ImVec2(-1, 80), true);
    
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "speed history");
    
    std::vector<float> history(stats.speedHistory.begin(), stats.speedHistory.end());
    float maxSpeed = *std::max_element(history.begin(), history.end());
    if (maxSpeed < 1024) maxSpeed = 1024;  // Minimum scale
    
    ImGui::PlotLines("##speed", history.data(), (int)history.size(), 0, nullptr, 0.0f, maxSpeed, ImVec2(-1, 50));
    
    ImGui::EndChild();
}

void DownloadManager::renderSettingsPanel(bool* open) {
    if (!*open) return;
    
    Settings& settings = Settings::getInstance();
    
    ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("settings", open)) {
        
        if (ImGui::BeginTabBar("settings_tabs")) {
            
            if (ImGui::BeginTabItem("general")) {
                ImGui::Text("download path:");
                static char pathBuf[512];
                strncpy(pathBuf, settings.downloadPath.c_str(), sizeof(pathBuf));
                ImGui::PushItemWidth(-80);
                if (ImGui::InputText("##path", pathBuf, sizeof(pathBuf))) {
                    settings.downloadPath = pathBuf;
                }
                ImGui::PopItemWidth();
                ImGui::SameLine();
                if (ImGui::Button("browse", ImVec2(70, 0))) {
#ifdef _WIN32
                    // Use Windows folder picker
                    IFileDialog *pfd = NULL;
                    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) {
                        DWORD dwOptions;
                        if (SUCCEEDED(pfd->GetOptions(&dwOptions))) {
                            pfd->SetOptions(dwOptions | FOS_PICKFOLDERS);
                        }
                        if (SUCCEEDED(pfd->Show(NULL))) {
                            IShellItem *psi;
                            if (SUCCEEDED(pfd->GetResult(&psi))) {
                                PWSTR pszPath = NULL;
                                if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
                                    int size_needed = WideCharToMultiByte(CP_UTF8, 0, pszPath, -1, NULL, 0, NULL, NULL);
                                    std::string result(size_needed, 0);
                                    WideCharToMultiByte(CP_UTF8, 0, pszPath, -1, &result[0], size_needed, NULL, NULL);
                                    if (!result.empty() && result.back() == '\0') result.pop_back();
                                    settings.downloadPath = result;
                                    strncpy(pathBuf, result.c_str(), sizeof(pathBuf));
                                    CoTaskMemFree(pszPath);
                                }
                                psi->Release();
                            }
                        }
                        pfd->Release();
                    }
#endif
                }
                
                ImGui::Spacing();
                
                ImGui::SliderInt("concurrent downloads", &settings.maxConcurrentDownloads, 1, 10);
                ImGui::SliderInt("speed limit (kb/s, 0=unlimited)", &settings.speedLimitKBps, 0, 10240);
                
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "behavior:");
                ImGui::Checkbox("auto-start downloads", &settings.autoStartDownloads);
                ImGui::Checkbox("monitor clipboard for urls", &settings.clipboardMonitoring);
                ImGui::Checkbox("show notifications", &settings.showNotifications);
                ImGui::Checkbox("play sound on completion", &settings.playSoundOnComplete);
                ImGui::Checkbox("auto-categorize files", &settings.autoCategorize);
                
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("connections")) {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "connection settings:");
                ImGui::Spacing();
                
                ImGui::SliderInt("connections per download", &settings.connectionsPerDownload, 1, 16);
                ImGui::SliderInt("connection timeout (sec)", &settings.connectionTimeout, 10, 120);
                ImGui::SliderInt("retry attempts", &settings.retryAttempts, 0, 10);
                ImGui::SliderInt("retry delay (sec)", &settings.retryDelay, 1, 60);
                
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "bandwidth allocation:");
                ImGui::Checkbox("enable bandwidth allocation", &settings.enableBandwidthAllocation);
                
                if (settings.enableBandwidthAllocation) {
                    const char* modes[] = { "equal distribution", "by priority" };
                    ImGui::Combo("allocation mode", &settings.bandwidthAllocationMode, modes, 2);
                }
                
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("proxy")) {
                ImGui::Checkbox("use proxy", &settings.useProxy);
                
                if (settings.useProxy) {
                    ImGui::Spacing();
                    
                    ImGui::Text("proxy address:");
                    static char proxyAddr[256];
                    strncpy(proxyAddr, settings.proxyAddress.c_str(), sizeof(proxyAddr));
                    if (ImGui::InputText("##proxyaddr", proxyAddr, sizeof(proxyAddr))) {
                        settings.proxyAddress = proxyAddr;
                    }
                    
                    ImGui::Text("proxy port:");
                    ImGui::InputInt("##proxyport", &settings.proxyPort);
                    
                    ImGui::Spacing();
                    ImGui::Checkbox("proxy authentication", &settings.proxyAuth);
                    
                    if (settings.proxyAuth) {
                        ImGui::Spacing();
                        
                        ImGui::Text("username:");
                        static char proxyUser[128];
                        strncpy(proxyUser, settings.proxyUsername.c_str(), sizeof(proxyUser));
                        if (ImGui::InputText("##proxyuser", proxyUser, sizeof(proxyUser))) {
                            settings.proxyUsername = proxyUser;
                        }
                        
                        ImGui::Text("password:");
                        static char proxyPass[128];
                        strncpy(proxyPass, settings.proxyPassword.c_str(), sizeof(proxyPass));
                        if (ImGui::InputText("##proxypass", proxyPass, sizeof(proxyPass), ImGuiInputTextFlags_Password)) {
                            settings.proxyPassword = proxyPass;
                        }
                    }
                }
                
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("appearance")) {
                const char* themes[] = { "modern dark", "light", "midnight blue", "forest" };
                ImGui::Combo("theme", &settings.theme, themes, 4);
                
                ImGui::SliderFloat("ui scale", &settings.uiScale, 0.8f, 2.0f);
                ImGui::Checkbox("show speed graph", &settings.showSpeedGraph);
                ImGui::Checkbox("compact mode", &settings.compactMode);
                
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("scheduler")) {
                ImGui::Checkbox("enable scheduler", &settings.schedulerEnabled);
                
                if (settings.schedulerEnabled) {
                    ImGui::Text("download only between:");
                    ImGui::SliderInt("start hour", &settings.schedulerStartHour, 0, 23);
                    ImGui::SliderInt("end hour", &settings.schedulerEndHour, 0, 23);
                }
                
                ImGui::EndTabItem();
            }
            
            ImGui::EndTabBar();
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        if (ImGui::Button("save settings", ImVec2(120, 32))) {
            settings.save();
            maxConcurrentDownloads = settings.maxConcurrentDownloads;
        }
        ImGui::SameLine();
        if (ImGui::Button("close", ImVec2(120, 32))) {
            *open = false;
        }
    }
    ImGui::End();
}

void DownloadManager::renderAddDownloadDialog(bool* open, char* urlBuffer, size_t bufferSize) {
    if (!*open) return;
    
    ImGui::SetNextWindowSize(ImVec2(500, 200), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("add download", open)) {
        ImGui::Text("url:");
        ImGui::PushItemWidth(-1);
        bool submitted = ImGui::InputTextWithHint("##url", "https://example.com/file.zip", 
            urlBuffer, bufferSize, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopItemWidth();
        
        ImGui::Spacing();
        
        static char pathBuf[512] = "";
        if (pathBuf[0] == '\0') {
            strncpy(pathBuf, Settings::getInstance().downloadPath.c_str(), sizeof(pathBuf));
        }
        
        ImGui::Text("save to:");
        ImGui::PushItemWidth(-1);
        ImGui::InputText("##saveto", pathBuf, sizeof(pathBuf));
        ImGui::PopItemWidth();
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        if (ImGui::Button("download", ImVec2(100, 0)) || submitted) {
            if (strlen(urlBuffer) > 0) {
                addDownload(std::string(urlBuffer), std::string(pathBuf));
                urlBuffer[0] = '\0';
                *open = false;
            }
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button("cancel", ImVec2(100, 0))) {
            *open = false;
        }
    }
    ImGui::End();
}

void DownloadManager::loadHistory() {
    Settings& settings = Settings::getInstance();
    std::string historyPath = settings.getConfigPath() + "/history.txt";
    
    if (!std::filesystem::exists(historyPath)) return;
    
    std::ifstream file(historyPath);
    if (!file.is_open()) return;
    
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        HistoryEntry entry;
        std::istringstream iss(line);
        std::string token;
        
        if (std::getline(iss, token, '\t')) entry.url = token;
        if (std::getline(iss, token, '\t')) entry.fileName = token;
        if (std::getline(iss, token, '\t')) entry.filePath = token;
        if (std::getline(iss, token, '\t')) entry.fileSize = std::stoull(token);
        if (std::getline(iss, token, '\t')) entry.dateCompleted = token;
        if (std::getline(iss, token, '\t')) entry.checksum = token;
        if (std::getline(iss, token, '\t')) entry.category = static_cast<FileCategory>(std::stoi(token));
        
        history.push_back(entry);
    }
    
    // Load stats
    std::string statsPath = settings.getConfigPath() + "/stats.txt";
    std::ifstream statsFile(statsPath);
    if (statsFile.is_open()) {
        statsFile >> stats.allTimeBytesDownloaded;
        statsFile >> stats.allTimeDownloadsCompleted;
    }
}

void DownloadManager::saveHistory() {
    Settings& settings = Settings::getInstance();
    std::filesystem::create_directories(settings.getConfigPath());
    
    std::string historyPath = settings.getConfigPath() + "/history.txt";
    std::ofstream file(historyPath);
    if (!file.is_open()) return;
    
    for (const auto& entry : history) {
        file << entry.url << "\t"
             << entry.fileName << "\t"
             << entry.filePath << "\t"
             << entry.fileSize << "\t"
             << entry.dateCompleted << "\t"
             << entry.checksum << "\t"
             << static_cast<int>(entry.category) << "\n";
    }
    
    // Save stats
    std::string statsPath = settings.getConfigPath() + "/stats.txt";
    std::ofstream statsFile(statsPath);
    if (statsFile.is_open()) {
        statsFile << stats.allTimeBytesDownloaded << "\n";
        statsFile << stats.allTimeDownloadsCompleted << "\n";
    }
}

void DownloadManager::addToHistory(const DownloadItem& item) {
    HistoryEntry entry;
    entry.url = item.getUrl();
    entry.fileName = item.getFileName();
    entry.filePath = item.getFilePath();
    entry.fileSize = item.getBytesTotal();
    
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&time));
    entry.dateCompleted = buf;
    
    entry.checksum = item.getChecksumResult();
    entry.category = item.getCategory();
    
    history.push_back(entry);
    
    stats.allTimeBytesDownloaded += entry.fileSize;
    stats.allTimeDownloadsCompleted++;
    
    // Record for dashboard statistics
    recordDownloadCompletion(item);
    
    saveHistory();
}

void DownloadManager::clearHistory() {
    history.clear();
    saveHistory();
}

std::string DownloadManager::getCategoryPath(FileCategory category) {
    Settings& settings = Settings::getInstance();
    if (!settings.autoCategorize) {
        return settings.downloadPath;
    }
    
    std::string subfolder;
    switch (category) {
        case FileCategory::Video: subfolder = settings.videoFolder; break;
        case FileCategory::Audio: subfolder = settings.audioFolder; break;
        case FileCategory::Document: subfolder = settings.documentFolder; break;
        case FileCategory::Archive: subfolder = settings.archiveFolder; break;
        case FileCategory::Program: subfolder = settings.programFolder; break;
        case FileCategory::Image: subfolder = settings.imageFolder; break;
        default: subfolder = settings.otherFolder; break;
    }
    
    if (subfolder.empty()) {
        return settings.downloadPath;
    }
    
    std::string path = settings.downloadPath + "/" + subfolder;
    std::filesystem::create_directories(path);
    return path;
}

void DownloadManager::moveUp(size_t index) {
    std::lock_guard<std::mutex> lock(downloadsMutex);
    if (index > 0 && index < downloads.size()) {
        std::swap(downloads[index], downloads[index - 1]);
        std::swap(selectedDownloads[index], selectedDownloads[index - 1]);
    }
}

void DownloadManager::moveDown(size_t index) {
    std::lock_guard<std::mutex> lock(downloadsMutex);
    if (index < downloads.size() - 1) {
        std::swap(downloads[index], downloads[index + 1]);
        std::swap(selectedDownloads[index], selectedDownloads[index + 1]);
    }
}

void DownloadManager::moveToTop(size_t index) {
    std::lock_guard<std::mutex> lock(downloadsMutex);
    if (index > 0 && index < downloads.size()) {
        auto item = std::move(downloads[index]);
        bool selected = selectedDownloads[index];
        downloads.erase(downloads.begin() + index);
        selectedDownloads.erase(selectedDownloads.begin() + index);
        downloads.insert(downloads.begin(), std::move(item));
        selectedDownloads.insert(selectedDownloads.begin(), selected);
    }
}

void DownloadManager::moveToBottom(size_t index) {
    std::lock_guard<std::mutex> lock(downloadsMutex);
    if (index < downloads.size() - 1) {
        auto item = std::move(downloads[index]);
        bool selected = selectedDownloads[index];
        downloads.erase(downloads.begin() + index);
        selectedDownloads.erase(selectedDownloads.begin() + index);
        downloads.push_back(std::move(item));
        selectedDownloads.push_back(selected);
    }
}

void DownloadManager::setPriority(size_t index, int priority) {
    std::lock_guard<std::mutex> lock(downloadsMutex);
    if (index < downloads.size() && downloads[index]) {
        downloads[index]->setPriority(priority);
    }
}

void DownloadManager::exportDownloads(const std::string& path) {
    std::ofstream file(path);
    if (!file.is_open()) return;
    
    file << "# Bolt Download Manager Export\n";
    file << "# Format: URL|FileName|Status\n\n";
    
    for (const auto& download : downloads) {
        if (download) {
            file << download->getUrl() << "|"
                 << download->getFileName() << "|"
                 << download->getStatus() << "\n";
        }
    }
}

void DownloadManager::importDownloads(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return;
    
    std::string line;
    std::vector<std::string> urls;
    
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        size_t pipePos = line.find('|');
        if (pipePos != std::string::npos) {
            urls.push_back(line.substr(0, pipePos));
        } else if (isValidUrl(line)) {
            urls.push_back(line);
        }
    }
    
    addDownloads(urls);
}

void DownloadManager::exportHistory(const std::string& path) {
    std::ofstream file(path);
    if (!file.is_open()) return;
    
    file << "URL,FileName,FilePath,Size,DateCompleted,Checksum\n";
    
    for (const auto& entry : history) {
        file << "\"" << entry.url << "\","
             << "\"" << entry.fileName << "\","
             << "\"" << entry.filePath << "\","
             << entry.fileSize << ","
             << "\"" << entry.dateCompleted << "\","
             << "\"" << entry.checksum << "\"\n";
    }
}

void DownloadManager::grabLinksFromUrl(const std::string& pageUrl) {
    isGrabbing = true;
    grabStatus = "fetching page...";
    grabbedLinks.clear();
    
    // Use a thread to fetch the page
    std::thread([this, pageUrl]() {
        CURL* curl = curl_easy_init();
        if (!curl) {
            grabStatus = "error: failed to initialize curl";
            isGrabbing = false;
            return;
        }
        
        std::string response;
        curl_easy_setopt(curl, CURLOPT_URL, pageUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, [](void* ptr, size_t size, size_t nmemb, std::string* data) -> size_t {
            data->append((char*)ptr, size * nmemb);
            return size * nmemb;
        });
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        
        if (res == CURLE_OK) {
            grabbedLinks = extractLinksFromHtml(response);
            grabStatus = "found " + std::to_string(grabbedLinks.size()) + " links";
        } else {
            grabStatus = "error: " + std::string(curl_easy_strerror(res));
        }
        
        isGrabbing = false;
    }).detach();
}

void DownloadManager::grabLinksFromText(const std::string& text) {
    grabbedLinks.clear();
    
    // URL regex pattern
    std::regex urlPattern(R"((https?://[^\s<>"{}|\\^`\[\]]+))");
    std::sregex_iterator begin(text.begin(), text.end(), urlPattern);
    std::sregex_iterator end;
    
    for (auto it = begin; it != end; ++it) {
        std::string url = it->str();
        // Filter out common non-downloadable URLs
        if (url.find(".html") == std::string::npos &&
            url.find(".htm") == std::string::npos &&
            url.find(".php") == std::string::npos &&
            url.find(".asp") == std::string::npos) {
            grabbedLinks.push_back(url);
        }
    }
    
    grabStatus = "found " + std::to_string(grabbedLinks.size()) + " links";
}

std::vector<std::string> DownloadManager::extractLinksFromHtml(const std::string& html) {
    std::vector<std::string> links;
    
    // Pattern to find href and src attributes
    std::regex linkPattern(R"((?:href|src)\s*=\s*["']([^"']+)["'])");
    std::sregex_iterator begin(html.begin(), html.end(), linkPattern);
    std::sregex_iterator end;
    
    std::set<std::string> uniqueLinks;
    
    for (auto it = begin; it != end; ++it) {
        std::string url = (*it)[1].str();
        
        // Skip non-file URLs
        if (url.empty() || url[0] == '#' || url[0] == '?' ||
            url.find("javascript:") != std::string::npos ||
            url.find("mailto:") != std::string::npos) {
            continue;
        }
        
        // Check for downloadable file extensions
        std::vector<std::string> downloadableExts = {
            ".zip", ".rar", ".7z", ".tar", ".gz", ".bz2",
            ".exe", ".msi", ".dmg", ".deb", ".rpm",
            ".pdf", ".doc", ".docx", ".xls", ".xlsx",
            ".mp3", ".wav", ".flac", ".aac", ".ogg",
            ".mp4", ".mkv", ".avi", ".mov", ".wmv", ".webm",
            ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".webp",
            ".iso", ".img", ".bin"
        };
        
        bool isDownloadable = false;
        std::string lowerUrl = url;
        std::transform(lowerUrl.begin(), lowerUrl.end(), lowerUrl.begin(), ::tolower);
        
        for (const auto& ext : downloadableExts) {
            if (lowerUrl.find(ext) != std::string::npos) {
                isDownloadable = true;
                break;
            }
        }
        
        if (isDownloadable && uniqueLinks.find(url) == uniqueLinks.end()) {
            uniqueLinks.insert(url);
            links.push_back(url);
        }
    }
    
    return links;
}

bool DownloadManager::isSchedulerActive() const {
    return Settings::getInstance().schedulerEnabled;
}

bool DownloadManager::canDownloadNow() const {
    Settings& settings = Settings::getInstance();
    
    if (!settings.schedulerEnabled) {
        return true;
    }
    
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    struct tm* tm_info = localtime(&time);
    int currentHour = tm_info->tm_hour;
    
    if (settings.schedulerStartHour <= settings.schedulerEndHour) {
        return currentHour >= settings.schedulerStartHour && currentHour < settings.schedulerEndHour;
    } else {
        // Overnight schedule (e.g., 22:00 to 06:00)
        return currentHour >= settings.schedulerStartHour || currentHour < settings.schedulerEndHour;
    }
}

void DownloadManager::checkAutoShutdown() {
    if (!shutdownOnComplete || shutdownTriggered) return;
    
    // Check if all downloads are complete
    bool allComplete = true;
    bool hasDownloads = false;
    
    for (const auto& download : downloads) {
        if (download) {
            hasDownloads = true;
            if (!download->isCompleted() && !download->isCancelled() && !download->hasError()) {
                allComplete = false;
                break;
            }
        }
    }
    
    if (hasDownloads && allComplete) {
        shutdownTriggered = true;
        performShutdown();
    }
}

void DownloadManager::performShutdown() {
#ifdef _WIN32
    Settings& settings = Settings::getInstance();
    
    // Show warning dialog
    int result = MessageBoxA(NULL, 
        "All downloads complete. The computer will shut down in 60 seconds.\n\nClick Cancel to abort.",
        "Bolt - Auto Shutdown", 
        MB_OKCANCEL | MB_ICONWARNING);
    
    if (result == IDCANCEL) {
        shutdownTriggered = false;
        shutdownOnComplete = false;
        return;
    }
    
    switch (settings.shutdownAction) {
        case 0:  // Shutdown
            system("shutdown /s /t 60");
            break;
        case 1:  // Sleep
            SetSuspendState(FALSE, TRUE, FALSE);
            break;
        case 2:  // Hibernate
            SetSuspendState(TRUE, TRUE, FALSE);
            break;
    }
#endif
}

void DownloadManager::handleKeyboardShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    
    // Ctrl+V - Paste URL from clipboard
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V)) {
        checkClipboard();
    }
    
    // Ctrl+A - Select all
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A)) {
        selectAll();
    }
    
    // Delete - Remove selected
    if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        removeSelected();
    }
    
    // Ctrl+P - Pause selected
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_P)) {
        pauseSelected();
    }
    
    // Ctrl+R - Resume selected
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_R)) {
        resumeSelected();
    }
}

void DownloadManager::verifyAllChecksums() {
    for (auto& download : downloads) {
        if (download && download->isCompleted()) {
            download->verifyChecksum();
        }
    }
}

void DownloadManager::renderBatchImportDialog(bool* open) {
    if (!*open) return;
    
    ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("batch import", open)) {
        ImGui::Text("paste urls (one per line) or drag and drop a text file:");
        ImGui::Spacing();
        
        static char batchBuffer[16384] = "";
        ImGui::InputTextMultiline("##batchurls", batchBuffer, sizeof(batchBuffer), 
            ImVec2(-1, 300), ImGuiInputTextFlags_AllowTabInput);
        
        ImGui::Spacing();
        
        static int urlCount = 0;
        if (ImGui::Button("count urls", ImVec2(120, 32))) {
            urlCount = 0;
            std::istringstream iss(batchBuffer);
            std::string line;
            while (std::getline(iss, line)) {
                if (!line.empty() && isValidUrl(line)) {
                    urlCount++;
                }
            }
        }
        ImGui::SameLine();
        ImGui::Text("valid urls: %d", urlCount);
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        if (ImGui::Button("import all", ImVec2(120, 32))) {
            std::vector<std::string> urls;
            std::istringstream iss(batchBuffer);
            std::string line;
            while (std::getline(iss, line)) {
                // Trim whitespace
                line.erase(0, line.find_first_not_of(" \t\r\n"));
                line.erase(line.find_last_not_of(" \t\r\n") + 1);
                
                if (!line.empty() && isValidUrl(line)) {
                    urls.push_back(line);
                }
            }
            addDownloads(urls);
            batchBuffer[0] = '\0';
            urlCount = 0;
            *open = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("clear", ImVec2(120, 32))) {
            batchBuffer[0] = '\0';
            urlCount = 0;
        }
        ImGui::SameLine();
        if (ImGui::Button("cancel", ImVec2(120, 32))) {
            *open = false;
        }
    }
    ImGui::End();
}

void DownloadManager::renderHistoryPanel(bool* open) {
    if (!*open) return;
    
    ImGui::SetNextWindowSize(ImVec2(800, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("download history", open)) {
        
        // Stats header
        ImGui::Text("all-time downloads: %d", stats.allTimeDownloadsCompleted);
        ImGui::SameLine(300);
        char sizeBuf[64];
        if (stats.allTimeBytesDownloaded >= 1024ULL * 1024 * 1024) {
            snprintf(sizeBuf, sizeof(sizeBuf), "%.2f gb", stats.allTimeBytesDownloaded / (1024.0 * 1024.0 * 1024.0));
        } else if (stats.allTimeBytesDownloaded >= 1024ULL * 1024) {
            snprintf(sizeBuf, sizeof(sizeBuf), "%.2f mb", stats.allTimeBytesDownloaded / (1024.0 * 1024.0));
        } else {
            snprintf(sizeBuf, sizeof(sizeBuf), "%.2f kb", stats.allTimeBytesDownloaded / 1024.0);
        }
        ImGui::Text("total downloaded: %s", sizeBuf);
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        // History list
        if (ImGui::BeginTable("history_table", 5, 
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY)) {
            
            ImGui::TableSetupColumn("file name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("size", ImGuiTableColumnFlags_WidthFixed, 100);
            ImGui::TableSetupColumn("date", ImGuiTableColumnFlags_WidthFixed, 150);
            ImGui::TableSetupColumn("category", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("actions", ImGuiTableColumnFlags_WidthFixed, 120);
            ImGui::TableHeadersRow();
            
            for (size_t i = 0; i < history.size(); ++i) {
                const auto& entry = history[i];
                ImGui::TableNextRow();
                
                ImGui::TableNextColumn();
                ImGui::Text("%s", entry.fileName.c_str());
                
                ImGui::TableNextColumn();
                if (entry.fileSize >= 1024ULL * 1024 * 1024) {
                    ImGui::Text("%.2f gb", entry.fileSize / (1024.0 * 1024.0 * 1024.0));
                } else if (entry.fileSize >= 1024ULL * 1024) {
                    ImGui::Text("%.2f mb", entry.fileSize / (1024.0 * 1024.0));
                } else {
                    ImGui::Text("%.2f kb", entry.fileSize / 1024.0);
                }
                
                ImGui::TableNextColumn();
                ImGui::Text("%s", entry.dateCompleted.c_str());
                
                ImGui::TableNextColumn();
                const char* catNames[] = {"video", "audio", "doc", "archive", "app", "image", "other"};
                ImGui::Text("%s", catNames[static_cast<int>(entry.category)]);
                
                ImGui::TableNextColumn();
                ImGui::PushID((int)i);
                if (ImGui::SmallButton("redownload")) {
                    addDownload(entry.url);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("open")) {
#ifdef _WIN32
                    ShellExecuteA(NULL, "open", entry.filePath.c_str(), NULL, NULL, SW_SHOWNORMAL);
#endif
                }
                ImGui::PopID();
            }
            
            ImGui::EndTable();
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        if (ImGui::Button("export csv", ImVec2(120, 32))) {
#ifdef _WIN32
            OPENFILENAMEA ofn = {0};
            char szFile[MAX_PATH] = "history.csv";
            ofn.lStructSize = sizeof(ofn);
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = sizeof(szFile);
            ofn.lpstrFilter = "CSV Files\0*.csv\0All Files\0*.*\0";
            ofn.lpstrDefExt = "csv";
            ofn.Flags = OFN_OVERWRITEPROMPT;
            
            if (GetSaveFileNameA(&ofn)) {
                exportHistory(szFile);
            }
#endif
        }
        ImGui::SameLine();
        if (ImGui::Button("clear history", ImVec2(120, 32))) {
            clearHistory();
        }
    }
    ImGui::End();
}

void DownloadManager::renderLinkGrabberDialog(bool* open) {
    if (!*open) return;
    
    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("link grabber", open)) {
        ImGui::Text("extract downloadable links from a webpage:");
        ImGui::Spacing();
        
        static char urlBuffer[2048] = "";
        ImGui::PushItemWidth(-120);
        ImGui::InputTextWithHint("##graburl", "https://example.com/downloads/", urlBuffer, sizeof(urlBuffer));
        ImGui::PopItemWidth();
        ImGui::SameLine();
        
        if (isGrabbing) {
            ImGui::BeginDisabled();
            ImGui::Button("grabbing...", ImVec2(100, 0));
            ImGui::EndDisabled();
        } else {
            if (ImGui::Button("grab links", ImVec2(100, 0))) {
                if (strlen(urlBuffer) > 0) {
                    grabLinksFromUrl(urlBuffer);
                }
            }
        }
        
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "status: %s", grabStatus.c_str());
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        // Link list with checkboxes
        static std::vector<char> selectedLinks;  // Use char instead of bool for ImGui compatibility
        if (selectedLinks.size() != grabbedLinks.size()) {
            selectedLinks.resize(grabbedLinks.size(), 1);  // 1 = selected by default
        }
        
        ImGui::Text("found links:");
        ImGui::BeginChild("links_list", ImVec2(-1, 250), true);
        
        for (size_t i = 0; i < grabbedLinks.size(); ++i) {
            ImGui::PushID((int)i);
            bool isSelected = (selectedLinks[i] != 0);
            if (ImGui::Checkbox("##sel", &isSelected)) {
                selectedLinks[i] = isSelected ? 1 : 0;
            }
            ImGui::SameLine();
            
            std::string displayUrl = grabbedLinks[i];
            if (displayUrl.length() > 80) {
                displayUrl = displayUrl.substr(0, 77) + "...";
            }
            ImGui::Text("%s", displayUrl.c_str());
            ImGui::PopID();
        }
        
        ImGui::EndChild();
        
        ImGui::Spacing();
        
        // Selection buttons
        if (ImGui::Button("select all", ImVec2(100, 28))) {
            std::fill(selectedLinks.begin(), selectedLinks.end(), 1);
        }
        ImGui::SameLine();
        if (ImGui::Button("select none", ImVec2(100, 28))) {
            std::fill(selectedLinks.begin(), selectedLinks.end(), 0);
        }
        ImGui::SameLine();
        
        // Count selected
        int selectedCount = 0;
        for (char sel : selectedLinks) if (sel) selectedCount++;
        ImGui::Text("selected: %d / %zu", selectedCount, grabbedLinks.size());
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        if (ImGui::Button("download selected", ImVec2(150, 32))) {
            std::vector<std::string> urls;
            for (size_t i = 0; i < grabbedLinks.size(); ++i) {
                if (selectedLinks[i]) {
                    urls.push_back(grabbedLinks[i]);
                }
            }
            addDownloads(urls);
            *open = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("cancel", ImVec2(100, 32))) {
            *open = false;
        }
    }
    ImGui::End();
}

void DownloadManager::renderDownloadDetailsPanel(bool* open, size_t index) {
    if (!*open || index >= downloads.size() || !downloads[index]) return;
    
    auto& item = downloads[index];
    
    ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
    std::string title = "details: " + item->getFileName();
    if (ImGui::Begin(title.c_str(), open)) {
        
        // File info
        ImGui::Text("file name:");
        ImGui::SameLine(120);
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%s", item->getFileName().c_str());
        
        ImGui::Text("url:");
        ImGui::SameLine(120);
        std::string url = item->getUrl();
        if (url.length() > 50) url = url.substr(0, 47) + "...";
        ImGui::TextColored(ImVec4(0.5f, 0.7f, 1.0f, 1.0f), "%s", url.c_str());
        
        ImGui::Text("save to:");
        ImGui::SameLine(120);
        ImGui::Text("%s", item->getFilePath().c_str());
        
        ImGui::Text("size:");
        ImGui::SameLine(120);
        ImGui::Text("%s", item->getSize().c_str());
        
        ImGui::Text("status:");
        ImGui::SameLine(120);
        ImGui::Text("%s", item->getStatus().c_str());
        
        ImGui::Text("category:");
        ImGui::SameLine(120);
        ImGui::Text("%s", item->getCategoryName().c_str());
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        // Progress
        ImGui::Text("progress:");
        float progress = (float)item->getProgress() / 100.0f;
        ImGui::ProgressBar(progress, ImVec2(-1, 20));
        
        ImGui::Text("speed:");
        ImGui::SameLine(120);
        ImGui::Text("%s", item->getSpeed().c_str());
        
        ImGui::Text("eta:");
        ImGui::SameLine(120);
        ImGui::Text("%s", item->getETA().c_str());
        
        ImGui::Text("segments:");
        ImGui::SameLine(120);
        ImGui::Text("%d / %d active", item->getActiveSegments(), item->getTotalSegments());
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        // Checksum
        ImGui::Text("checksum verification:");
        if (item->isChecksumVerified()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "verified");
        } else if (!item->getChecksumResult().empty()) {
            ImGui::Text("md5: %s", item->getChecksumResult().c_str());
        }
        
        if (item->isCompleted() && ImGui::Button("calculate md5", ImVec2(120, 28))) {
            item->verifyChecksum();
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        // Actions
        if (!item->isCompleted()) {
            if (item->isActive()) {
                if (ImGui::Button("pause", ImVec2(100, 32))) {
                    item->pause();
                }
            } else {
                if (ImGui::Button("resume", ImVec2(100, 32))) {
                    item->resume();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("cancel", ImVec2(100, 32))) {
                item->cancel();
            }
        } else {
            if (ImGui::Button("open file", ImVec2(100, 32))) {
#ifdef _WIN32
                ShellExecuteA(NULL, "open", item->getFilePath().c_str(), NULL, NULL, SW_SHOWNORMAL);
#endif
            }
            ImGui::SameLine();
            if (ImGui::Button("open folder", ImVec2(100, 32))) {
#ifdef _WIN32
                std::string dir = std::filesystem::path(item->getFilePath()).parent_path().string();
                ShellExecuteA(NULL, "explore", dir.c_str(), NULL, NULL, SW_SHOWNORMAL);
#endif
            }
        }
    }
    ImGui::End();
}

void DownloadManager::renderSchedulerPanel(bool* open) {
    if (!*open) return;
    
    Settings& settings = Settings::getInstance();
    
    ImGui::SetNextWindowSize(ImVec2(450, 350), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("scheduler", open)) {
        
        ImGui::Checkbox("enable download scheduler", &settings.schedulerEnabled);
        
        if (settings.schedulerEnabled) {
            ImGui::Spacing();
            ImGui::Text("download only during these hours:");
            ImGui::Spacing();
            
            ImGui::SliderInt("start hour", &settings.schedulerStartHour, 0, 23);
            ImGui::SliderInt("end hour", &settings.schedulerEndHour, 0, 23);
            
            ImGui::Spacing();
            
            // Visual schedule display
            ImGui::Text("schedule visualization:");
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            
            float width = ImGui::GetContentRegionAvail().x;
            float height = 30;
            
            // Background
            drawList->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), 
                ImGui::ColorConvertFloat4ToU32(ImVec4(0.2f, 0.2f, 0.2f, 1.0f)));
            
            // Active hours
            float hourWidth = width / 24.0f;
            int start = settings.schedulerStartHour;
            int end = settings.schedulerEndHour;
            
            if (start <= end) {
                drawList->AddRectFilled(
                    ImVec2(pos.x + start * hourWidth, pos.y),
                    ImVec2(pos.x + end * hourWidth, pos.y + height),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(0.3f, 0.6f, 1.0f, 1.0f)));
            } else {
                drawList->AddRectFilled(
                    ImVec2(pos.x, pos.y),
                    ImVec2(pos.x + end * hourWidth, pos.y + height),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(0.3f, 0.6f, 1.0f, 1.0f)));
                drawList->AddRectFilled(
                    ImVec2(pos.x + start * hourWidth, pos.y),
                    ImVec2(pos.x + width, pos.y + height),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(0.3f, 0.6f, 1.0f, 1.0f)));
            }
            
            // Hour markers
            for (int i = 0; i <= 24; i += 6) {
                float x = pos.x + i * hourWidth;
                drawList->AddLine(ImVec2(x, pos.y), ImVec2(x, pos.y + height), 
                    ImGui::ColorConvertFloat4ToU32(ImVec4(0.5f, 0.5f, 0.5f, 1.0f)));
            }
            
            ImGui::Dummy(ImVec2(width, height + 5));
            ImGui::Text("0h      6h      12h      18h      24h");
            
            ImGui::Spacing();
            
            bool canDl = canDownloadNow();
            if (canDl) {
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "status: downloads allowed now");
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "status: waiting for scheduled time");
            }
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        // Speed limiting during hours
        ImGui::Checkbox("limit speed during certain hours", &settings.limitSpeedDuringHours);
        
        if (settings.limitSpeedDuringHours) {
            ImGui::SliderInt("limit start", &settings.limitStartHour, 0, 23);
            ImGui::SliderInt("limit end", &settings.limitEndHour, 0, 23);
            ImGui::SliderInt("speed limit (kb/s)", &settings.limitSpeedKBps, 10, 10240);
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        if (ImGui::Button("save", ImVec2(100, 32))) {
            settings.save();
        }
    }
    ImGui::End();
}

void DownloadManager::renderTorrentPanel(bool* open) {
    if (!*open) return;
    
    ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("torrents", open)) {
        // Torrent stats
        const TorrentStats& stats = torrentManager.getStats();
        
        ImGui::Text("total: %d | downloading: %d | seeding: %d", 
            stats.totalTorrents, stats.activeTorrents, stats.seedingTorrents);
        
        char speedBuf[128];
        snprintf(speedBuf, sizeof(speedBuf), "D: %.2f kb/s | U: %.2f kb/s",
            stats.downloadSpeed / 1024.0, stats.uploadSpeed / 1024.0);
        ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(speedBuf).x - 20);
        ImGui::Text("%s", speedBuf);
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        // Toolbar
        static bool showAddTorrent = false;
        if (ImGui::Button("add torrent", ImVec2(120, 32))) {
            showAddTorrent = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("pause all", ImVec2(100, 32))) {
            torrentManager.pauseAll();
        }
        ImGui::SameLine();
        if (ImGui::Button("resume all", ImVec2(100, 32))) {
            torrentManager.resumeAll();
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        // Torrent list
        ImGui::BeginChild("torrent_list", ImVec2(-1, -1), false);
        torrentManager.render(Settings::getInstance().compactMode);
        ImGui::EndChild();
        
        // Add torrent dialog
        torrentManager.renderAddTorrentDialog(&showAddTorrent);
    }
    ImGui::End();
}

void DownloadManager::renderAddTorrentDialog(bool* open) {
    torrentManager.renderAddTorrentDialog(open);
}

void DownloadManager::renderContextMenu(size_t index) {
    if (index >= downloads.size() || !downloads[index]) return;
    
    auto& item = downloads[index];
    
    if (ImGui::BeginPopupContextItem()) {
        if (item->isActive()) {
            if (ImGui::MenuItem("pause")) item->pause();
        } else if (item->isPaused() || item->isQueued()) {
            if (ImGui::MenuItem("resume")) item->resume();
        }
        
        if (!item->isCompleted()) {
            if (ImGui::MenuItem("cancel")) item->cancel();
        }
        
        if (item->hasError()) {
            if (ImGui::MenuItem("retry")) item->retry();
        }
        
        ImGui::Separator();
        
        if (ImGui::MenuItem("move to top")) moveToTop(index);
        if (ImGui::MenuItem("move up")) moveUp(index);
        if (ImGui::MenuItem("move down")) moveDown(index);
        if (ImGui::MenuItem("move to bottom")) moveToBottom(index);
        
        ImGui::Separator();
        
        if (ImGui::BeginMenu("priority")) {
            if (ImGui::MenuItem("high", nullptr, item->getPriority() == 2)) setPriority(index, 2);
            if (ImGui::MenuItem("normal", nullptr, item->getPriority() == 0)) setPriority(index, 0);
            if (ImGui::MenuItem("low", nullptr, item->getPriority() == -1)) setPriority(index, -1);
            ImGui::EndMenu();
        }
        
        ImGui::Separator();
        
        if (ImGui::MenuItem("copy url")) {
#ifdef _WIN32
            if (OpenClipboard(NULL)) {
                EmptyClipboard();
                std::string url = item->getUrl();
                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, url.size() + 1);
                if (hMem) {
                    memcpy(GlobalLock(hMem), url.c_str(), url.size() + 1);
                    GlobalUnlock(hMem);
                    SetClipboardData(CF_TEXT, hMem);
                }
                CloseClipboard();
            }
#endif
        }
        
        if (item->isCompleted()) {
            if (ImGui::MenuItem("open file")) {
#ifdef _WIN32
                ShellExecuteA(NULL, "open", item->getFilePath().c_str(), NULL, NULL, SW_SHOWNORMAL);
#endif
            }
            if (ImGui::MenuItem("open folder")) {
#ifdef _WIN32
                std::string dir = std::filesystem::path(item->getFilePath()).parent_path().string();
                ShellExecuteA(NULL, "explore", dir.c_str(), NULL, NULL, SW_SHOWNORMAL);
#endif
            }
        }
        
        ImGui::Separator();
        
        if (ImGui::MenuItem("details...")) {
            detailsIndex = (int)index;
        }
        
        ImGui::Separator();
        
        if (ImGui::MenuItem("remove")) {
            removeDownload(index);
        }
        
        ImGui::EndPopup();
    }
}

// ============================================================================
// DRAG & DROP SUPPORT
// ============================================================================

void DownloadManager::handleDroppedFiles(int count, const char** paths) {
    for (int i = 0; i < count; i++) {
        std::string path = paths[i];
        
        // Check if it's a .torrent file
        if (path.size() > 8 && path.substr(path.size() - 8) == ".torrent") {
            // Handle torrent file
            torrentManager.addTorrent(path);
            continue;
        }
        
        // Check if it's a text file with URLs
        if (path.size() > 4 && (path.substr(path.size() - 4) == ".txt" || 
                                 path.substr(path.size() - 4) == ".url")) {
            std::ifstream file(path);
            if (file.is_open()) {
                std::string line;
                while (std::getline(file, line)) {
                    // Trim whitespace
                    line.erase(0, line.find_first_not_of(" \t\r\n"));
                    line.erase(line.find_last_not_of(" \t\r\n") + 1);
                    
                    if (!line.empty() && isValidUrl(line)) {
                        addDownload(line);
                    }
                }
                file.close();
            }
            continue;
        }
        
        // Check if the path itself is a URL (from browser drag)
        if (isValidUrl(path)) {
            addDownload(path);
        }
    }
}

void DownloadManager::handleDroppedUrls(const std::string& text) {
    // Parse text for URLs
    std::regex urlRegex(R"((https?|ftp)://[^\s<>"{}|\\^`\[\]]+)", std::regex::icase);
    std::sregex_iterator begin(text.begin(), text.end(), urlRegex);
    std::sregex_iterator end;
    
    for (auto it = begin; it != end; ++it) {
        std::string url = it->str();
        if (isValidUrl(url)) {
            addDownload(url);
        }
    }
}

// ============================================================================
// STATISTICS DASHBOARD
// ============================================================================

void DownloadManager::updateHourlyStats() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::hours>(now - stats.lastHourUpdate);
    
    if (elapsed.count() >= 1 || stats.lastHourUpdate == std::chrono::steady_clock::time_point{}) {
        // Save current hour's data
        if (stats.lastHourUpdate != std::chrono::steady_clock::time_point{}) {
            stats.hourlyBytesHistory.push_back(stats.currentHourBytes);
            if (stats.currentHourSpeedSamples > 0) {
                stats.hourlySpeedHistory.push_back(stats.currentHourTotalSpeed / stats.currentHourSpeedSamples);
            } else {
                stats.hourlySpeedHistory.push_back(0);
            }
            
            // Keep only last 24 hours
            while (stats.hourlyBytesHistory.size() > 24) {
                stats.hourlyBytesHistory.pop_front();
            }
            while (stats.hourlySpeedHistory.size() > 24) {
                stats.hourlySpeedHistory.pop_front();
            }
        }
        
        // Reset for new hour
        stats.currentHourBytes = 0;
        stats.currentHourPeakSpeed = 0;
        stats.currentHourTotalSpeed = 0;
        stats.currentHourSpeedSamples = 0;
        stats.lastHourUpdate = now;
    }
    
    // Update current hour
    stats.currentHourBytes += static_cast<uint64_t>(stats.currentTotalSpeed);
    stats.currentHourTotalSpeed += static_cast<float>(stats.currentTotalSpeed);
    stats.currentHourSpeedSamples++;
    if (stats.currentTotalSpeed > stats.currentHourPeakSpeed) {
        stats.currentHourPeakSpeed = static_cast<float>(stats.currentTotalSpeed);
    }
}

void DownloadManager::recordDownloadCompletion(const DownloadItem& item) {
    // Update category stats
    FileCategory cat = item.getCategory();
    stats.categoryCount[cat]++;
    stats.categoryBytes[cat] += item.getBytesTotal();
    
    // Update daily stats
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    char dateBuffer[11];
    std::strftime(dateBuffer, sizeof(dateBuffer), "%Y-%m-%d", std::localtime(&time_t_now));
    std::string today(dateBuffer);
    
    // Find or create today's entry
    bool found = false;
    for (auto& daily : stats.dailyHistory) {
        if (daily.date == today) {
            daily.bytesDownloaded += item.getBytesTotal();
            daily.downloadsCompleted++;
            found = true;
            break;
        }
    }
    
    if (!found) {
        DailyStats newDay;
        newDay.date = today;
        newDay.bytesDownloaded = item.getBytesTotal();
        newDay.downloadsCompleted = 1;
        stats.dailyHistory.push_back(newDay);
        
        // Keep only last 30 days
        while (stats.dailyHistory.size() > 30) {
            stats.dailyHistory.erase(stats.dailyHistory.begin());
        }
    }
    
    saveDashboardStats();
}

void DownloadManager::loadDashboardStats() {
    std::string path = Settings::getInstance().downloadPath + "/bolt_dashboard_stats.dat";
    std::ifstream file(path);
    if (!file.is_open()) return;
    
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string type;
        iss >> type;
        
        if (type == "DAILY") {
            DailyStats day;
            iss >> day.date >> day.bytesDownloaded >> day.downloadsCompleted >> day.peakSpeed >> day.averageSpeed;
            stats.dailyHistory.push_back(day);
        } else if (type == "CATEGORY_COUNT") {
            int cat;
            int count;
            iss >> cat >> count;
            stats.categoryCount[static_cast<FileCategory>(cat)] = count;
        } else if (type == "CATEGORY_BYTES") {
            int cat;
            uint64_t bytes;
            iss >> cat >> bytes;
            stats.categoryBytes[static_cast<FileCategory>(cat)] = bytes;
        } else if (type == "ALLTIME") {
            iss >> stats.allTimeBytesDownloaded >> stats.allTimeDownloadsCompleted;
        }
    }
    
    file.close();
}

void DownloadManager::saveDashboardStats() {
    std::string path = Settings::getInstance().downloadPath + "/bolt_dashboard_stats.dat";
    std::ofstream file(path);
    if (!file.is_open()) return;
    
    file << "ALLTIME " << stats.allTimeBytesDownloaded << " " << stats.allTimeDownloadsCompleted << "\n";
    
    for (const auto& day : stats.dailyHistory) {
        file << "DAILY " << day.date << " " << day.bytesDownloaded << " " 
             << day.downloadsCompleted << " " << day.peakSpeed << " " << day.averageSpeed << "\n";
    }
    
    for (const auto& [cat, count] : stats.categoryCount) {
        file << "CATEGORY_COUNT " << static_cast<int>(cat) << " " << count << "\n";
    }
    
    for (const auto& [cat, bytes] : stats.categoryBytes) {
        file << "CATEGORY_BYTES " << static_cast<int>(cat) << " " << bytes << "\n";
    }
    
    file.close();
}

void DownloadManager::renderStatisticsDashboard(bool* open) {
    if (!open || !*open) return;
    
    ImGui::SetNextWindowSize(ImVec2(900, 700), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("statistics dashboard", open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    
    // Tab bar for different views
    if (ImGui::BeginTabBar("StatsTabs")) {
        // Overview Tab
        if (ImGui::BeginTabItem("overview")) {
            ImGui::Spacing();
            
            // Summary cards
            float cardWidth = (ImGui::GetContentRegionAvail().x - 30) / 4;
            
            // Total Downloads Card
            ImGui::BeginChild("card1", ImVec2(cardWidth, 100), true);
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "total downloads");
            ImGui::SetWindowFontScale(2.0f);
            ImGui::Text("%d", stats.allTimeDownloadsCompleted);
            ImGui::SetWindowFontScale(1.0f);
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "all time");
            ImGui::EndChild();
            
            ImGui::SameLine();
            
            // Total Data Card
            ImGui::BeginChild("card2", ImVec2(cardWidth, 100), true);
            ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "data downloaded");
            ImGui::SetWindowFontScale(1.5f);
            auto formatSize = [](uint64_t bytes) -> std::string {
                if (bytes >= 1099511627776ULL) return std::to_string(bytes / 1099511627776ULL) + " TB";
                if (bytes >= 1073741824ULL) return std::to_string(bytes / 1073741824ULL) + " GB";
                if (bytes >= 1048576ULL) return std::to_string(bytes / 1048576ULL) + " MB";
                return std::to_string(bytes / 1024ULL) + " KB";
            };
            ImGui::Text("%s", formatSize(stats.allTimeBytesDownloaded).c_str());
            ImGui::SetWindowFontScale(1.0f);
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "all time");
            ImGui::EndChild();
            
            ImGui::SameLine();
            
            // Session Downloads Card
            ImGui::BeginChild("card3", ImVec2(cardWidth, 100), true);
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "session downloads");
            ImGui::SetWindowFontScale(2.0f);
            ImGui::Text("%d", stats.sessionDownloadsCompleted);
            ImGui::SetWindowFontScale(1.0f);
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "this session");
            ImGui::EndChild();
            
            ImGui::SameLine();
            
            // Peak Speed Card
            ImGui::BeginChild("card4", ImVec2(cardWidth, 100), true);
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "peak speed");
            ImGui::SetWindowFontScale(1.5f);
            if (stats.peakSpeed >= 1048576) {
                ImGui::Text("%.1f MB/s", stats.peakSpeed / 1048576.0);
            } else {
                ImGui::Text("%.1f KB/s", stats.peakSpeed / 1024.0);
            }
            ImGui::SetWindowFontScale(1.0f);
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "all time");
            ImGui::EndChild();
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            
            // Speed Graph (last 24 hours)
            ImGui::Text("bandwidth usage (last 24 hours)");
            ImGui::Spacing();
            
            if (!stats.hourlySpeedHistory.empty()) {
                std::vector<float> speedData(stats.hourlySpeedHistory.begin(), stats.hourlySpeedHistory.end());
                float maxSpeed = *std::max_element(speedData.begin(), speedData.end());
                if (maxSpeed < 1) maxSpeed = 1;
                
                ImGui::PlotLines("##hourlyspeed", speedData.data(), (int)speedData.size(), 0,
                    nullptr, 0, maxSpeed * 1.1f, ImVec2(ImGui::GetContentRegionAvail().x, 150));
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "no hourly data yet - keep downloading!");
            }
            
            ImGui::EndTabItem();
        }
        
        // Daily History Tab
        if (ImGui::BeginTabItem("daily history")) {
            ImGui::Spacing();
            
            if (stats.dailyHistory.empty()) {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "no daily history yet");
            } else {
                // Bar chart of daily downloads
                ImGui::Text("downloads per day (last 30 days)");
                ImGui::Spacing();
                
                std::vector<float> dailyData;
                std::vector<std::string> labels;
                uint64_t maxBytes = 0;
                
                for (const auto& day : stats.dailyHistory) {
                    dailyData.push_back(static_cast<float>(day.bytesDownloaded));
                    labels.push_back(day.date.substr(5)); // MM-DD
                    if (day.bytesDownloaded > maxBytes) maxBytes = day.bytesDownloaded;
                }
                
                // Draw custom bar chart
                ImVec2 canvasPos = ImGui::GetCursorScreenPos();
                ImVec2 canvasSize(ImGui::GetContentRegionAvail().x, 200);
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                
                drawList->AddRectFilled(canvasPos, 
                    ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                    IM_COL32(30, 30, 35, 255));
                
                if (!dailyData.empty() && maxBytes > 0) {
                    float barWidth = canvasSize.x / dailyData.size() - 4;
                    for (size_t i = 0; i < dailyData.size(); i++) {
                        float barHeight = (dailyData[i] / maxBytes) * (canvasSize.y - 20);
                        float x = canvasPos.x + i * (barWidth + 4) + 2;
                        float y = canvasPos.y + canvasSize.y - barHeight - 10;
                        
                        // Gradient color based on height
                        ImU32 color = IM_COL32(
                            (int)(100 + 155 * (dailyData[i] / maxBytes)),
                            (int)(150 + 50 * (dailyData[i] / maxBytes)),
                            255, 255);
                        
                        drawList->AddRectFilled(
                            ImVec2(x, y),
                            ImVec2(x + barWidth, canvasPos.y + canvasSize.y - 10),
                            color, 3.0f);
                    }
                }
                
                ImGui::Dummy(canvasSize);
                
                // Table with details
                ImGui::Spacing();
                if (ImGui::BeginTable("dailyTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("date", ImGuiTableColumnFlags_WidthFixed, 120);
                    ImGui::TableSetupColumn("downloads", ImGuiTableColumnFlags_WidthFixed, 100);
                    ImGui::TableSetupColumn("data", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("avg speed", ImGuiTableColumnFlags_WidthFixed, 120);
                    ImGui::TableHeadersRow();
                    
                    // Show in reverse order (most recent first)
                    for (int i = (int)stats.dailyHistory.size() - 1; i >= 0; i--) {
                        const auto& day = stats.dailyHistory[i];
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", day.date.c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text("%d", day.downloadsCompleted);
                        ImGui::TableNextColumn();
                        auto formatSize = [](uint64_t bytes) -> std::string {
                            if (bytes >= 1073741824ULL) return std::to_string(bytes / 1073741824ULL) + " GB";
                            if (bytes >= 1048576ULL) return std::to_string(bytes / 1048576ULL) + " MB";
                            return std::to_string(bytes / 1024ULL) + " KB";
                        };
                        ImGui::Text("%s", formatSize(day.bytesDownloaded).c_str());
                        ImGui::TableNextColumn();
                        if (day.averageSpeed >= 1048576) {
                            ImGui::Text("%.1f MB/s", day.averageSpeed / 1048576.0);
                        } else {
                            ImGui::Text("%.1f KB/s", day.averageSpeed / 1024.0);
                        }
                    }
                    
                    ImGui::EndTable();
                }
            }
            
            ImGui::EndTabItem();
        }
        
        // Categories Tab
        if (ImGui::BeginTabItem("categories")) {
            ImGui::Spacing();
            
            auto getCategoryName = [](FileCategory cat) -> const char* {
                switch (cat) {
                    case FileCategory::Video: return "videos";
                    case FileCategory::Audio: return "audio";
                    case FileCategory::Document: return "documents";
                    case FileCategory::Archive: return "archives";
                    case FileCategory::Program: return "programs";
                    case FileCategory::Image: return "images";
                    default: return "other";
                }
            };
            
            auto getCategoryColor = [](FileCategory cat) -> ImVec4 {
                switch (cat) {
                    case FileCategory::Video: return ImVec4(0.9f, 0.4f, 0.4f, 1.0f);
                    case FileCategory::Audio: return ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
                    case FileCategory::Document: return ImVec4(0.4f, 0.6f, 0.9f, 1.0f);
                    case FileCategory::Archive: return ImVec4(0.9f, 0.7f, 0.3f, 1.0f);
                    case FileCategory::Program: return ImVec4(0.7f, 0.4f, 0.9f, 1.0f);
                    case FileCategory::Image: return ImVec4(0.3f, 0.9f, 0.9f, 1.0f);
                    default: return ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
                }
            };
            
            // Pie chart visualization (simple version using colored rectangles)
            uint64_t totalBytes = 0;
            int totalCount = 0;
            for (const auto& [cat, bytes] : stats.categoryBytes) {
                totalBytes += bytes;
            }
            for (const auto& [cat, count] : stats.categoryCount) {
                totalCount += count;
            }
            
            ImGui::Text("downloads by category");
            ImGui::Spacing();
            
            // Visual bar representation
            float barHeight = 30;
            ImVec2 barStart = ImGui::GetCursorScreenPos();
            float barWidth = ImGui::GetContentRegionAvail().x;
            
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            float currentX = barStart.x;
            
            if (totalBytes > 0) {
                for (int i = 0; i <= (int)FileCategory::Other; i++) {
                    FileCategory cat = static_cast<FileCategory>(i);
                    auto it = stats.categoryBytes.find(cat);
                    if (it != stats.categoryBytes.end() && it->second > 0) {
                        float segmentWidth = (float)it->second / totalBytes * barWidth;
                        ImVec4 col = getCategoryColor(cat);
                        drawList->AddRectFilled(
                            ImVec2(currentX, barStart.y),
                            ImVec2(currentX + segmentWidth, barStart.y + barHeight),
                            ImGui::ColorConvertFloat4ToU32(col));
                        currentX += segmentWidth;
                    }
                }
            } else {
                drawList->AddRectFilled(barStart, 
                    ImVec2(barStart.x + barWidth, barStart.y + barHeight),
                    IM_COL32(50, 50, 55, 255));
            }
            
            ImGui::Dummy(ImVec2(barWidth, barHeight + 10));
            ImGui::Spacing();
            
            // Legend and details
            if (ImGui::BeginTable("categoryTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("category", ImGuiTableColumnFlags_WidthFixed, 150);
                ImGui::TableSetupColumn("count", ImGuiTableColumnFlags_WidthFixed, 100);
                ImGui::TableSetupColumn("data", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("percentage", ImGuiTableColumnFlags_WidthFixed, 100);
                ImGui::TableHeadersRow();
                
                for (int i = 0; i <= (int)FileCategory::Other; i++) {
                    FileCategory cat = static_cast<FileCategory>(i);
                    int count = 0;
                    uint64_t bytes = 0;
                    
                    auto countIt = stats.categoryCount.find(cat);
                    if (countIt != stats.categoryCount.end()) count = countIt->second;
                    
                    auto bytesIt = stats.categoryBytes.find(cat);
                    if (bytesIt != stats.categoryBytes.end()) bytes = bytesIt->second;
                    
                    if (count == 0 && bytes == 0) continue;
                    
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    
                    // Color indicator
                    ImVec4 col = getCategoryColor(cat);
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    drawList->AddRectFilled(p, ImVec2(p.x + 12, p.y + 12), 
                        ImGui::ColorConvertFloat4ToU32(col), 2.0f);
                    ImGui::Dummy(ImVec2(16, 0));
                    ImGui::SameLine();
                    ImGui::Text("%s", getCategoryName(cat));
                    
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", count);
                    
                    ImGui::TableNextColumn();
                    auto formatSize = [](uint64_t bytes) -> std::string {
                        if (bytes >= 1073741824ULL) return std::to_string(bytes / 1073741824ULL) + " GB";
                        if (bytes >= 1048576ULL) return std::to_string(bytes / 1048576ULL) + " MB";
                        return std::to_string(bytes / 1024ULL) + " KB";
                    };
                    ImGui::Text("%s", formatSize(bytes).c_str());
                    
                    ImGui::TableNextColumn();
                    if (totalBytes > 0) {
                        ImGui::Text("%.1f%%", (float)bytes / totalBytes * 100);
                    } else {
                        ImGui::Text("0%%");
                    }
                }
                
                ImGui::EndTable();
            }
            
            ImGui::EndTabItem();
        }
        
        // Live Monitor Tab
        if (ImGui::BeginTabItem("live monitor")) {
            ImGui::Spacing();
            
            // Current speed
            ImGui::Text("current speed:");
            ImGui::SameLine();
            if (stats.currentTotalSpeed >= 1048576) {
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%.2f MB/s", 
                    stats.currentTotalSpeed / 1048576.0);
            } else {
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%.2f KB/s", 
                    stats.currentTotalSpeed / 1024.0);
            }
            
            ImGui::SameLine(300);
            ImGui::Text("active downloads: %d", activeDownloads);
            
            ImGui::Spacing();
            
            // Real-time speed graph
            ImGui::Text("real-time bandwidth");
            if (!stats.speedHistory.empty()) {
                std::vector<float> speedData(stats.speedHistory.begin(), stats.speedHistory.end());
                float maxSpeed = *std::max_element(speedData.begin(), speedData.end());
                if (maxSpeed < 1024) maxSpeed = 1024;
                
                ImGui::PlotLines("##realtimespeed", speedData.data(), (int)speedData.size(), 0,
                    nullptr, 0, maxSpeed * 1.1f, ImVec2(ImGui::GetContentRegionAvail().x, 200));
            }
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            
            // Active downloads list
            ImGui::Text("active downloads:");
            ImGui::Spacing();
            
            for (size_t i = 0; i < downloads.size(); i++) {
                auto& dl = downloads[i];
                if (dl->isActive()) {
                    ImGui::PushID((int)i);
                    
                    // Progress bar with speed
                    float progress = static_cast<float>(dl->getProgress() / 100.0);
                    char overlay[128];
                    snprintf(overlay, sizeof(overlay), "%s - %.1f%% (%.1f KB/s)", 
                        dl->getFileName().c_str(), dl->getProgress(), dl->getCurrentSpeed() / 1024.0);
                    
                    ImGui::ProgressBar(progress, ImVec2(-1, 25), overlay);
                    
                    ImGui::PopID();
                }
            }
            
            ImGui::EndTabItem();
        }
        
        ImGui::EndTabBar();
    }
    
    ImGui::End();
}
