# LUDO (LUa DOwnloader)

LUDO is a lightweight, cross-platform download manager with:

- Native GUI built with `libui-ng`
- **Console-only CLI** (`ludocon`) with no GUI dependency
- Download engine in C using `libcurl`
- Extensible URL extraction via Lua plugins

The application lets you paste URLs in the GUI (or pass them as command-line arguments to `ludocon`), then resolves and downloads them through a Lua-driven plugin pipeline.

## Features

- GUI downloader app (`libui-ng`) or **console-only CLI** (`ludocon`)
- Static Lua runtime (`lua-5.2.4` from source)
- HTTP module exposed to Lua (`http.get`, `http.get_async`, `http.head`, `http.post`, cookies, URL tools)
- AES-128-CBC encryption/decryption exposed to Lua (`http.aes128_cbc_{en,de}crypt`)
- HLS/m3u8 playlist parsing and segment download (`plugins/m3u8.lua`)
- Host module exposed to Lua (`ludo.newDownload`, logging, output-dir helpers)
- Plugin-based URL extraction (`plugins/*.lua`)
- Worker-threaded download processing and GUI progress updates
- Runtime settings loaded from `config.ini`

## Project Layout

- `src/` : C source code
- `plugins/` : Lua extraction plugins
- `third_party/libui-ng/` : UI library source
- `third_party/curl-8.19.0/` : curl source
- `third_party/lua-5.2.4/` : Lua source
- `CMakeLists.txt` : build configuration

## Architecture (High Level)

- Main thread runs `libui-ng` event loop (`uiMain()`)
- Worker threads handle URL tasks, Lua processing, and downloads
- Lua plugin contract:
  - `validate(url)` -> boolean
    - `process(url)` -> typically calls `ludo.newDownload(...)` (may provide an optional filename hint as the 4th argument)
- GUI updates are marshalled back to main thread through `uiQueueMain()`

## Prerequisites

### Windows (MSYS2 MinGW-64)

Install toolchain and common tools:

```bash
pacman -Syu
pacman -S --needed \
  mingw-w64-x86_64-toolchain \
  mingw-w64-x86_64-cmake \
  mingw-w64-x86_64-ninja \
  mingw-w64-x86_64-meson
```

### Linux (WSL2 Ubuntu 22.04)

Install toolchain and common tools:

```bash
sudo apt update
sudo apt install pkg-config cmake libssl-dev libgtk-3-dev
```

> This project builds `libui-ng` from source via CMake; Meson is not required.

CMake builds two executables:

| Target | Description | Debug name | Release name |
|--------|-------------|------------|--------------|
| `ludo` | GUI downloader (requires libui-ng) | `ludo-debug.exe` | `ludo.exe` |
| `ludocon` | Console-only CLI (no GUI) | `ludocon-debug.exe` | `ludocon.exe` |

## Workflow

### 1. Build

Configure and compile from repository root:

```bash
# Debug build (GUI) — default
cmake -B build .
cmake --build build --parallel

# Release build (GUI)
cmake -B build -DCMAKE_BUILD_TYPE=Release .
cmake --build build --parallel

# Console-only (no GUI dependencies)
cmake -B build . -DBUILD_GUI=OFF
cmake --build build --parallel
```

### 2. Install

Copy executables and all assets (plugins, config, snippets, lualib, res, tools) to a local directory:

```bash
cmake --install build --prefix install
```

This creates the `install/` directory with everything needed to run.

### 3. Run

```bash
# GUI
./install/ludo-debug.exe

# Console — download a URL
./install/ludocon-debug.exe https://www.dailymotion.com/video/x5kesuj

# Console — read URLs from file
./install/ludocon-debug.exe --file urls.txt

# Console — help
./install/ludocon-debug.exe --help
```

### 4. Test

Run a test script (output goes to `install/ludo.log`):

```bash
./install/ludo-debug.exe -s test_PLUGINNAME.lua
grep -E "PASS|FAIL|SUCCESS|ERROR" install/ludo.log | tail -30
```

### 5. Package

Create a `ludo-<version>.zip` distribution archive via CPack:

```bash
cmake --build build --target package
```

The package contains all executables, config, plugins, and assets — ready to unzip and run.

## Configuration

Ludo reads `config.ini` at startup. Common settings include:

- `maxDownloadRetry`
- `maxThread`
- `urlQueueCapacity`
- `downloadQueueCapacity`
- `maxRedirect`
- `outputDir`
- `pluginDir`

## VS Code Tasks

This repo includes `.vscode/tasks.json` with configure/build tasks.

- `CMake: Configure`
- `CMake: Build`
- `Build (Configure + Build)`

You can run default build with `Ctrl+Shift+B`.

## Plugin System

Plugins live in `plugins/` and are loaded at startup.

Included examples:

- `plugins/example_plugin.lua`
- `plugins/generic_direct_download.lua`

A plugin should return a table and implement:

```lua
function plugin.validate(url)
  return true or false
end

function plugin.process(url)
  -- typically use http.* and then call ludo.newDownload(...)
end
```

## Notes on Static/Independent Windows Build

Current CMake is configured so `ludo.exe` does not require local `libcurl.dll` or `libstdc++-6.dll` next to the executable.

For `Release` builds, CMake also applies optimization and symbol stripping:

- `-O3` and linker `-s`

`dumpbin`/`objdump` will still show normal Windows system DLL imports (for example `KERNEL32.dll`, `USER32.dll`, `COMCTL32.dll`, etc.).

## Troubleshooting

### 1) App exits immediately with no window

- Ensure you are running the newest built executable.
- Rebuild cleanly and run again:

```bash
cmake --build build --target ludo --clean-first --parallel 4
cmake --install build --prefix install
./install/ludo.exe
```

### 2) `cannot open output file ludo.exe: Permission denied`

`ludo.exe` is still running and locked. Close it or kill it, then rebuild.

```bash
taskkill /IM ludo.exe /F
cmake --build build --target ludo --parallel 4
```

### 3) Meson note

This repository builds `libui-ng` from source using CMake; Meson is not required to build LUDO.

### 4) Plugins not loaded

Make sure `plugins/` exists beside the executable. Run `cmake --install build --prefix install` to copy all assets.

## License

This repository includes third-party components under their own licenses:

- curl
- libui-ng
- Lua

Check each upstream `LICENSE`/`COPYING` file in `third_party/`.
