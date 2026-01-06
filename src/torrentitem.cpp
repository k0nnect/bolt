#include "torrentitem.h"
#include "imgui.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>

#ifdef _WIN32
#include <windows.h>
#endif

TorrentItem::TorrentItem(const std::string& source, const std::string& savePath)
    : source(source)
    , savePath(savePath)
    , state(TorrentState::Queued)
{
    lastHistoryUpdate = std::chrono::steady_clock::now();
    
    // Extract name from source
    if (isMagnet()) {
        // Parse magnet link for display name
        std::regex dnRegex("dn=([^&]+)");
        std::smatch match;
        if (std::regex_search(source, match, dnRegex)) {
            name = match[1].str();
            // URL decode
            std::string decoded;
            for (size_t i = 0; i < name.length(); ++i) {
                if (name[i] == '%' && i + 2 < name.length()) {
                    int value;
                    std::istringstream is(name.substr(i + 1, 2));
                    if (is >> std::hex >> value) {
                        decoded += static_cast<char>(value);
                        i += 2;
                    } else {
                        decoded += name[i];
                    }
                } else if (name[i] == '+') {
                    decoded += ' ';
                } else {
                    decoded += name[i];
                }
            }
            name = decoded;
        } else {
            name = "unknown torrent";
        }
        
        // Extract info hash
        std::regex hashRegex("btih:([a-fA-F0-9]{40})");
        if (std::regex_search(source, match, hashRegex)) {
            infoHash = match[1].str();
        }
    } else {
        // .torrent file - extract filename
        name = std::filesystem::path(source).stem().string();
    }
}

TorrentItem::~TorrentItem() {
    cancel();
    if (updateThreadHandle.joinable()) {
        updateThreadHandle.join();
    }
}

bool TorrentItem::isMagnet() const {
    return source.find("magnet:") == 0;
}

std::string TorrentItem::getName() const {
    return name.empty() ? "unknown" : name;
}

std::string TorrentItem::getStateString() const {
    switch (state.load()) {
        case TorrentState::Queued: return "queued";
        case TorrentState::Checking: return "checking";
        case TorrentState::Downloading: return "downloading";
        case TorrentState::Seeding: return "seeding";
        case TorrentState::Paused: return "paused";
        case TorrentState::Completed: return "completed";
        case TorrentState::Error: return "error: " + errorMessage;
        default: return "unknown";
    }
}

std::string TorrentItem::getETA() const {
    if (state != TorrentState::Downloading || downloadSpeed <= 0 || totalSize == 0) {
        return "";
    }
    
    int64_t remaining = totalSize - downloaded.load();
    int seconds = static_cast<int>(remaining / downloadSpeed.load());
    return formatTime(seconds);
}

void TorrentItem::start() {
#ifdef BOLT_ENABLE_TORRENT
    if (state == TorrentState::Queued || state == TorrentState::Paused) {
        shouldStop = false;
        state = TorrentState::Downloading;
        
        if (updateThreadHandle.joinable()) {
            updateThreadHandle.join();
        }
        updateThreadHandle = std::thread(&TorrentItem::updateThread, this);
    }
#else
    state = TorrentState::Error;
    errorMessage = "torrent support not compiled in";
#endif
}

void TorrentItem::pause() {
#ifdef BOLT_ENABLE_TORRENT
    if (state == TorrentState::Downloading || state == TorrentState::Seeding) {
        state = TorrentState::Paused;
        if (handle.is_valid()) {
            handle.pause();
        }
    }
#endif
}

void TorrentItem::resume() {
#ifdef BOLT_ENABLE_TORRENT
    if (state == TorrentState::Paused) {
        state = TorrentState::Downloading;
        if (handle.is_valid()) {
            handle.resume();
        }
    }
#endif
}

void TorrentItem::cancel() {
    shouldStop = true;
#ifdef BOLT_ENABLE_TORRENT
    if (handle.is_valid()) {
        // The session should remove the torrent
    }
#endif
    state = TorrentState::Paused;
}

