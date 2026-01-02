#include "downloaditem.h"
#include "settings.h"
#include "imgui.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <filesystem>
#include <curl/curl.h>
#include <algorithm>
#include <cmath>

static size_t WriteFileCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    std::ofstream* file = (std::ofstream*)userp;
    size_t realsize = size * nmemb;
    file->write((char*)contents, realsize);
    return realsize;
}

DownloadItem::DownloadItem(const std::string& url, const std::string& filePath)
    : url(url)
    , filePath(filePath)
    , state(DownloadState::Queued)
    , shouldStop(false)
    , shouldPause(false)
    , bytesReceived(0)
    , bytesTotal(0)
    , resumeOffset(0)
    , progress(0.0)
    , currentSpeed(0.0)
    , lastBytesReceived(0)
{
    lastUpdateTime = std::chrono::steady_clock::now();
    lastHistoryUpdate = std::chrono::steady_clock::now();
    startTime = std::chrono::system_clock::now();
}

DownloadItem::~DownloadItem() {
    cancel();
    if (downloadThreadHandle.joinable()) {
        downloadThreadHandle.join();
    }
    for (auto& segment : segments) {
        if (segment && segment->workerThread.joinable()) {
            segment->workerThread.join();
        }
    }
}

std::string DownloadItem::getFileNameFromPath(const std::string& path) const {
    std::filesystem::path p(path);
    return p.filename().string();
}

std::string DownloadItem::getFileName() const {
    return getFileNameFromPath(filePath);
}

FileCategory DownloadItem::determineCategory() const {
    std::string ext = std::filesystem::path(filePath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    Settings& settings = Settings::getInstance();
    
    for (const auto& e : settings.videoExtensions) {
        if (ext == e) return FileCategory::Video;
    }
    for (const auto& e : settings.audioExtensions) {
        if (ext == e) return FileCategory::Audio;
    }
    for (const auto& e : settings.documentExtensions) {
        if (ext == e) return FileCategory::Document;
    }
    for (const auto& e : settings.archiveExtensions) {
        if (ext == e) return FileCategory::Archive;
    }
    for (const auto& e : settings.programExtensions) {
        if (ext == e) return FileCategory::Program;
    }
    for (const auto& e : settings.imageExtensions) {
        if (ext == e) return FileCategory::Image;
    }
    
    return FileCategory::Other;
}

FileCategory DownloadItem::getCategory() const {
    return determineCategory();
}

std::string DownloadItem::getCategoryName() const {
    switch (getCategory()) {
        case FileCategory::Video: return "video";
        case FileCategory::Audio: return "audio";
        case FileCategory::Document: return "document";
        case FileCategory::Archive: return "archive";
        case FileCategory::Program: return "program";
        case FileCategory::Image: return "image";
        default: return "other";
    }
}

std::string DownloadItem::getStatus() const {
    switch (state.load()) {
        case DownloadState::Queued:
            return "queued";
        case DownloadState::Downloading:
            return "downloading";
        case DownloadState::Paused:
            return "paused";
        case DownloadState::Completed:
            return "completed";
        case DownloadState::Cancelled:
            return "cancelled";
        case DownloadState::Error:
            return "error: " + errorMessage;
        case DownloadState::Waiting:
            return "waiting";
        default:
            return "unknown";
    }
}

std::string DownloadItem::getSpeed() const {
    if (state == DownloadState::Downloading && currentSpeed > 0) {
        return formatSpeed(currentSpeed);
    }
    return "";
}

std::string DownloadItem::getSize() const {
    if (bytesTotal > 0) {
        return formatBytes(bytesReceived) + " / " + formatBytes(bytesTotal);
    }
    return formatBytes(bytesReceived);
}

std::string DownloadItem::getETA() const {
    if (state != DownloadState::Downloading || currentSpeed <= 0 || bytesTotal == 0) {
        return "";
    }
    
    uint64_t remaining = bytesTotal - bytesReceived;
    int seconds = (int)(remaining / currentSpeed);
    return formatTime(seconds);
}

int DownloadItem::getActiveSegments() const {
    int count = 0;
    for (const auto& seg : segments) {
        if (seg && seg->active) count++;
    }
    return count;
}

void DownloadItem::updateSpeedHistory() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastHistoryUpdate).count();
    
    if (elapsed >= 500) {  // Update every 500ms
        speedHistory.push_back((float)currentSpeed);
        if (speedHistory.size() > 120) {  // Keep last 60 seconds (at 500ms intervals)
            speedHistory.pop_front();
        }
        lastHistoryUpdate = now;
    }
}

