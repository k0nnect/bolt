#include "settings.h"
#include <sstream>
#include <iostream>

std::string Settings::getConfigPath() {
#ifdef _WIN32
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, NULL, &path))) {
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, path, -1, NULL, 0, NULL, NULL);
        std::string result(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, path, -1, &result[0], size_needed, NULL, NULL);
        CoTaskMemFree(path);
        if (!result.empty() && result.back() == '\0') {
            result.pop_back();
        }
        return result + "/bolt";
    }
    return "./bolt_config";
#else
    const char* home = getenv("HOME");
    if (home) {
        return std::string(home) + "/.config/bolt";
    }
    return "./bolt_config";
#endif
}

void Settings::load() {
    std::string configDir = getConfigPath();
    std::string configFile = configDir + "/settings.cfg";
    
    // Set default download path
#ifdef _WIN32
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, NULL, &path))) {
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, path, -1, NULL, 0, NULL, NULL);
        downloadPath.resize(size_needed);
        WideCharToMultiByte(CP_UTF8, 0, path, -1, &downloadPath[0], size_needed, NULL, NULL);
        CoTaskMemFree(path);
        if (!downloadPath.empty() && downloadPath.back() == '\0') {
            downloadPath.pop_back();
        }
    } else {
        downloadPath = ".";
    }
#else
    const char* home = getenv("HOME");
    if (home) {
        downloadPath = std::string(home) + "/Downloads";
    } else {
        downloadPath = ".";
    }
#endif
    
    if (!std::filesystem::exists(configFile)) {
        return;
    }
    
    std::ifstream file(configFile);
    if (!file.is_open()) {
        return;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        size_t pos = line.find('=');
        if (pos == std::string::npos) continue;
        
        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        
        if (key == "downloadPath") downloadPath = value;
        else if (key == "maxConcurrentDownloads") maxConcurrentDownloads = std::stoi(value);
        else if (key == "maxSegmentsPerDownload") maxSegmentsPerDownload = std::stoi(value);
        else if (key == "speedLimitKBps") speedLimitKBps = std::stoi(value);
        else if (key == "autoStartDownloads") autoStartDownloads = (value == "1");
        else if (key == "showNotifications") showNotifications = (value == "1");
        else if (key == "minimizeToTray") minimizeToTray = (value == "1");
        else if (key == "startMinimized") startMinimized = (value == "1");
        else if (key == "clipboardMonitoring") clipboardMonitoring = (value == "1");
        else if (key == "schedulerEnabled") schedulerEnabled = (value == "1");
        else if (key == "schedulerStartHour") schedulerStartHour = std::stoi(value);
        else if (key == "schedulerStartMinute") schedulerStartMinute = std::stoi(value);
        else if (key == "schedulerEndHour") schedulerEndHour = std::stoi(value);
        else if (key == "schedulerEndMinute") schedulerEndMinute = std::stoi(value);
        else if (key == "theme") theme = std::stoi(value);
        else if (key == "uiScale") uiScale = std::stof(value);
        else if (key == "showSpeedGraph") showSpeedGraph = (value == "1");
        else if (key == "compactMode") compactMode = (value == "1");
        else if (key == "useProxy") useProxy = (value == "1");
        else if (key == "proxyAddress") proxyAddress = value;
        else if (key == "proxyPort") proxyPort = std::stoi(value);
        else if (key == "proxyAuth") proxyAuth = (value == "1");
        else if (key == "proxyUsername") proxyUsername = value;
        else if (key == "proxyPassword") proxyPassword = value;
        else if (key == "autoCategorize") autoCategorize = (value == "1");
        else if (key == "videoFolder") videoFolder = value;
        else if (key == "audioFolder") audioFolder = value;
        else if (key == "documentFolder") documentFolder = value;
        else if (key == "archiveFolder") archiveFolder = value;
        else if (key == "programFolder") programFolder = value;
        else if (key == "imageFolder") imageFolder = value;
        else if (key == "otherFolder") otherFolder = value;
        else if (key == "verifyChecksum") verifyChecksum = (value == "1");
        else if (key == "playSoundOnComplete") playSoundOnComplete = (value == "1");
        else if (key == "showBalloonNotification") showBalloonNotification = (value == "1");
        else if (key == "autoShutdown") autoShutdown = (value == "1");
        else if (key == "shutdownAction") shutdownAction = std::stoi(value);
        else if (key == "connectionsPerDownload") connectionsPerDownload = std::stoi(value);
        else if (key == "connectionTimeout") connectionTimeout = std::stoi(value);
        else if (key == "retryAttempts") retryAttempts = std::stoi(value);
        else if (key == "retryDelay") retryDelay = std::stoi(value);
        else if (key == "limitSpeedDuringHours") limitSpeedDuringHours = (value == "1");
        else if (key == "limitStartHour") limitStartHour = std::stoi(value);
        else if (key == "limitEndHour") limitEndHour = std::stoi(value);
        else if (key == "limitSpeedKBps") limitSpeedKBps = std::stoi(value);
    }
}

