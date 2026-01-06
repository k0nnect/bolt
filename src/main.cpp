#include "downloadmanager.h"
#include "settings.h"
#include <GLFW/glfw3.h>
#define GL_SILENCE_DEPRECATION
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <cctype>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

// Global pointer for drag & drop callback
static DownloadManager* g_downloadManager = nullptr;

// GLFW drag & drop callback
void dropCallback(GLFWwindow* window, int count, const char** paths) {
    if (g_downloadManager && count > 0) {
        g_downloadManager->handleDroppedFiles(count, paths);
    }
}

// Draw lightning bolt logo
void drawLightningBolt(ImDrawList* drawList, ImVec2 pos, float size, ImU32 color) {
    ImVec2 points[7];
    points[0] = ImVec2(pos.x + size * 0.5f, pos.y);
    points[1] = ImVec2(pos.x + size * 0.2f, pos.y + size * 0.5f);
    points[2] = ImVec2(pos.x + size * 0.4f, pos.y + size * 0.5f);
    points[3] = ImVec2(pos.x, pos.y + size);
    points[4] = ImVec2(pos.x + size * 0.3f, pos.y + size * 0.6f);
    points[5] = ImVec2(pos.x + size * 0.2f, pos.y + size * 0.6f);
    points[6] = ImVec2(pos.x + size * 0.5f, pos.y);
    
    drawList->AddConvexPolyFilled(points, 7, color);
}