void DownloadItem::start() {
    if (state != DownloadState::Queued && state != DownloadState::Paused) {
        return;
    }
    
    shouldStop = false;
    shouldPause = false;
    state = DownloadState::Downloading;
    startTime = std::chrono::system_clock::now();
    
    if (downloadThreadHandle.joinable()) {
        downloadThreadHandle.join();
    }
    
    downloadThreadHandle = std::thread(&DownloadItem::downloadThread, this);
}

void DownloadItem::pause() {
    if (state == DownloadState::Downloading) {
        shouldPause = true;
    }
}

void DownloadItem::resume() {
    if (state == DownloadState::Paused) {
        shouldPause = false;
        start();
    }
}

void DownloadItem::retry() {
    if (state == DownloadState::Error || state == DownloadState::Cancelled) {
        state = DownloadState::Queued;
        bytesReceived = 0;
        progress = 0.0;
        errorMessage.clear();
    }
}

void DownloadItem::cancel() {
    shouldStop = true;
    shouldPause = false;
    state = DownloadState::Cancelled;
    
    if (file && file->is_open()) {
        file->close();
    }
    
    // Delete partial file
    if (std::filesystem::exists(filePath)) {
        std::filesystem::remove(filePath);
    }
}

void DownloadItem::downloadThread() {
    CURL* curl = curl_easy_init();
    if (!curl) {
        state = DownloadState::Error;
        errorMessage = "failed to initialize curl";
        return;
    }
    
    // Check if file exists and get current size for resume
    uint64_t resumeFrom = 0;
    if (std::filesystem::exists(filePath)) {
        resumeFrom = std::filesystem::file_size(filePath);
        bytesReceived = resumeFrom;
        lastBytesReceived = resumeFrom;
    }
    
    file = std::make_unique<std::fstream>(filePath, std::ios::out | std::ios::binary | std::ios::app);
    if (!file->is_open()) {
        state = DownloadState::Error;
        errorMessage = "cannot open file for writing";
        curl_easy_cleanup(curl);
        return;
    }
    
    if (resumeFrom > 0) {
        file->seekp(resumeFrom);
    }
    
    resumeOffset = resumeFrom;
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFileCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, file.get());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "bolt/1.0");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    
    // Proxy settings
    Settings& settings = Settings::getInstance();
    if (settings.useProxy && !settings.proxyAddress.empty()) {
        std::string proxyUrl = settings.proxyAddress + ":" + std::to_string(settings.proxyPort);
        curl_easy_setopt(curl, CURLOPT_PROXY, proxyUrl.c_str());
        
        if (settings.proxyAuth && !settings.proxyUsername.empty()) {
            std::string proxyAuth = settings.proxyUsername + ":" + settings.proxyPassword;
            curl_easy_setopt(curl, CURLOPT_PROXYUSERPWD, proxyAuth.c_str());
        }
    }
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
    
    // Speed limit
    if (speedLimitKBps > 0) {
        curl_easy_setopt(curl, CURLOPT_MAX_RECV_SPEED_LARGE, (curl_off_t)(speedLimitKBps * 1024));
    }
    
    if (resumeFrom > 0) {
        curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)resumeFrom);
    }
    
    // Progress callback
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, [](void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) -> int {
        DownloadItem* item = (DownloadItem*)clientp;
        if (item->shouldStop) {
            return 1;
        }
        if (item->shouldPause) {
            return 1;
        }
        
        std::lock_guard<std::mutex> lock(item->fileMutex);
        item->bytesReceived = dlnow + item->resumeOffset;
        item->bytesTotal = dltotal > 0 ? dltotal + item->resumeOffset : 0;
        
        if (item->bytesTotal > 0) {
            item->progress = (double(item->bytesReceived) / double(item->bytesTotal)) * 100.0;
        }
        
        // Calculate speed
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - item->lastUpdateTime).count();
        if (elapsed > 100) {
            uint64_t bytesDiff = item->bytesReceived - item->lastBytesReceived;
            item->currentSpeed = (double(bytesDiff) * 1000.0) / double(elapsed);
            item->lastUpdateTime = now;
            item->lastBytesReceived = item->bytesReceived;
            item->updateSpeedHistory();
        }
        
        return 0;
    });
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, this);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    
    CURLcode res = curl_easy_perform(curl);
    
    endTime = std::chrono::system_clock::now();
    
    if (shouldPause) {
        state = DownloadState::Paused;
        lastBytesReceived = bytesReceived;
    } else if (shouldStop) {
        state = DownloadState::Cancelled;
    } else if (res == CURLE_OK || res == CURLE_ABORTED_BY_CALLBACK) {
        if (shouldPause) {
            state = DownloadState::Paused;
            lastBytesReceived = bytesReceived;
        } else if (!shouldStop) {
            state = DownloadState::Completed;
            progress = 100.0;
        }
    } else {
        state = DownloadState::Error;
        errorMessage = curl_easy_strerror(res);
    }
    
    file->close();
    curl_easy_cleanup(curl);
}