void TorrentItem::setFilePriority(size_t index, int priority) {
#ifdef BOLT_ENABLE_TORRENT
    if (handle.is_valid() && index < files.size()) {
        std::lock_guard<std::mutex> lock(filesMutex);
        files[index].priority = priority;
        // Update in libtorrent
        // handle.file_priority(index, priority);
    }
#endif
}

void TorrentItem::setAllPriorities(int priority) {
    std::lock_guard<std::mutex> lock(filesMutex);
    for (auto& file : files) {
        file.priority = priority;
    }
}

void TorrentItem::updateThread() {
#ifdef BOLT_ENABLE_TORRENT
    // This would contain the actual libtorrent session handling
    // For now, this is a placeholder that would integrate with TorrentManager
#endif
    
    while (!shouldStop && state == TorrentState::Downloading) {
        // Update speed history
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastHistoryUpdate).count();
        
        if (elapsed >= 500) {
            speedHistory.push_back(static_cast<float>(downloadSpeed.load()));
            if (speedHistory.size() > 120) {
                speedHistory.pop_front();
            }
            lastHistoryUpdate = now;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void TorrentItem::parseMetadata() {
    // Parse .torrent file or receive metadata from magnet
}

std::string TorrentItem::formatBytes(int64_t bytes) const {
    const int64_t KB = 1024;
    const int64_t MB = KB * 1024;
    const int64_t GB = MB * 1024;
    
    char buffer[64];
    if (bytes >= GB) {
        snprintf(buffer, sizeof(buffer), "%.2f gb", static_cast<double>(bytes) / GB);
    } else if (bytes >= MB) {
        snprintf(buffer, sizeof(buffer), "%.2f mb", static_cast<double>(bytes) / MB);
    } else if (bytes >= KB) {
        snprintf(buffer, sizeof(buffer), "%.2f kb", static_cast<double>(bytes) / KB);
    } else {
        snprintf(buffer, sizeof(buffer), "%lld b", static_cast<long long>(bytes));
    }
    return std::string(buffer);
}

std::string TorrentItem::formatSpeed(double bytesPerSecond) const {
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

std::string TorrentItem::formatTime(int seconds) const {
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

void TorrentItem::render(size_t index, bool compactMode) {
    if (compactMode) {
        renderCompact(index);
    } else {
        renderDetailed(index);
    }
}

void TorrentItem::renderCompact(size_t index) {
    ImGui::PushID(static_cast<int>(index) + 10000);  // Offset to avoid ID collision
    
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.10f, 0.12f, 1.0f));
    ImGui::BeginChild("torrent_compact", ImVec2(-1, 50), true, ImGuiWindowFlags_NoScrollbar);
    
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
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "TORRENT");
        
        // Name
        ImGui::TableNextColumn();
        std::string displayName = getName();
        if (displayName.length() > 35) {
            displayName = displayName.substr(0, 32) + "...";
        }
        ImGui::Text("%s", displayName.c_str());
        
        // Progress bar
        ImGui::TableNextColumn();
        float progressValue = static_cast<float>(progress.load()) / 100.0f;
        ImGui::ProgressBar(progressValue, ImVec2(-1, 18), "");
        
        // Percentage
        ImGui::TableNextColumn();
        ImGui::Text("%.0f%%", progress.load());
        
        // Speed/Status
        ImGui::TableNextColumn();
        if (state == TorrentState::Downloading) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s", formatSpeed(downloadSpeed).c_str());
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", getStateString().c_str());
        }
        
        // Action button
        ImGui::TableNextColumn();
        if (state == TorrentState::Paused || state == TorrentState::Queued) {
            if (ImGui::Button("start", ImVec2(60, 24))) { start(); }
        } else if (state == TorrentState::Downloading) {
            if (ImGui::Button("pause", ImVec2(60, 24))) { pause(); }
        }
        
        ImGui::EndTable();
    }
    
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::PopID();
}

