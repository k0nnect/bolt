cmake_minimum_required(VERSION 3.20)
project(bolt VERSION 1.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable FetchContent
include(FetchContent)

# Fetch GLFW
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.3.8
)
FetchContent_MakeAvailable(glfw)

# Fetch Dear ImGui (header-only, just download source at configure time)
set(imgui_SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/imgui-src")
if(NOT EXISTS "${imgui_SOURCE_DIR}/imgui.h")
    message(STATUS "Downloading ImGui...")
    find_package(Git QUIET)
    if(Git_FOUND)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} clone --depth 1 --branch v1.92.5
                    https://github.com/ocornut/imgui.git ${imgui_SOURCE_DIR}
            RESULT_VARIABLE GIT_CLONE_RESULT
        )
        if(NOT GIT_CLONE_RESULT EQUAL "0")
            message(FATAL_ERROR "Failed to clone ImGui repository")
        endif()
    else()
        message(FATAL_ERROR "Git not found. Please install Git to download ImGui.")
    endif()
endif()

# Fetch libcurl with SSL support
FetchContent_Declare(
    curl
    GIT_REPOSITORY https://github.com/curl/curl.git
    GIT_TAG curl-8_5_0
)
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)  # Use Windows native SSL
set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAPS ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_TELNET ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_DICT ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_FILE ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_TFTP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_RTSP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_POP3 ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_IMAP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_SMTP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_GOPHER ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(curl)

# ImGui sources
set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

# Source files
set(SOURCES
    src/main.cpp
    src/downloadmanager.cpp
    src/downloaditem.cpp
    src/settings.cpp
    src/torrentitem.cpp
    src/torrentmanager.cpp
    ${IMGUI_SOURCES}
)

set(HEADERS
    src/downloadmanager.h
    src/downloaditem.h
    src/settings.h
    src/resource.h
    src/torrentitem.h
    src/torrentmanager.h
)

# Optional libtorrent support
option(BOLT_ENABLE_TORRENT "Enable torrent support via libtorrent" OFF)

if(BOLT_ENABLE_TORRENT)
    find_package(LibtorrentRasterbar CONFIG QUIET)
    if(LibtorrentRasterbar_FOUND)
        add_compile_definitions(BOLT_ENABLE_TORRENT)
        message(STATUS "libtorrent found - torrent support enabled")
    else()
        message(WARNING "libtorrent not found - torrent support disabled")
    endif()
endif()

# Windows resource file for application icon
if(WIN32)
    set(WIN_RESOURCES src/resource.rc)
endif()

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${WIN_RESOURCES})

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    src
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    glfw
    ${CMAKE_DL_LIBS}
)

# Link curl (target name may vary)
if(TARGET libcurl)
    target_link_libraries(${PROJECT_NAME} PRIVATE libcurl)
    target_include_directories(${PROJECT_NAME} PRIVATE ${curl_SOURCE_DIR}/include)
elseif(TARGET CURL::libcurl)
    target_link_libraries(${PROJECT_NAME} PRIVATE CURL::libcurl)
else()
    # Fallback: try to find curl
    find_package(CURL)
    if(CURL_FOUND)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${CURL_LIBRARIES})
        target_include_directories(${PROJECT_NAME} PRIVATE ${CURL_INCLUDE_DIRS})
    else()
        message(FATAL_ERROR "Could not find or build libcurl")
    endif()
endif()

# Link libtorrent if available
if(BOLT_ENABLE_TORRENT AND LibtorrentRasterbar_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE LibtorrentRasterbar::torrent-rasterbar)
endif()

# OpenGL (platform-specific)
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE opengl32)
    # Don't set WIN32_EXECUTABLE - we want a console app for GLFW
elseif(APPLE)
    find_library(COCOA_FRAMEWORK Cocoa)
    find_library(IOKIT_FRAMEWORK IOKit)
    find_library(COREVIDEO_FRAMEWORK CoreVideo)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${COCOA_FRAMEWORK}
        ${IOKIT_FRAMEWORK}
        ${COREVIDEO_FRAMEWORK}
    )
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE OpenGL::GL)
endif()

# Compiler-specific options
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
endif()

# Installation
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)