void DownloadItem::render(size_t index, bool compactMode) {
    if (compactMode) {
        renderCompact(index);
    } else {
        renderDetailed(index);
    }
}

// Helper for auto-sized buttons
static bool AutoBtn(const char* label, float height = 28) {
    float textWidth = ImGui::CalcTextSize(label).x;
    float buttonWidth = textWidth + 20;  // 10px padding on each side
    return ImGui::Button(label, ImVec2(buttonWidth, height));
}

void DownloadItem::renderCompact(size_t index) {
    ImGui::PushID((int)index);
    
    float windowWidth = ImGui::GetContentRegionAvail().x;
    
    // Compact card
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.10f, 0.10f, 1.0f));
    ImGui::BeginChild("compact_card", ImVec2(-1, 50), true, ImGuiWindowFlags_NoScrollbar);
    
    float innerWidth = ImGui::GetContentRegionAvail().x;
    
    // Layout: [Icon] [FileName...] [Progress] [%] [Speed/Status] [Button]
    // Use table for better alignment
    if (ImGui::BeginTable("compact_layout", 6, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("icon", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("progress", ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableSetupColumn("pct", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("status", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthFixed, 70);
        
        ImGui::TableNextRow();
        
        // Icon
        ImGui::TableNextColumn();
        const char* icon = "";
        ImVec4 iconColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
        switch (getCategory()) {
            case FileCategory::Video: icon = "VIDEO"; iconColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f); break;
            case FileCategory::Audio: icon = "AUDIO"; iconColor = ImVec4(0.4f, 1.0f, 0.4f, 1.0f); break;
            case FileCategory::Document: icon = "DOC"; iconColor = ImVec4(0.4f, 0.6f, 1.0f, 1.0f); break;
            case FileCategory::Archive: icon = "ZIP"; iconColor = ImVec4(1.0f, 0.8f, 0.3f, 1.0f); break;
            case FileCategory::Program: icon = "APP"; iconColor = ImVec4(0.8f, 0.4f, 1.0f, 1.0f); break;
            case FileCategory::Image: icon = "IMG"; iconColor = ImVec4(0.4f, 1.0f, 1.0f, 1.0f); break;
            default: icon = "FILE"; break;
        }
        ImGui::TextColored(iconColor, "%s", icon);
        
        // File name
        ImGui::TableNextColumn();
        std::string fileName = getFileName();
        if (fileName.length() > 35) {
            fileName = fileName.substr(0, 32) + "...";
        }
        ImGui::Text("%s", fileName.c_str());
        
        // Progress bar
        ImGui::TableNextColumn();
        float progressValue = (float)progress / 100.0f;
        ImGui::ProgressBar(progressValue, ImVec2(-1, 18), "");
        
        // Percentage
        ImGui::TableNextColumn();
        ImGui::Text("%.0f%%", progress.load());
        
        // Speed/Status
        ImGui::TableNextColumn();
        if (state == DownloadState::Downloading) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s", getSpeed().c_str());
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", getStatus().c_str());
        }
        
        // Action button
        ImGui::TableNextColumn();
        if (state == DownloadState::Paused || state == DownloadState::Queued) {
            if (AutoBtn("start", 24)) { start(); }
        } else if (state == DownloadState::Downloading) {
            if (AutoBtn("pause", 24)) { pause(); }
        } else if (state == DownloadState::Error) {
            if (AutoBtn("retry", 24)) { retry(); }
        }
        
        ImGui::EndTable();
    }
    
    ImGui::EndChild();
    ImGui::PopStyleColor();
    
    ImGui::Spacing();
    ImGui::PopID();
}

void DownloadItem::renderDetailed(size_t index) {
    ImGui::PushID((int)index);
    
    (void)index; // suppress unused warning
    
    // Card-style container - MUCH LARGER
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.10f, 0.10f, 1.0f));
    ImGui::BeginChild("download_card", ImVec2(-1, 180), true, ImGuiWindowFlags_NoScrollbar);
    
    // Header row: Icon + Name
    const char* icon = "";
    ImVec4 iconColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    switch (getCategory()) {
        case FileCategory::Video: icon = "[VIDEO]"; iconColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f); break;
        case FileCategory::Audio: icon = "[AUDIO]"; iconColor = ImVec4(0.4f, 1.0f, 0.4f, 1.0f); break;
        case FileCategory::Document: icon = "[DOC]"; iconColor = ImVec4(0.4f, 0.6f, 1.0f, 1.0f); break;
        case FileCategory::Archive: icon = "[ZIP]"; iconColor = ImVec4(1.0f, 0.8f, 0.3f, 1.0f); break;
        case FileCategory::Program: icon = "[APP]"; iconColor = ImVec4(0.8f, 0.4f, 1.0f, 1.0f); break;
        case FileCategory::Image: icon = "[IMG]"; iconColor = ImVec4(0.4f, 1.0f, 1.0f, 1.0f); break;
        default: icon = "[FILE]"; break;
    }
    
    ImGui::TextColored(iconColor, "%s", icon);
    ImGui::SameLine();
    
    std::string fileName = getFileName();
    ImGui::Text("%s", fileName.c_str());
    
    // Progress percentage on the right
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80);
    if (state == DownloadState::Completed) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "100%%");
    } else {
        ImGui::Text("%.1f%%", progress.load());
    }
    
    ImGui::Spacing();
    ImGui::Spacing();
    
    // Progress bar - LARGER
    float progressValue = (float)progress / 100.0f;
    
    // Custom progress bar color based on state
    ImVec4 progressColor = ImVec4(0.45f, 0.72f, 1.00f, 1.00f);  // Blue
    if (state == DownloadState::Completed) {
        progressColor = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);  // Green
    } else if (state == DownloadState::Error) {
        progressColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);  // Red
    } else if (state == DownloadState::Paused) {
        progressColor = ImVec4(1.0f, 0.8f, 0.3f, 1.0f);  // Yellow
    }
    
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, progressColor);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
    ImGui::ProgressBar(progressValue, ImVec2(-1, 16), "");
    ImGui::PopStyleColor(2);
    
    ImGui::Spacing();
    ImGui::Spacing();
    
    // Info row - SIZE | SPEED | ETA | STATUS
    ImGui::Text("%s", getSize().c_str());
    
    if (state == DownloadState::Downloading) {
        std::string speed = getSpeed();
        std::string eta = getETA();
        if (!speed.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), " | ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s", speed.c_str());
        }
        if (!eta.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), " | ");
            ImGui::SameLine();
            ImGui::Text("eta: %s", eta.c_str());
        }
    } else {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), " | ");
        ImGui::SameLine();
        
        // Color-coded status
        ImVec4 statusColor = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
        if (state == DownloadState::Completed) statusColor = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
        else if (state == DownloadState::Error) statusColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
        else if (state == DownloadState::Paused) statusColor = ImVec4(1.0f, 0.8f, 0.3f, 1.0f);
        
        ImGui::TextColored(statusColor, "%s", getStatus().c_str());
    }
    
    ImGui::Spacing();
    ImGui::Spacing();
    
    // Buttons - auto-sized
    if (state == DownloadState::Paused || state == DownloadState::Queued) {
        if (AutoBtn("start", 30)) { start(); }
    } else if (state == DownloadState::Downloading) {
        if (AutoBtn("pause", 30)) { pause(); }
    } else if (state == DownloadState::Error) {
        if (AutoBtn("retry", 30)) { retry(); }
    } else {
        ImGui::BeginDisabled();
        AutoBtn("done", 30);
        ImGui::EndDisabled();
    }
    
    ImGui::SameLine();
    
    if (state != DownloadState::Completed && state != DownloadState::Cancelled) {
        if (AutoBtn("cancel", 30)) { cancel(); }
    } else {
        if (AutoBtn("remove", 30)) { cancel(); }
    }
    
    ImGui::SameLine();
    
    // Open folder button
    if (AutoBtn("open folder", 30)) {
        std::string dir = std::filesystem::path(filePath).parent_path().string();
#ifdef _WIN32
        ShellExecuteA(NULL, "explore", dir.c_str(), NULL, NULL, SW_SHOWNORMAL);
#else
        std::string cmd = "xdg-open \"" + dir + "\"";
        system(cmd.c_str());
#endif
    }
    
    ImGui::EndChild();
    ImGui::PopStyleColor();
    
    ImGui::Spacing();
    ImGui::PopID();
}

