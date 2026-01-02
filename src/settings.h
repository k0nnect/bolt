#ifndef SETTINGS_H
#define SETTINGS_H

#include <string>
#include <vector>
#include <fstream>
#include <filesystem>

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>
#endif

struct Settings {
    // Download settings
    std::string downloadPath;
    int maxConcurrentDownloads = 3;
    int maxSegmentsPerDownload = 8;
    int speedLimitKBps = 0;  // 0 = unlimited
    bool autoStartDownloads = true;
    bool showNotifications = true;
    bool minimizeToTray = false;
    bool startMinimized = false;
    bool clipboardMonitoring = true;
    
    // Proxy settings
    bool useProxy = false;
    std::string proxyAddress = "";
    int proxyPort = 8080;
    bool proxyAuth = false;
    std::string proxyUsername = "";
    std::string proxyPassword = "";
    
    // Auto-categorization - save to subfolders
    bool autoCategorize = true;
    std::string videoFolder = "Videos";
    std::string audioFolder = "Music";
    std::string documentFolder = "Documents";
    std::string archiveFolder = "Archives";
    std::string programFolder = "Programs";
    std::string imageFolder = "Images";
    std::string otherFolder = "";
    
    // Post-download actions
    bool verifyChecksum = false;
    bool playSoundOnComplete = true;
    bool showBalloonNotification = true;
    bool autoShutdown = false;
    int shutdownAction = 0;  // 0=shutdown, 1=sleep, 2=hibernate
    
    // Advanced
    int connectionsPerDownload = 4;  // Multi-segment
    int connectionTimeout = 30;
    int retryAttempts = 3;
    int retryDelay = 5;
    bool limitSpeedDuringHours = false;
    int limitStartHour = 9;
    int limitEndHour = 17;
    int limitSpeedKBps = 100;
    
    // Scheduler
    bool schedulerEnabled = false;
    int schedulerStartHour = 0;
    int schedulerStartMinute = 0;
    int schedulerEndHour = 23;
    int schedulerEndMinute = 59;
    
    // UI settings
    int theme = 0;  // 0 = dark, 1 = light, 2 = midnight blue, 3 = forest
    float uiScale = 1.0f;
    bool showSpeedGraph = true;
    bool compactMode = false;
    
    // File categories
    std::vector<std::string> videoExtensions = {".mp4", ".mkv", ".avi", ".mov", ".wmv", ".flv", ".webm"};
    std::vector<std::string> audioExtensions = {".mp3", ".wav", ".flac", ".aac", ".ogg", ".m4a", ".wma"};
    std::vector<std::string> documentExtensions = {".pdf", ".doc", ".docx", ".xls", ".xlsx", ".ppt", ".pptx", ".txt"};
    std::vector<std::string> archiveExtensions = {".zip", ".rar", ".7z", ".tar", ".gz", ".bz2"};
    std::vector<std::string> programExtensions = {".exe", ".msi", ".dmg", ".app", ".deb", ".rpm"};
    std::vector<std::string> imageExtensions = {".jpg", ".jpeg", ".png", ".gif", ".bmp", ".webp", ".svg"};
    
    static Settings& getInstance() {
        static Settings instance;
        return instance;
    }
    
    void load();
    void save();
    std::string getConfigPath();
    
private:
    Settings() { load(); }
};

#endif // SETTINGS_H
