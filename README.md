# LUDO (LUa DOwnloader)

LUDO is a lightweight, cross-platform download manager with:

- Native GUI built with `libui-ng`
- Download engine in C using `libcurl`
- Extensible URL extraction via Lua plugins

The application lets you paste URLs in the GUI, then resolves and downloads them through a Lua-driven plugin pipeline.

## Features

- GUI downloader app (`libui-ng`)
- Static Lua runtime (`lua-5.2.4` from source)
- HTTP module exposed to Lua (`http.get`, `http.head`, `http.post`, cookies, URL tools)
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
  - `process(url)` -> typically calls `ludo.newDownload(...)`
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

> If you do not have Meson (for example in `w64devkit`), see the prebuilt `libui` option below.

## Build

### Option A: Build `libui-ng` via Meson (default)

From repository root:

```bash
cmake -B build .
cmake --build build --parallel 4
```

### Build Configurations (`Debug` / `Release`)

This project explicitly supports `Debug` and `Release` configurations.

- Single-config generators (Ninja/Makefiles): use `CMAKE_BUILD_TYPE`
- Multi-config generators: use `--config <Debug|Release>` at build time

Single-config examples:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug .
cmake --build build --parallel 4
```

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release .
cmake --build build --parallel 4
```

Multi-config examples:

```bash
cmake -B build .
cmake --build build --config Debug --parallel 4
cmake --build build --config Release --parallel 4
```

### Option B: Use prebuilt `libui.a` (no Meson required)

CMake supports a prebuilt `libui` flow:

- `LUDO_USE_PREBUILT_LIBUI=ON`
- `LUDO_LIBUI_LIBRARY=<path to libui.a>`
- `LUDO_LIBUI_INCLUDE_DIR=<path containing ui.h>`

Example:

```bash
cmake -B build . \
  -DLUDO_USE_PREBUILT_LIBUI=ON \
  -DLUDO_LIBUI_LIBRARY="C:/path/to/libui.a" \
  -DLUDO_LIBUI_INCLUDE_DIR="C:/path/to/libui/headers"

cmake --build build --parallel 4
```

## Run

From repository root:

```bash
./build/ludo.exe
```

Or from inside `build/`:

```bash
./ludo.exe
```

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

- GCC/Clang/MinGW: `-O3` and linker `-s`
- MSVC: `/O2` and linker `/OPT:REF` + `/OPT:ICF`

`dumpbin`/`objdump` will still show normal Windows system DLL imports (for example `KERNEL32.dll`, `USER32.dll`, `COMCTL32.dll`, etc.).

## Troubleshooting

### 1) App exits immediately with no window

- Ensure you are running the newest built executable.
- Rebuild cleanly and run again:

```bash
cmake --build build --target ludo --clean-first --parallel 4
./build/ludo.exe
```

### 2) `cannot open output file ludo.exe: Permission denied`

`ludo.exe` is still running and locked. Close it or kill it, then rebuild.

```bash
taskkill /IM ludo.exe /F
cmake --build build --target ludo --parallel 4
```

### 3) Meson not found

Either install Meson or use prebuilt `libui` mode (Option B above).

### 4) Plugins not loaded

Make sure `plugins/` exists beside the executable (`build/plugins` is copied after build).

## License

This repository includes third-party components under their own licenses:

- curl
- libui-ng
- Lua

Check each upstream `LICENSE`/`COPYING` file in `third_party/`.