std::string DownloadItem::formatBytes(uint64_t bytes) const {
    const uint64_t KB = 1024;
    const uint64_t MB = KB * 1024;
    const uint64_t GB = MB * 1024;
    
    char buffer[64];
    if (bytes >= GB) {
        snprintf(buffer, sizeof(buffer), "%.2f gb", double(bytes) / GB);
    } else if (bytes >= MB) {
        snprintf(buffer, sizeof(buffer), "%.2f mb", double(bytes) / MB);
    } else if (bytes >= KB) {
        snprintf(buffer, sizeof(buffer), "%.2f kb", double(bytes) / KB);
    } else {
        snprintf(buffer, sizeof(buffer), "%llu b", (unsigned long long)bytes);
    }
    return std::string(buffer);
}

std::string DownloadItem::formatSpeed(double bytesPerSecond) const {
    const double KB = 1024.0;
    const double MB = KB * 1024.0;
    
    char buffer[64];
    if (bytesPerSecond >= MB) {
        snprintf(buffer, sizeof(buffer), "%.2f mb/s", bytesPerSecond / MB);
    } else if (bytesPerSecond >= KB) {
        snprintf(buffer, sizeof(buffer), "%.2f kb/s", bytesPerSecond / KB);
    } else {
        snprintf(buffer, sizeof(buffer), "%.0f b/s", bytesPerSecond);
    }
    return std::string(buffer);
}

