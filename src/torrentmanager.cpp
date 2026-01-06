#include "torrentmanager.h"
#include "settings.h"
#include "imgui.h"
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <commdlg.h>
#endif

TorrentManager::TorrentManager() {
#ifdef BOLT_ENABLE_TORRENT
    lt::settings_pack pack;
    pack.set_int(lt::settings_pack::alert_mask, lt::alert::status_notification 
        | lt::alert::error_notification 
        | lt::alert::progress_notification);
    
    session = std::make_unique<lt::session>(pack);
    
    // Start session thread
    sessionThreadHandle = std::thread(&TorrentManager::sessionThread, this);
#endif
    
    loadTorrents();
}

TorrentManager::~TorrentManager() {
    running = false;
    
    saveTorrents();
    
    if (sessionThreadHandle.joinable()) {
        sessionThreadHandle.join();
    }
    
#ifdef BOLT_ENABLE_TORRENT
    // Pause all torrents before shutdown
    for (auto& torrent : torrents) {
        if (torrent) {
            torrent->pause();
        }
    }
#endif
}

bool TorrentManager::isAvailable() {
#ifdef BOLT_ENABLE_TORRENT
    return true;
#else
    return false;
#endif
}

void TorrentManager::addTorrent(const std::string& source, const std::string& savePath) {
    std::string path = savePath.empty() ? getDefaultSavePath() : savePath;
    
    std::lock_guard<std::mutex> lock(torrentsMutex);
    auto torrent = std::make_unique<TorrentItem>(source, path);
    
#ifdef BOLT_ENABLE_TORRENT
    if (session) {
        lt::add_torrent_params params;
        
        if (torrent->isMagnet()) {
            params = lt::parse_magnet_uri(source);
        } else {
            params.ti = std::make_shared<lt::torrent_info>(source);
        }
        
        params.save_path = path;
        
        lt::torrent_handle handle = session->add_torrent(params);
        // Store handle in torrent item (would need to add this)
    }
#endif
    
    torrents.push_back(std::move(torrent));
    
    // Auto-start if setting enabled
    if (Settings::getInstance().autoStartDownloads) {
        torrents.back()->start();
    }
}

void TorrentManager::addMagnet(const std::string& magnetUri, const std::string& savePath) {
    addTorrent(magnetUri, savePath);
}

void TorrentManager::removeTorrent(size_t index, bool deleteFiles) {
    std::lock_guard<std::mutex> lock(torrentsMutex);
    
    if (index < torrents.size()) {
        if (torrents[index]) {
            torrents[index]->cancel();
            
#ifdef BOLT_ENABLE_TORRENT
            // Remove from session
            // session->remove_torrent(torrents[index]->getHandle(), deleteFiles ? lt::session::delete_files : 0);
#endif
            
            if (deleteFiles) {
                // Delete downloaded files
                try {
                    std::filesystem::remove_all(torrents[index]->getSavePath() + "/" + torrents[index]->getName());
                } catch (...) {}
            }
        }
        torrents.erase(torrents.begin() + index);
    }
}

void TorrentManager::pauseTorrent(size_t index) {
    std::lock_guard<std::mutex> lock(torrentsMutex);
    if (index < torrents.size() && torrents[index]) {
        torrents[index]->pause();
    }
}

void TorrentManager::resumeTorrent(size_t index) {
    std::lock_guard<std::mutex> lock(torrentsMutex);
    if (index < torrents.size() && torrents[index]) {
        torrents[index]->resume();
    }
}

void TorrentManager::pauseAll() {
    std::lock_guard<std::mutex> lock(torrentsMutex);
    for (auto& torrent : torrents) {
        if (torrent) {
            torrent->pause();
        }
    }
}

void TorrentManager::resumeAll() {
    std::lock_guard<std::mutex> lock(torrentsMutex);
    for (auto& torrent : torrents) {
        if (torrent && torrent->getState() == TorrentState::Paused) {
            torrent->resume();
        }
    }
}