void TorrentItem::renderDetailed(size_t index) {
    ImGui::PushID(static_cast<int>(index) + 10000);
    
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.10f, 0.12f, 1.0f));
    ImGui::BeginChild("torrent_detailed", ImVec2(-1, 200), true, ImGuiWindowFlags_NoScrollbar);
    
    // Header: Icon + Name
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "[TORRENT]");
    ImGui::SameLine();
    ImGui::Text("%s", getName().c_str());
    
    // Progress percentage on right
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80);
    if (state == TorrentState::Completed || state == TorrentState::Seeding) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "100%%");
    } else {
        ImGui::Text("%.1f%%", progress.load());
    }
    
    ImGui::Spacing();
    
    // Progress bar
    float progressValue = static_cast<float>(progress.load()) / 100.0f;
    ImVec4 progressColor = ImVec4(1.0f, 0.6f, 0.2f, 1.0f);  // Orange for torrents
    if (state == TorrentState::Completed || state == TorrentState::Seeding) {
        progressColor = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
    } else if (state == TorrentState::Paused) {
        progressColor = ImVec4(1.0f, 0.8f, 0.3f, 1.0f);
    }
    
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, progressColor);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
    ImGui::ProgressBar(progressValue, ImVec2(-1, 16), "");
    ImGui::PopStyleColor(2);
    
    ImGui::Spacing();
    
    // Info row
    ImGui::Text("%s / %s", formatBytes(downloaded).c_str(), formatBytes(totalSize).c_str());
    
    if (state == TorrentState::Downloading || state == TorrentState::Seeding) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), " | ");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "D: %s", formatSpeed(downloadSpeed).c_str());
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.5f, 1.0f), "U: %s", formatSpeed(uploadSpeed).c_str());
        
        std::string eta = getETA();
        if (!eta.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), " | ");
            ImGui::SameLine();
            ImGui::Text("eta: %s", eta.c_str());
        }
    }
    
    // Seeds and peers
    ImGui::Text("seeds: %d | peers: %d", seeds.load(), peers.load());
    
    ImGui::Spacing();
    
    // Buttons
    if (state == TorrentState::Paused || state == TorrentState::Queued) {
        if (ImGui::Button("start", ImVec2(80, 30))) { start(); }
    } else if (state == TorrentState::Downloading) {
        if (ImGui::Button("pause", ImVec2(80, 30))) { pause(); }
    } else if (state == TorrentState::Seeding) {
        if (ImGui::Button("stop seed", ImVec2(80, 30))) { pause(); }
    }
    
    ImGui::SameLine();
    if (ImGui::Button("cancel", ImVec2(80, 30))) { cancel(); }
    
    ImGui::SameLine();
    if (ImGui::Button("open folder", ImVec2(100, 30))) {
#ifdef _WIN32
        ShellExecuteA(NULL, "explore", savePath.c_str(), NULL, NULL, SW_SHOWNORMAL);
#endif
    }
    
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::PopID();
}

std::string TorrentItem::serialize() const {
    std::ostringstream oss;
    oss << "TORRENT\t"
        << source << "\t"
        << savePath << "\t"
        << static_cast<int>(state.load()) << "\t"
        << progress.load() << "\t"
        << downloaded.load() << "\t"
        << uploaded.load() << "\t"
        << totalSize;
    return oss.str();
}

std::unique_ptr<TorrentItem> TorrentItem::deserialize(const std::string& data) {
    std::istringstream iss(data);
    std::string token;
    
    std::string type, src, path;
    int stateInt = 0;
    double prog = 0;
    int64_t down = 0, up = 0, total = 0;
    
    if (std::getline(iss, token, '\t')) type = token;
    if (type != "TORRENT") return nullptr;
    
    if (std::getline(iss, token, '\t')) src = token;
    if (std::getline(iss, token, '\t')) path = token;
    if (std::getline(iss, token, '\t')) stateInt = std::stoi(token);
    if (std::getline(iss, token, '\t')) prog = std::stod(token);
    if (std::getline(iss, token, '\t')) down = std::stoll(token);
    if (std::getline(iss, token, '\t')) up = std::stoll(token);
    if (std::getline(iss, token, '\t')) total = std::stoll(token);
    
    auto item = std::make_unique<TorrentItem>(src, path);
    item->progress = prog;
    item->downloaded = down;
    item->uploaded = up;
    item->totalSize = total;
    
    if (stateInt == static_cast<int>(TorrentState::Completed)) {
        item->state = TorrentState::Completed;
    } else {
        item->state = TorrentState::Paused;
    }
    
    return item;
}