std::string DownloadItem::formatTime(int seconds) const {
    if (seconds < 0) return "";
    
    int hours = seconds / 3600;
    int mins = (seconds % 3600) / 60;
    int secs = seconds % 60;
    
    char buffer[64];
    if (hours > 0) {
        snprintf(buffer, sizeof(buffer), "%dh %dm %ds", hours, mins, secs);
    } else if (mins > 0) {
        snprintf(buffer, sizeof(buffer), "%dm %ds", mins, secs);
    } else {
        snprintf(buffer, sizeof(buffer), "%ds", secs);
    }
    return std::string(buffer);
}

std::string DownloadItem::getAddedDate() const {
    auto time = std::chrono::system_clock::to_time_t(addedTime);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", localtime(&time));
    return std::string(buf);
}

bool DownloadItem::verifyChecksum() {
    if (state != DownloadState::Completed) {
        return false;
    }
    
    checksumResult = calculateMD5();
    
    if (!expectedChecksum.empty()) {
        std::string expected = expectedChecksum;
        std::string calculated = checksumResult;
        
        // Case-insensitive comparison
        std::transform(expected.begin(), expected.end(), expected.begin(), ::tolower);
        std::transform(calculated.begin(), calculated.end(), calculated.begin(), ::tolower);
        
        checksumVerified = (expected == calculated);
    } else {
        checksumVerified = true;  // No expected checksum, just calculated
    }
    
    return checksumVerified;
}