void applyTheme(int themeIndex, float scale) {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;
    
    switch (themeIndex) {
        case 0:  // Modern Dark (updated)
            colors[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.09f, 0.11f, 1.00f);
            colors[ImGuiCol_ChildBg] = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
            colors[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.12f, 0.14f, 0.98f);
            colors[ImGuiCol_Border] = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
            colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.16f, 0.19f, 1.00f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.13f, 0.14f, 0.17f, 1.00f);
            colors[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.09f, 0.11f, 1.00f);
            colors[ImGuiCol_TitleBgActive] = ImVec4(0.09f, 0.09f, 0.11f, 1.00f);
            colors[ImGuiCol_MenuBarBg] = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
            colors[ImGuiCol_ScrollbarBg] = ImVec4(0.09f, 0.09f, 0.11f, 1.00f);
            colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.28f, 0.30f, 0.35f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.35f, 0.37f, 0.42f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.42f, 0.44f, 0.49f, 1.00f);
            colors[ImGuiCol_CheckMark] = ImVec4(0.40f, 0.70f, 1.00f, 1.00f);
            colors[ImGuiCol_SliderGrab] = ImVec4(0.40f, 0.70f, 1.00f, 1.00f);
            colors[ImGuiCol_SliderGrabActive] = ImVec4(0.50f, 0.78f, 1.00f, 1.00f);
            colors[ImGuiCol_Button] = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.32f, 0.40f, 1.00f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.35f, 0.42f, 0.54f, 1.00f);
            colors[ImGuiCol_Header] = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
            colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.32f, 0.40f, 1.00f);
            colors[ImGuiCol_HeaderActive] = ImVec4(0.35f, 0.42f, 0.54f, 1.00f);
            colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
            colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.16f, 0.19f, 1.00f);
            colors[ImGuiCol_TabHovered] = ImVec4(0.28f, 0.32f, 0.40f, 1.00f);
            colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
            colors[ImGuiCol_Text] = ImVec4(0.92f, 0.93f, 0.95f, 1.00f);
            colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.52f, 0.55f, 1.00f);
            colors[ImGuiCol_PlotLines] = ImVec4(0.40f, 0.70f, 1.00f, 1.00f);
            colors[ImGuiCol_PlotHistogram] = ImVec4(0.40f, 0.70f, 1.00f, 1.00f);
            break;
            
        case 1:  // Light
            colors[ImGuiCol_WindowBg] = ImVec4(0.96f, 0.96f, 0.96f, 1.00f);
            colors[ImGuiCol_ChildBg] = ImVec4(0.92f, 0.92f, 0.92f, 1.00f);
            colors[ImGuiCol_PopupBg] = ImVec4(0.98f, 0.98f, 0.98f, 0.98f);
            colors[ImGuiCol_Border] = ImVec4(0.78f, 0.78f, 0.78f, 1.00f);
            colors[ImGuiCol_FrameBg] = ImVec4(0.88f, 0.88f, 0.88f, 1.00f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.82f, 0.82f, 0.82f, 1.00f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.75f, 0.75f, 0.75f, 1.00f);
            colors[ImGuiCol_Button] = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.78f, 0.78f, 0.78f, 1.00f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.70f, 0.70f, 0.70f, 1.00f);
            colors[ImGuiCol_Text] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
            colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
            colors[ImGuiCol_CheckMark] = ImVec4(0.20f, 0.50f, 0.90f, 1.00f);
            colors[ImGuiCol_SliderGrab] = ImVec4(0.20f, 0.50f, 0.90f, 1.00f);
            colors[ImGuiCol_PlotLines] = ImVec4(0.20f, 0.50f, 0.90f, 1.00f);
            colors[ImGuiCol_PlotHistogram] = ImVec4(0.20f, 0.50f, 0.90f, 1.00f);
            break;
            
        case 2:  // Midnight Blue
            colors[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.08f, 0.14f, 1.00f);
            colors[ImGuiCol_ChildBg] = ImVec4(0.07f, 0.10f, 0.18f, 1.00f);
            colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.12f, 0.20f, 0.98f);
            colors[ImGuiCol_Border] = ImVec4(0.18f, 0.24f, 0.36f, 1.00f);
            colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.16f, 0.24f, 1.00f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.24f, 0.34f, 1.00f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.10f, 0.14f, 0.22f, 1.00f);
            colors[ImGuiCol_Button] = ImVec4(0.16f, 0.22f, 0.32f, 1.00f);
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.32f, 0.46f, 1.00f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.30f, 0.42f, 0.62f, 1.00f);
            colors[ImGuiCol_Text] = ImVec4(0.88f, 0.92f, 0.98f, 1.00f);
            colors[ImGuiCol_TextDisabled] = ImVec4(0.44f, 0.50f, 0.62f, 1.00f);
            colors[ImGuiCol_CheckMark] = ImVec4(0.45f, 0.68f, 1.00f, 1.00f);
            colors[ImGuiCol_SliderGrab] = ImVec4(0.45f, 0.68f, 1.00f, 1.00f);
            colors[ImGuiCol_PlotLines] = ImVec4(0.45f, 0.68f, 1.00f, 1.00f);
            colors[ImGuiCol_PlotHistogram] = ImVec4(0.45f, 0.68f, 1.00f, 1.00f);
            break;
            
        case 3:  // Forest
            colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.10f, 0.06f, 1.00f);
            colors[ImGuiCol_ChildBg] = ImVec4(0.08f, 0.14f, 0.08f, 1.00f);
            colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.16f, 0.10f, 0.98f);
            colors[ImGuiCol_Border] = ImVec4(0.18f, 0.28f, 0.18f, 1.00f);
            colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.20f, 0.12f, 1.00f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.28f, 0.18f, 1.00f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.10f, 0.16f, 0.10f, 1.00f);
            colors[ImGuiCol_Button] = ImVec4(0.16f, 0.26f, 0.16f, 1.00f);
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.38f, 0.24f, 1.00f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.30f, 0.48f, 0.30f, 1.00f);
            colors[ImGuiCol_Text] = ImVec4(0.88f, 0.96f, 0.88f, 1.00f);
            colors[ImGuiCol_TextDisabled] = ImVec4(0.48f, 0.62f, 0.48f, 1.00f);
            colors[ImGuiCol_CheckMark] = ImVec4(0.45f, 0.88f, 0.52f, 1.00f);
            colors[ImGuiCol_SliderGrab] = ImVec4(0.45f, 0.88f, 0.52f, 1.00f);
            colors[ImGuiCol_PlotLines] = ImVec4(0.45f, 0.88f, 0.52f, 1.00f);
            colors[ImGuiCol_PlotHistogram] = ImVec4(0.45f, 0.88f, 0.52f, 1.00f);
            break;
    }
    
    // Scaled style settings for larger UI
    float s = scale;
    style.WindowPadding = ImVec2(16.0f * s, 16.0f * s);
    style.FramePadding = ImVec2(12.0f * s, 8.0f * s);
    style.ItemSpacing = ImVec2(12.0f * s, 10.0f * s);
    style.ItemInnerSpacing = ImVec2(8.0f * s, 6.0f * s);
    style.IndentSpacing = 24.0f * s;
    style.ScrollbarSize = 18.0f * s;
    style.GrabMinSize = 14.0f * s;
    
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;
    
    style.WindowRounding = 8.0f * s;
    style.ChildRounding = 8.0f * s;
    style.FrameRounding = 6.0f * s;
    style.PopupRounding = 8.0f * s;
    style.ScrollbarRounding = 8.0f * s;
    style.GrabRounding = 6.0f * s;
    style.TabRounding = 6.0f * s;
    
    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.WindowMenuButtonPosition = ImGuiDir_Right;
    style.ColorButtonPosition = ImGuiDir_Right;
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.0f, 0.0f);
}