void Settings::save() {
    std::string configDir = getConfigPath();
    std::filesystem::create_directories(configDir);
    
    std::string configFile = configDir + "/settings.cfg";
    std::ofstream file(configFile);
    if (!file.is_open()) {
        return;
    }
    
    file << "downloadPath=" << downloadPath << "\n";
    file << "maxConcurrentDownloads=" << maxConcurrentDownloads << "\n";
    file << "maxSegmentsPerDownload=" << maxSegmentsPerDownload << "\n";
    file << "speedLimitKBps=" << speedLimitKBps << "\n";
    file << "autoStartDownloads=" << (autoStartDownloads ? "1" : "0") << "\n";
    file << "showNotifications=" << (showNotifications ? "1" : "0") << "\n";
    file << "minimizeToTray=" << (minimizeToTray ? "1" : "0") << "\n";
    file << "startMinimized=" << (startMinimized ? "1" : "0") << "\n";
    file << "clipboardMonitoring=" << (clipboardMonitoring ? "1" : "0") << "\n";
    file << "schedulerEnabled=" << (schedulerEnabled ? "1" : "0") << "\n";
    file << "schedulerStartHour=" << schedulerStartHour << "\n";
    file << "schedulerStartMinute=" << schedulerStartMinute << "\n";
    file << "schedulerEndHour=" << schedulerEndHour << "\n";
    file << "schedulerEndMinute=" << schedulerEndMinute << "\n";
    file << "theme=" << theme << "\n";
    file << "uiScale=" << uiScale << "\n";
    file << "showSpeedGraph=" << (showSpeedGraph ? "1" : "0") << "\n";
    file << "compactMode=" << (compactMode ? "1" : "0") << "\n";
    file << "useProxy=" << (useProxy ? "1" : "0") << "\n";
    file << "proxyAddress=" << proxyAddress << "\n";
    file << "proxyPort=" << proxyPort << "\n";
    file << "proxyAuth=" << (proxyAuth ? "1" : "0") << "\n";
    file << "proxyUsername=" << proxyUsername << "\n";
    file << "proxyPassword=" << proxyPassword << "\n";
    file << "autoCategorize=" << (autoCategorize ? "1" : "0") << "\n";
    file << "videoFolder=" << videoFolder << "\n";
    file << "audioFolder=" << audioFolder << "\n";
    file << "documentFolder=" << documentFolder << "\n";
    file << "archiveFolder=" << archiveFolder << "\n";
    file << "programFolder=" << programFolder << "\n";
    file << "imageFolder=" << imageFolder << "\n";
    file << "otherFolder=" << otherFolder << "\n";
    file << "verifyChecksum=" << (verifyChecksum ? "1" : "0") << "\n";
    file << "playSoundOnComplete=" << (playSoundOnComplete ? "1" : "0") << "\n";
    file << "showBalloonNotification=" << (showBalloonNotification ? "1" : "0") << "\n";
    file << "autoShutdown=" << (autoShutdown ? "1" : "0") << "\n";
    file << "shutdownAction=" << shutdownAction << "\n";
    file << "connectionsPerDownload=" << connectionsPerDownload << "\n";
    file << "connectionTimeout=" << connectionTimeout << "\n";
    file << "retryAttempts=" << retryAttempts << "\n";
    file << "retryDelay=" << retryDelay << "\n";
    file << "limitSpeedDuringHours=" << (limitSpeedDuringHours ? "1" : "0") << "\n";
    file << "limitStartHour=" << limitStartHour << "\n";
    file << "limitEndHour=" << limitEndHour << "\n";
    file << "limitSpeedKBps=" << limitSpeedKBps << "\n";
}