std::string DownloadItem::calculateMD5() {
    // Simple MD5 implementation for file hashing
    // Using a simplified approach - in production you'd use a proper crypto library
    
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return "error: cannot open file";
    }
    
    // Simple hash for now (not actual MD5, just a placeholder)
    // In production, you would integrate a proper MD5 library like OpenSSL
    uint64_t hash = 0;
    char buffer[8192];
    
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        for (std::streamsize i = 0; i < file.gcount(); ++i) {
            hash = hash * 31 + static_cast<unsigned char>(buffer[i]);
        }
    }
    
    // Convert to hex string
    char hexBuf[32];
    snprintf(hexBuf, sizeof(hexBuf), "%016llx", (unsigned long long)hash);
    return std::string(hexBuf) + " (simple hash)";
}

std::string DownloadItem::calculateSHA256() {
    // Placeholder - would need a proper crypto library
    return calculateMD5();  // Use same simplified hash for now
}

std::string DownloadItem::serialize() const {
    std::ostringstream oss;
    oss << url << "\t"
        << filePath << "\t"
        << static_cast<int>(state.load()) << "\t"
        << bytesReceived.load() << "\t"
        << bytesTotal.load() << "\t"
        << progress.load() << "\t"
        << priority << "\t"
        << checksumType << "\t"
        << expectedChecksum << "\t"
        << customFileName;
    return oss.str();
}

std::unique_ptr<DownloadItem> DownloadItem::deserialize(const std::string& data) {
    std::istringstream iss(data);
    std::string token;
    
    std::string url, filePath;
    int stateInt = 0;
    uint64_t received = 0, total = 0;
    double prog = 0;
    int prio = 0;
    std::string checksumType, expectedChecksum, customName;
    
    if (std::getline(iss, token, '\t')) url = token;
    if (std::getline(iss, token, '\t')) filePath = token;
    if (std::getline(iss, token, '\t')) stateInt = std::stoi(token);
    if (std::getline(iss, token, '\t')) received = std::stoull(token);
    if (std::getline(iss, token, '\t')) total = std::stoull(token);
    if (std::getline(iss, token, '\t')) prog = std::stod(token);
    if (std::getline(iss, token, '\t')) prio = std::stoi(token);
    if (std::getline(iss, token, '\t')) checksumType = token;
    if (std::getline(iss, token, '\t')) expectedChecksum = token;
    if (std::getline(iss, token, '\t')) customName = token;
    
    auto item = std::make_unique<DownloadItem>(url, filePath);
    item->priority = prio;
    item->checksumType = checksumType;
    item->expectedChecksum = expectedChecksum;
    item->customFileName = customName;
    
    // Restore state
    item->bytesReceived = received;
    item->bytesTotal = total;
    item->progress = prog;
    
    if (stateInt == static_cast<int>(DownloadState::Completed)) {
        item->state = DownloadState::Completed;
    } else if (stateInt == static_cast<int>(DownloadState::Paused)) {
        item->state = DownloadState::Paused;
    } else {
        item->state = DownloadState::Queued;
    }
    
    return item;
}