int main(int, char**) {
    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "failed to initialize glfw\n";
        return -1;
    }

    // GLFW window hints
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    // Create larger window
    GLFWwindow* window = glfwCreateWindow(1400, 900, "bolt", nullptr, nullptr);
    if (!window) {
        std::cerr << "failed to create glfw window\n";
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // Set window icon (lightning bolt)
    {
        // Generate a simple 32x32 lightning bolt icon
        const int iconSize = 32;
        unsigned char iconPixels[iconSize * iconSize * 4];
        memset(iconPixels, 0, sizeof(iconPixels));
        
        // Draw lightning bolt
        auto setPixel = [&](int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
            if (x >= 0 && x < iconSize && y >= 0 && y < iconSize) {
                int idx = (y * iconSize + x) * 4;
                iconPixels[idx + 0] = r;
                iconPixels[idx + 1] = g;
                iconPixels[idx + 2] = b;
                iconPixels[idx + 3] = a;
            }
        };
        
        // Lightning bolt shape (simplified)
        // Upper part
        for (int y = 2; y < 16; y++) {
            int left = 16 - (y - 2);
            int right = 20 - (y - 2) / 2;
            for (int x = left; x < right; x++) {
                setPixel(x, y, 255, 200, 0, 255);  // Gold color
            }
        }
        // Lower part
        for (int y = 14; y < 30; y++) {
            int left = 10 + (y - 14) / 2;
            int right = 18 + (y - 14);
            for (int x = left; x < right; x++) {
                setPixel(x, y, 255, 200, 0, 255);  // Gold color
            }
        }
        
        GLFWimage icon;
        icon.width = iconSize;
        icon.height = iconSize;
        icon.pixels = iconPixels;
        glfwSetWindowIcon(window, 1, &icon);
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    
    // Load larger font
    io.Fonts->AddFontDefault();
    ImFontConfig fontConfig;
    fontConfig.SizePixels = 18.0f;  // Larger font
    io.Fonts->AddFontDefault(&fontConfig);

    // Setup backends
    const char* glsl_version = "#version 330";
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Apply theme with scale
    Settings& settings = Settings::getInstance();
    float uiScale = 1.2f;  // 20% larger
    applyTheme(settings.theme, uiScale);

    // Create download manager
    DownloadManager downloadManager;
    g_downloadManager = &downloadManager;  // Set global pointer for drop callback
    
    // Set up drag & drop callback
    glfwSetDropCallback(window, dropCallback);

    // UI State
    char urlBuffer[4096] = "";
    bool showSettings = false;
    bool showAddDownload = false;
    bool showAbout = false;
    bool showBatchImport = false;
    bool showHistory = false;
    bool showLinkGrabber = false;
    bool showScheduler = false;
    bool showDetails = false;
    bool showTorrents = false;
    bool showAddTorrent = false;
    bool showStatsDashboard = false;
    int detailsIndex = -1;
    int lastTheme = settings.theme;
    
    // Notification state
    std::chrono::steady_clock::time_point clipboardNotificationTime;
    bool clipboardNotificationActive = false;
    const float notificationDuration = 8.0f;  // Auto-dismiss after 8 seconds
    float notificationAlpha = 1.0f;
    
    // Main loop
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        
        // Check for theme changes
        if (settings.theme != lastTheme) {
            applyTheme(settings.theme, uiScale);
            lastTheme = settings.theme;
        }
        
        // Check clipboard
        downloadManager.checkClipboard();

        // Start ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        
        // Use the larger font
        ImGui::PushFont(io.Fonts->Fonts[1]);

        // Main window (fullscreen)
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("bolt_main", nullptr, 
            ImGuiWindowFlags_NoTitleBar | 
            ImGuiWindowFlags_NoResize | 
            ImGuiWindowFlags_NoMove | 
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_MenuBar |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        // Handle keyboard shortcuts
        downloadManager.handleKeyboardShortcuts();
        
        // Menu bar
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("file")) {
                if (ImGui::MenuItem("add download", "Ctrl+N")) {
                    showAddDownload = true;
                }
                if (ImGui::MenuItem("add torrent", "Ctrl+T")) {
                    showAddTorrent = true;
                }
                if (ImGui::MenuItem("batch import", "Ctrl+B")) {
                    showBatchImport = true;
                }
                if (ImGui::MenuItem("link grabber", "Ctrl+G")) {
                    showLinkGrabber = true;
                }
                if (ImGui::MenuItem("add from clipboard", "Ctrl+V")) {
                    if (downloadManager.hasClipboardUrl()) {
                        downloadManager.addDownload(downloadManager.getClipboardUrl());
                        downloadManager.clearClipboardUrl();
                    }
                }
                ImGui::Separator();
                if (ImGui::BeginMenu("import/export")) {
                    if (ImGui::MenuItem("import urls from file...")) {
#ifdef _WIN32
                        OPENFILENAMEA ofn = {0};
                        char szFile[MAX_PATH] = "";
                        ofn.lStructSize = sizeof(ofn);
                        ofn.lpstrFile = szFile;
                        ofn.nMaxFile = sizeof(szFile);
                        ofn.lpstrFilter = "Text Files\0*.txt\0All Files\0*.*\0";
                        ofn.Flags = OFN_FILEMUSTEXIST;
                        if (GetOpenFileNameA(&ofn)) {
                            downloadManager.importDownloads(szFile);
                        }
#endif
                    }
                    if (ImGui::MenuItem("export downloads...")) {
#ifdef _WIN32
                        OPENFILENAMEA ofn = {0};
                        char szFile[MAX_PATH] = "downloads.txt";
                        ofn.lStructSize = sizeof(ofn);
                        ofn.lpstrFile = szFile;
                        ofn.nMaxFile = sizeof(szFile);
                        ofn.lpstrFilter = "Text Files\0*.txt\0All Files\0*.*\0";
                        ofn.lpstrDefExt = "txt";
                        ofn.Flags = OFN_OVERWRITEPROMPT;
                        if (GetSaveFileNameA(&ofn)) {
                            downloadManager.exportDownloads(szFile);
                        }
#endif
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("settings", "Ctrl+,")) {
                    showSettings = true;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("exit", "Alt+F4")) {
                    glfwSetWindowShouldClose(window, true);
                }
                ImGui::EndMenu();
            }
            
            if (ImGui::BeginMenu("downloads")) {
                if (ImGui::MenuItem("pause all", "Ctrl+P")) {
                    downloadManager.pauseAll();
                }
                if (ImGui::MenuItem("resume all", "Ctrl+R")) {
                    downloadManager.resumeAll();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("select all", "Ctrl+A")) {
                    downloadManager.selectAll();
                }
                if (ImGui::MenuItem("deselect all")) {
                    downloadManager.deselectAll();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("clear completed")) {
                    downloadManager.clearCompleted();
                }
                if (ImGui::MenuItem("clear all")) {
                    downloadManager.clearAll();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("verify all checksums")) {
                    downloadManager.verifyAllChecksums();
                }
                ImGui::EndMenu();
            }
            
            if (ImGui::BeginMenu("tools")) {
                if (ImGui::MenuItem("torrents...", "Ctrl+Shift+T")) {
                    showTorrents = true;
                }
                if (ImGui::MenuItem("scheduler...")) {
                    showScheduler = true;
                }
                if (ImGui::MenuItem("download history")) {
                    showHistory = true;
                }
                ImGui::Separator();
                bool shutdownEnabled = downloadManager.getShutdownOnComplete();
                if (ImGui::MenuItem("shutdown when done", nullptr, &shutdownEnabled)) {
                    downloadManager.setShutdownOnComplete(shutdownEnabled);
                }
                ImGui::EndMenu();
            }
            
            if (ImGui::BeginMenu("view")) {
                if (ImGui::MenuItem("compact mode", nullptr, &settings.compactMode)) {
                    settings.save();
                }
                if (ImGui::MenuItem("show speed graph", nullptr, &settings.showSpeedGraph)) {
                    settings.save();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("statistics dashboard", "Ctrl+D")) {
                    showStatsDashboard = true;
                }
                ImGui::EndMenu();
            }
            
            if (ImGui::BeginMenu("help")) {
                if (ImGui::MenuItem("about bolt")) {
                    showAbout = true;
                }
                ImGui::EndMenu();
            }
            
            // Right-aligned status
            float menuWidth = ImGui::GetWindowWidth();
            char statusBuf[128];
            size_t active = downloadManager.getActiveCount();
            snprintf(statusBuf, sizeof(statusBuf), "%zu active", active);
            float textWidth = ImGui::CalcTextSize(statusBuf).x;
            ImGui::SetCursorPosX(menuWidth - textWidth - 30);
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", statusBuf);
            
            ImGui::EndMenuBar();
        }

        // Layout: Sidebar + Main content
        float sidebarWidth = 240.0f;
        
        // Sidebar
        ImGui::BeginChild("sidebar_region", ImVec2(sidebarWidth, -1), false);
        downloadManager.renderSidebar();
        ImGui::EndChild();
        
        ImGui::SameLine();
        
        // Main content area
        ImGui::BeginChild("main_content", ImVec2(-1, -1), false);
        
        // Quick add bar
        ImGui::Text("add new download:");
        ImGui::Spacing();
        
        ImGui::PushItemWidth(-140);
        bool submitted = ImGui::InputTextWithHint("##quickurl", "paste url here (supports http, https, ftp)...", 
            urlBuffer, sizeof(urlBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopItemWidth();
        
        ImGui::SameLine();
        if (ImGui::Button("download", ImVec2(120, 0)) || submitted) {
            if (strlen(urlBuffer) > 0) {
                downloadManager.addDownload(std::string(urlBuffer));
                urlBuffer[0] = '\0';
            }
        }
        
        ImGui::Spacing();
        ImGui::Spacing();
        
        // Statistics bar
        downloadManager.renderStatistics();
        
        // Speed graph
        downloadManager.renderSpeedGraph();
        
        ImGui::Spacing();
        
        // Toolbar
        downloadManager.renderToolbar();
        
        ImGui::Spacing();
        
        // Download list
        downloadManager.render();
        
        ImGui::EndChild();
        
        ImGui::End();
        
        ImGui::PopFont();

        // Dialogs (use larger font)
        ImGui::PushFont(io.Fonts->Fonts[1]);
        downloadManager.renderSettingsPanel(&showSettings);
        downloadManager.renderAddDownloadDialog(&showAddDownload, urlBuffer, sizeof(urlBuffer));
        downloadManager.renderBatchImportDialog(&showBatchImport);
        downloadManager.renderHistoryPanel(&showHistory);
        downloadManager.renderLinkGrabberDialog(&showLinkGrabber);
        downloadManager.renderSchedulerPanel(&showScheduler);
        downloadManager.renderTorrentPanel(&showTorrents);
        downloadManager.renderAddTorrentDialog(&showAddTorrent);
        downloadManager.renderStatisticsDashboard(&showStatsDashboard);
        if (detailsIndex >= 0) {
            downloadManager.renderDownloadDetailsPanel(&showDetails, detailsIndex);
            if (!showDetails) detailsIndex = -1;
        }
        
        // Clipboard notification popup with auto-fade
        if (downloadManager.hasClipboardUrl()) {
            // Start notification timer if new URL detected
            if (!clipboardNotificationActive) {
                clipboardNotificationActive = true;
                clipboardNotificationTime = std::chrono::steady_clock::now();
                notificationAlpha = 1.0f;
            }
            
            // Calculate time elapsed
            auto now = std::chrono::steady_clock::now();
            float elapsed = std::chrono::duration<float>(now - clipboardNotificationTime).count();
            
            // Auto-dismiss after duration
            if (elapsed >= notificationDuration) {
                downloadManager.clearClipboardUrl();
                clipboardNotificationActive = false;
            } else {
                // Start fading out in the last 2 seconds
                if (elapsed > notificationDuration - 2.0f) {
                    notificationAlpha = (notificationDuration - elapsed) / 2.0f;
                } else {
                    notificationAlpha = 1.0f;
                }
                
                // Apply alpha to window
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, notificationAlpha);
                
                ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 420, io.DisplaySize.y - 150));
                ImGui::SetNextWindowSize(ImVec2(400, 130));
                ImGui::Begin("clipboard_popup", nullptr, 
                    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
                
                // Header with X button
                ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "url detected in clipboard");
                
                // X button on the right
                ImGui::SameLine(ImGui::GetWindowWidth() - 35);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.3f, 0.3f, 0.5f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.2f, 0.2f, 0.7f));
                if (ImGui::Button("X", ImVec2(25, 25))) {
                    downloadManager.clearClipboardUrl();
                    clipboardNotificationActive = false;
                }
                ImGui::PopStyleColor(3);
                
                ImGui::Spacing();
                
                std::string clipUrl = downloadManager.getClipboardUrl();
                if (clipUrl.length() > 50) {
                    clipUrl = clipUrl.substr(0, 47) + "...";
                }
                ImGui::TextWrapped("%s", clipUrl.c_str());
                
                ImGui::Spacing();
                
                if (ImGui::Button("download", ImVec2(120, 32))) {
                    downloadManager.addDownload(downloadManager.getClipboardUrl());
                    downloadManager.clearClipboardUrl();
                    clipboardNotificationActive = false;
                }
                ImGui::SameLine();
                if (ImGui::Button("dismiss", ImVec2(120, 32))) {
                    downloadManager.clearClipboardUrl();
                    clipboardNotificationActive = false;
                }
                
                // Show countdown timer
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%.0fs", notificationDuration - elapsed);
                
                ImGui::End();
                ImGui::PopStyleVar();
            }
        } else {
            clipboardNotificationActive = false;
        }

        // About dialog
        if (showAbout) {
            ImGui::OpenPopup("about bolt");
            showAbout = false;
        }
        
        ImGui::SetNextWindowSize(ImVec2(500, 380));
        if (ImGui::BeginPopupModal("about bolt", nullptr, ImGuiWindowFlags_NoResize)) {
            ImGui::Spacing();
            
            // Center the title
            float titleWidth = ImGui::CalcTextSize("bolt v1.0.1").x;
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - titleWidth) / 2);
            ImGui::TextColored(ImVec4(0.45f, 0.72f, 1.0f, 1.0f), "bolt");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "v1.0.1");
            
            ImGui::Spacing();
            
            // Center description
            const char* desc = "a fast, minimalist download manager";
            float descWidth = ImGui::CalcTextSize(desc).x;
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - descWidth) / 2);
            ImGui::Text("%s", desc);
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            
            // Credits - centered and prominent
            const char* credits = "developed by k0nnect";
            float creditsWidth = ImGui::CalcTextSize(credits).x;
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - creditsWidth) / 2);
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", credits);
            
            ImGui::Spacing();
            
            // GitHub link - centered and clickable
            const char* githubUrl = "https://github.com/k0nnect/bolt";
            float linkWidth = ImGui::CalcTextSize(githubUrl).x;
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - linkWidth) / 2);
            ImGui::TextColored(ImVec4(0.45f, 0.72f, 1.0f, 1.0f), "%s", githubUrl);
            if (ImGui::IsItemHovered()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                ImGui::SetTooltip("click to open in browser");
            }
            if (ImGui::IsItemClicked()) {
#ifdef _WIN32
                ShellExecuteA(NULL, "open", githubUrl, NULL, NULL, SW_SHOWNORMAL);
#elif __APPLE__
                std::string cmd = "open " + std::string(githubUrl);
                system(cmd.c_str());
#else
                std::string cmd = "xdg-open " + std::string(githubUrl);
                system(cmd.c_str());
#endif
            }
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            
            // Features in compact format
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "features:");
            ImGui::TextWrapped("http/https/ftp downloads, torrent support, file categorization, speed limiting, clipboard monitoring, themes, queue management, show in folder");
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            
            float buttonWidth = 140;
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - buttonWidth) / 2);
            if (ImGui::Button("close", ImVec2(buttonWidth, 36))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::PopFont();

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        
        // Clear with theme-appropriate color
        float bgColor = (settings.theme == 1) ? 0.96f : 0.06f;
        glClearColor(bgColor, bgColor, bgColor, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }

    // Cleanup
    settings.save();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
