# bolt

a minimalist download manager built with c++ and dear imgui.

## features

### core
- fast downloads with resume support
- pause/resume/cancel functionality
- download queue with configurable concurrent downloads
- speed limiting per download or globally
- clipboard monitoring with auto-detection of urls

### ui
- clean, minimalist interface
- multiple themes (dark, light, midnight blue, forest)
- compact and detailed view modes
- real-time speed graph
- file categorization with icons (video, audio, document, archive, program, image)
- sidebar with filter options (all, downloading, completed, queued, paused, errors)
- search and filter downloads

### settings
- configurable download path
- concurrent download limit (1-10)
- global speed limit
- scheduler for download times
- persistent settings saved to config file

### cross-platform
- windows, linux, macos support
- no external dependencies required (all fetched via cmake)

## requirements

- cmake 3.20 or higher
- c++17 compatible compiler
- opengl 3.3+ support

## building

### windows

```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

### linux

```bash
mkdir build
cd build
cmake ..
make -j$(nproc)
```

### macos

```bash
mkdir build
cd build
cmake ..
make -j$(sysctl -n hw.ncpu)
```

## running

after building, run the executable:

```bash
# windows
build/Release/bolt.exe

# linux/macos
./build/bolt
```

## installation

### windows

copy the executable to your desired location or add to path.

### linux

```bash
sudo cmake --install build
```

### macos

```bash
sudo cmake --install build
```

## usage

1. enter a url in the input field
2. click "download" or press enter
3. monitor progress in the download list
4. pause, resume, or cancel downloads as needed

## contributing

contributions are welcome! please feel free to submit a pull request.

## license

mit
