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
- HTTP module exposed to Lua (`http.get`, `http.get_async`, `http.head`, `http.post`, cookie handling, URL tools, `base64_*`, `sha256`)
- Cookie state shared between `http.*` requests and `ludo.newDownload()` — authenticated sessions carry over automatically
- AES-128-CBC encryption/decryption exposed to Lua (`http.aes128_cbc_{en,de}crypt`)
- Zip archive creation exposed to Lua (`zip.create`)
- HLS/m3u8 playlist parsing and segment download (`plugins/m3u8.lua`)
- Host module exposed to Lua (`ludo.newDownload`, logging, output-dir helpers)
- Native `libui-ng` widgets exposed to Lua as the `ui` module (GUI build only)
- JSON encode/decode available globally (`json.encode`, `json.decode`)
- Plugin-based URL extraction (`plugins/*.lua`)
- Worker-threaded download processing and GUI progress updates
- Runtime settings loaded from `config.ini`

## Project Layout

- `src/` : C source code
- `plugins/` : Lua extraction plugins
- `lualib/` : Lua support libraries loaded via `require` (e.g. `ftcsv`)
- `snippets/` : Sample Lua snippets for the GUI/test windows
- `res/` : GUI resources
- `tools/` : Standalone Lua tool scripts
- `config.ini` : Runtime settings
- `ludo_scripting.md` : Full scripting API reference
- `third_party/curl-8.19.0/` : curl source
- `third_party/lua-5.2.4/` : Lua source
- `third_party/libui-ng/` : UI library source
- `third_party/libuilua/` : libui-ng Lua bindings
- `third_party/brotli/`, `third_party/zlib-1.2.8/`, `third_party/zstd-dev/` : compression libraries
- `third_party/cJSON/`, `third_party/stb/` : helper libraries
- `CMakeLists.txt` : build configuration

## Architecture (High Level)

- Main thread runs `libui-ng` event loop (`uiMain()`)
- Worker threads handle URL tasks, Lua processing, and downloads
- Lua plugin contract:
  - `validate(url)` -> boolean
    - `process(url)` -> typically calls `ludo.newDownload(...)` (may provide an optional filename hint as the 4th argument)
- GUI updates are marshalled back to main thread through `uiQueueMain()`

## Prerequisites

### Windows (w64devkit)

Use **w64devkit** — a self-contained MinGW-w64 toolchain that bundles GCC,
CMake and Ninja. All build commands **must** run inside its shell, otherwise
GCC fails with `cannot execute 'cc1'` (its subprocess directory is not on the
`PATH`):

```bash
# Run from the w64devkit terminal (e.g. C:\w64devkit\w64devkit.exe)
cmake -B build . && cmake --build build --parallel
```

Alternatively, from `cmd.exe`, put the toolchain on `PATH` first (adjust the
GCC version directory to match your install):

```bat
set PATH=C:\w64devkit\bin;C:\w64devkit\libexec\gcc\x86_64-w64-mingw32\15.2.0;%PATH%
cmake --build build --parallel
```

### Linux (WSL2 Ubuntu 22.04)

Install toolchain and common tools:

```bash
sudo apt update
sudo apt install build-essential cmake pkg-config libssl-dev libgtk-3-dev
```

> This project builds `libui-ng` from source via CMake; Meson is not required.

CMake builds two executables:

| Target | Description | Debug name | Release name |
|--------|-------------|------------|--------------|
| `ludo` | GUI downloader (requires libui-ng) | `ludo-debug.exe` | `ludo.exe` |
| `ludocon` | Console-only CLI (no GUI) | `ludocon-debug.exe` | `ludocon.exe` |

It also builds `wenativehost`, a small native-messaging host that lets the
companion browser extension send links to Ludo.

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
cmake --install build --prefix ~/ludo
cmake --install build --prefix C:/PortableApps/ludo
cmake --install build --prefix C:/Program Files/ludo
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

The bundled plugins cover many sites (YouTube, Facebook, Instagram, TikTok,
Twitter/X, Bilibili, and more). The fallback for plain direct links is
`plugins/generic.lua` (shown in the UI as "Generic Direct Download"); a
minimal example lives in `snippets/old/example_plugin.lua`.

See `ludo_scripting.md` for the full scripting API reference.

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
./install/ludo-debug.exe   # or ludo.exe for a Release build
```

### 2) `cannot open output file ludo.exe: Permission denied`

`ludo.exe` is still running and locked. Close it or kill it, then rebuild.

```bash
taskkill /IM ludo-debug.exe /F   # Debug build
taskkill /IM ludo.exe /F         # Release build
cmake --build build --target ludo --parallel 4
```

### 3) Meson note

This repository builds `libui-ng` from source using CMake; Meson is not required to build LUDO.

### 4) Plugins not loaded

Make sure `plugins/` exists beside the executable. Run `cmake --install build --prefix install` to copy all assets.

## License

No license file is provided at the repository root; the LUDO sources are
distributed without an explicit project license.

Third-party components are bundled under their own licenses:

- curl
- libui-ng
- Lua
- brotli
- zlib
- zstd
- cJSON
- stb

Check each upstream `LICENSE`/`COPYING` file in `third_party/`.