void TorrentManager::setMaxDownloadSpeed(int kbps) {
    maxDownloadSpeed = kbps;
#ifdef BOLT_ENABLE_TORRENT
    if (session) {
        lt::settings_pack pack;
        pack.set_int(lt::settings_pack::download_rate_limit, kbps * 1024);
        session->apply_settings(pack);
    }
#endif
}

void TorrentManager::setMaxUploadSpeed(int kbps) {
    maxUploadSpeed = kbps;
#ifdef BOLT_ENABLE_TORRENT
    if (session) {
        lt::settings_pack pack;
        pack.set_int(lt::settings_pack::upload_rate_limit, kbps * 1024);
        session->apply_settings(pack);
    }
#endif
}

void TorrentManager::setMaxConnections(int connections) {
    maxConnections = connections;
#ifdef BOLT_ENABLE_TORRENT
    if (session) {
        lt::settings_pack pack;
        pack.set_int(lt::settings_pack::connections_limit, connections);
        session->apply_settings(pack);
    }
#endif
}

void TorrentManager::setDHTEnabled(bool enabled) {
    dhtEnabled = enabled;
#ifdef BOLT_ENABLE_TORRENT
    if (session) {
        lt::settings_pack pack;
        pack.set_bool(lt::settings_pack::enable_dht, enabled);
        session->apply_settings(pack);
    }
#endif
}

void TorrentManager::setPort(int port) {
    listenPort = port;
    // Would need to restart session to change port
}

size_t TorrentManager::getActiveTorrentCount() const {
    size_t count = 0;
    for (const auto& t : torrents) {
        if (t && t->getState() == TorrentState::Downloading) count++;
    }
    return count;
}

size_t TorrentManager::getSeedingCount() const {
    size_t count = 0;
    for (const auto& t : torrents) {
        if (t && t->getState() == TorrentState::Seeding) count++;
    }
    return count;
}

TorrentItem* TorrentManager::getTorrent(size_t index) {
    std::lock_guard<std::mutex> lock(torrentsMutex);
    if (index < torrents.size()) {
        return torrents[index].get();
    }
    return nullptr;
}

void TorrentManager::update() {
    updateStats();
    
#ifdef BOLT_ENABLE_TORRENT
    if (!session) return;
    
    std::vector<lt::alert*> alerts;
    session->pop_alerts(&alerts);
    
    for (lt::alert* alert : alerts) {
        // Handle different alert types
        if (auto* state_alert = lt::alert_cast<lt::state_update_alert>(alert)) {
            for (const auto& status : state_alert->status) {
                // Find matching torrent and update its state
                std::lock_guard<std::mutex> lock(torrentsMutex);
                for (auto& torrent : torrents) {
                    if (torrent && torrent->getInfoHash() == lt::aux::to_hex(status.info_hash)) {
                        // Update torrent with status
                        // This would require additional methods in TorrentItem
                    }
                }
            }
        }
    }
    
    // Request status updates
    session->post_torrent_updates();
#endif
}

void TorrentManager::sessionThread() {
#ifdef BOLT_ENABLE_TORRENT
    while (running) {
        update();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
#endif
}

void TorrentManager::updateStats() {
    stats.totalTorrents = static_cast<int>(torrents.size());
    stats.activeTorrents = 0;
    stats.seedingTorrents = 0;
    stats.downloadSpeed = 0.0;
    stats.uploadSpeed = 0.0;
    
    for (const auto& torrent : torrents) {
        if (!torrent) continue;
        
        TorrentState state = torrent->getState();
        if (state == TorrentState::Downloading) {
            stats.activeTorrents++;
            stats.downloadSpeed += torrent->getDownloadSpeed();
            stats.uploadSpeed += torrent->getUploadSpeed();
        } else if (state == TorrentState::Seeding) {
            stats.seedingTorrents++;
            stats.uploadSpeed += torrent->getUploadSpeed();
        }
    }
}

void TorrentManager::render(bool compactMode) {
    std::lock_guard<std::mutex> lock(torrentsMutex);
    
    if (torrents.empty()) {
        ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "no torrents");
        ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.3f, 1.0f), "add a .torrent file or magnet link");
        return;
    }
    
    for (size_t i = 0; i < torrents.size(); ++i) {
        if (torrents[i]) {
            torrents[i]->render(i, compactMode);
        }
    }
}

void TorrentManager::renderAddTorrentDialog(bool* open) {
    if (!*open) return;
    
    ImGui::SetNextWindowSize(ImVec2(600, 300), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("add torrent", open)) {
        static char sourceBuffer[4096] = "";
        static char pathBuffer[512] = "";
        static bool firstOpen = true;
        
        if (firstOpen) {
            strncpy(pathBuffer, getDefaultSavePath().c_str(), sizeof(pathBuffer));
            firstOpen = false;
        }
        
        ImGui::Text("torrent source (file path or magnet link):");
        ImGui::PushItemWidth(-80);
        ImGui::InputTextWithHint("##source", "paste magnet link or .torrent path...", 
            sourceBuffer, sizeof(sourceBuffer));
        ImGui::PopItemWidth();
        
        ImGui::SameLine();
        if (ImGui::Button("browse", ImVec2(70, 0))) {
#ifdef _WIN32
            OPENFILENAMEA ofn = {0};
            char szFile[MAX_PATH] = "";
            ofn.lStructSize = sizeof(ofn);
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = sizeof(szFile);
            ofn.lpstrFilter = "Torrent Files\0*.torrent\0All Files\0*.*\0";
            ofn.Flags = OFN_FILEMUSTEXIST;
            
            if (GetOpenFileNameA(&ofn)) {
                strncpy(sourceBuffer, szFile, sizeof(sourceBuffer));
            }
#endif
        }
        
        ImGui::Spacing();
        
        ImGui::Text("save to:");
        ImGui::PushItemWidth(-80);
        ImGui::InputText("##savepath", pathBuffer, sizeof(pathBuffer));
        ImGui::PopItemWidth();
        
        ImGui::SameLine();
        if (ImGui::Button("...", ImVec2(70, 0))) {
#ifdef _WIN32
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
                            strncpy(pathBuffer, result.c_str(), sizeof(pathBuffer));
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
        ImGui::Separator();
        ImGui::Spacing();
        
        // Torrent support status
        if (!isAvailable()) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), 
                "note: torrent support requires libtorrent. currently showing placeholder UI.");
            ImGui::Spacing();
        }
        
        if (ImGui::Button("add torrent", ImVec2(120, 32))) {
            if (strlen(sourceBuffer) > 0) {
                addTorrent(sourceBuffer, pathBuffer);
                sourceBuffer[0] = '\0';
                *open = false;
            }
        }
        
        ImGui::SameLine();
        if (ImGui::Button("cancel", ImVec2(120, 32))) {
            *open = false;
        }
    }
    ImGui::End();
}

void TorrentManager::loadTorrents() {
    Settings& settings = Settings::getInstance();
    std::string torrentsPath = settings.getConfigPath() + "/torrents.txt";
    
    if (!std::filesystem::exists(torrentsPath)) return;
    
    std::ifstream file(torrentsPath);
    if (!file.is_open()) return;
    
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        auto torrent = TorrentItem::deserialize(line);
        if (torrent) {
            std::lock_guard<std::mutex> lock(torrentsMutex);
            torrents.push_back(std::move(torrent));
        }
    }
}

void TorrentManager::saveTorrents() {
    Settings& settings = Settings::getInstance();
    std::filesystem::create_directories(settings.getConfigPath());
    
    std::string torrentsPath = settings.getConfigPath() + "/torrents.txt";
    std::ofstream file(torrentsPath);
    if (!file.is_open()) return;
    
    std::lock_guard<std::mutex> lock(torrentsMutex);
    for (const auto& torrent : torrents) {
        if (torrent) {
            file << torrent->serialize() << "\n";
        }
    }
}

std::string TorrentManager::getDefaultSavePath() const {
    return Settings::getInstance().downloadPath;
}
