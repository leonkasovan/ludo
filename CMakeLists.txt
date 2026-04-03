cmake_minimum_required(VERSION 3.16)
project(ludo C CXX RC)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11) 
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -------------------------
# Versioning (auto-increment build)
# -------------------------
set(LUDO_VERSION_MAJOR 0)
set(LUDO_VERSION_MINOR 6)
set(LUDO_VERSION_PATCH 0)

set(LUDO_VERSION_FILE "${CMAKE_SOURCE_DIR}/build_version.txt")
if(EXISTS "${LUDO_VERSION_FILE}")
    file(READ "${LUDO_VERSION_FILE}" _build_val)
    string(STRIP "${_build_val}" _build_val)
    if(_build_val STREQUAL "")
        set(_build_val 0)
    endif()
    math(EXPR _build_val "${_build_val} + 1")
else()
    set(_build_val 1)
endif()
file(WRITE "${LUDO_VERSION_FILE}" "${_build_val}\n")
set(LUDO_VERSION_BUILD ${_build_val})
set(LUDO_VERSION_STR "${LUDO_VERSION_MAJOR}.${LUDO_VERSION_MINOR}.${LUDO_VERSION_PATCH}.${LUDO_VERSION_BUILD}")
set(LUDO_COMPANY "leonkasovan@gmail.com")
set(LUDO_DESCRIPTION "Lua powered and streamlined download manager")

# ------------------------------------------------------------
# Build configurations
# ------------------------------------------------------------
get_property(LUDO_IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

if(LUDO_IS_MULTI_CONFIG)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "Available build configurations" FORCE)
else()
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
    endif()
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release")
endif()

# ------------------------------------------------------------
# System Dependencies
# ------------------------------------------------------------
find_package(Threads REQUIRED) 

# ------------------------------------------------------------
# Build options
# ------------------------------------------------------------
option(LUDO_USE_PREBUILT_LIBUI "Use a prebuilt libui static library instead of building with Meson" OFF)
set(LUDO_LIBUI_LIBRARY "" CACHE FILEPATH "Path to prebuilt libui static library (e.g. C:/path/to/libui.a)")
set(LUDO_LIBUI_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/third_party/libui-ng" CACHE PATH "Path to libui headers (ui.h)")

# ------------------------------------------------------------
# zlib 1.2.8 (built from vendored source, static)
# ------------------------------------------------------------
set(ZLIB_SRC_DIR ${CMAKE_SOURCE_DIR}/third_party/zlib-1.2.8)
set(ZLIB_OUTPUT_DIR ${CMAKE_BINARY_DIR}/zlib/lib)

set(ZLIB_SOURCES
    ${ZLIB_SRC_DIR}/adler32.c
    ${ZLIB_SRC_DIR}/compress.c
    ${ZLIB_SRC_DIR}/crc32.c
    ${ZLIB_SRC_DIR}/deflate.c
    ${ZLIB_SRC_DIR}/gzclose.c
    ${ZLIB_SRC_DIR}/gzlib.c
    ${ZLIB_SRC_DIR}/gzread.c
    ${ZLIB_SRC_DIR}/gzwrite.c
    ${ZLIB_SRC_DIR}/infback.c
    ${ZLIB_SRC_DIR}/inffast.c
    ${ZLIB_SRC_DIR}/inflate.c
    ${ZLIB_SRC_DIR}/inftrees.c
    ${ZLIB_SRC_DIR}/trees.c
    ${ZLIB_SRC_DIR}/uncompr.c
    ${ZLIB_SRC_DIR}/zutil.c
)

add_library(zlib_static STATIC ${ZLIB_SOURCES})
target_include_directories(zlib_static PUBLIC ${ZLIB_SRC_DIR})
set_target_properties(zlib_static PROPERTIES
    OUTPUT_NAME zlibstatic
    ARCHIVE_OUTPUT_DIRECTORY ${ZLIB_OUTPUT_DIR}
)
if(CMAKE_CONFIGURATION_TYPES)
    set_target_properties(zlib_static PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${ZLIB_OUTPUT_DIR}
        ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${ZLIB_OUTPUT_DIR}
    )
endif()

if(MSVC)
    set(ZLIB_STATIC_LIBRARY ${ZLIB_OUTPUT_DIR}/zlibstatic.lib)
else()
    set(ZLIB_STATIC_LIBRARY ${ZLIB_OUTPUT_DIR}/libzlibstatic.a)
endif()

# ------------------------------------------------------------
# Lua 5.2.4 (built from source)
# ------------------------------------------------------------
set(LUA_SRC_DIR ${CMAKE_SOURCE_DIR}/third_party/lua-5.2.4/src)

set(LUA_SOURCES
    ${LUA_SRC_DIR}/lapi.c ${LUA_SRC_DIR}/lauxlib.c ${LUA_SRC_DIR}/lbaselib.c
    ${LUA_SRC_DIR}/lbitlib.c ${LUA_SRC_DIR}/lcode.c ${LUA_SRC_DIR}/lcorolib.c
    ${LUA_SRC_DIR}/lctype.c ${LUA_SRC_DIR}/ldblib.c ${LUA_SRC_DIR}/ldebug.c
    ${LUA_SRC_DIR}/ldo.c ${LUA_SRC_DIR}/ldump.c ${LUA_SRC_DIR}/lfunc.c
    ${LUA_SRC_DIR}/lgc.c ${LUA_SRC_DIR}/linit.c ${LUA_SRC_DIR}/liolib.c
    ${LUA_SRC_DIR}/llex.c ${LUA_SRC_DIR}/lmathlib.c ${LUA_SRC_DIR}/lmem.c
    ${LUA_SRC_DIR}/loadlib.c ${LUA_SRC_DIR}/lobject.c ${LUA_SRC_DIR}/lopcodes.c
    ${LUA_SRC_DIR}/loslib.c ${LUA_SRC_DIR}/lparser.c ${LUA_SRC_DIR}/lstate.c
    ${LUA_SRC_DIR}/lstring.c ${LUA_SRC_DIR}/lstrlib.c ${LUA_SRC_DIR}/ltable.c
    ${LUA_SRC_DIR}/ltablib.c ${LUA_SRC_DIR}/ltm.c ${LUA_SRC_DIR}/lundump.c
    ${LUA_SRC_DIR}/lvm.c ${LUA_SRC_DIR}/lzio.c
)

add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua PUBLIC ${LUA_SRC_DIR})

if(WIN32)
    target_compile_definitions(lua PRIVATE LUA_USE_WINDOWS)
elseif(UNIX AND NOT APPLE)
    target_compile_definitions(lua PRIVATE LUA_USE_LINUX)
    target_link_libraries(lua PUBLIC m dl)
elseif(APPLE)
    target_compile_definitions(lua PRIVATE LUA_USE_MACOSX)
    target_link_libraries(lua PUBLIC m dl)
endif()

# ------------------------------------------------------------
# libcurl (built from source)
# ------------------------------------------------------------
include(ExternalProject)
set(BROTLI_BUILD_DIR ${CMAKE_BINARY_DIR}/brotli_build)
set(BROTLI_INSTALL_DIR ${CMAKE_BINARY_DIR}/brotli_install)

if(MSVC)
    set(BROTLI_LIB_NAME brotlidec-static.lib)
    set(BROTLI_ENC_NAME brotlienc-static.lib)
    set(BROTLI_COM_NAME brotlicommon-static.lib)
else()
    set(BROTLI_LIB_NAME libbrotlidec-static.a)
    set(BROTLI_ENC_NAME libbrotlienc-static.a)
    set(BROTLI_COM_NAME libbrotlicommon-static.a)
endif()

if(CMAKE_BUILD_TYPE)
    set(BROTLI_BUILD_CONFIG_ARG --config ${CMAKE_BUILD_TYPE})
else()
    set(BROTLI_BUILD_CONFIG_ARG "")
endif()

## Build brotli in-tree instead of via ExternalProject
# Force static-only, disable CLI/tools and tests to match previous ExternalProject settings.
set(BROTLI_BUILD_TOOLS OFF CACHE BOOL "Build/install CLI tools" FORCE)
set(BROTLI_BUILD_TESTS OFF CACHE BOOL "Build test suite" FORCE)
set(BROTLI_DISABLE_TESTS ON CACHE BOOL "Disable tests" FORCE)

# Ensure we build static libraries for bundled brotli (global BUILD_SHARED_LIBS
# is already forced off for our in-tree builds elsewhere; set again to be explicit).
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)

add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/brotli ${BROTLI_BUILD_DIR})

# Provide compatibility targets used elsewhere in this CMakeLists.
# brotli's CMake defines targets named either brotlidec/brotlienc/brotlicommon
# (their type follows BUILD_SHARED_LIBS). Create *_static alias targets so
# existing references (brotlidec_static, brotlienc_static, brotlicommon_static)
# keep working. If brotli isn't available, fall back to the previous imported
# target behavior pointing at the install directory.
if(TARGET brotlidec OR TARGET brotlidec-static OR TARGET brotlienc OR TARGET brotlienc-static OR TARGET brotlicommon OR TARGET brotlicommon-static)
    if(TARGET brotlidec)
        add_library(brotlidec_static ALIAS brotlidec)
    elseif(TARGET brotlidec-static)
        add_library(brotlidec_static ALIAS brotlidec-static)
    endif()

    if(TARGET brotlienc)
        add_library(brotlienc_static ALIAS brotlienc)
    elseif(TARGET brotlienc-static)
        add_library(brotlienc_static ALIAS brotlienc-static)
    endif()

    if(TARGET brotlicommon)
        add_library(brotlicommon_static ALIAS brotlicommon)
    elseif(TARGET brotlicommon-static)
        add_library(brotlicommon_static ALIAS brotlicommon-static)
    endif()

    # Expose include/library paths so Find modules (e.g. curl) can locate brotli.
    set(BROTLI_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/third_party/brotli/c/include CACHE PATH "Brotli include dir" FORCE)
    if(DEFINED brotlicommon_BINARY_DIR)
        set(BROTLI_LIBRARY ${brotlicommon_BINARY_DIR}/${BROTLI_COM_NAME} CACHE FILEPATH "Brotli library" FORCE)
    else()
        set(BROTLI_LIBRARY ${BROTLI_BUILD_DIR}/lib/${BROTLI_COM_NAME} CACHE FILEPATH "Brotli library" FORCE)
    endif()
else()
    add_library(brotlidec_static STATIC IMPORTED GLOBAL)
    add_library(brotlienc_static STATIC IMPORTED GLOBAL)
    add_library(brotlicommon_static STATIC IMPORTED GLOBAL)
    set_target_properties(brotlidec_static PROPERTIES IMPORTED_LOCATION ${BROTLI_INSTALL_DIR}/lib/${BROTLI_LIB_NAME})
    set_target_properties(brotlienc_static PROPERTIES IMPORTED_LOCATION ${BROTLI_INSTALL_DIR}/lib/${BROTLI_ENC_NAME})
    set_target_properties(brotlicommon_static PROPERTIES IMPORTED_LOCATION ${BROTLI_INSTALL_DIR}/lib/${BROTLI_COM_NAME})
endif()

set(ZSTD_BUILD_DIR ${CMAKE_BINARY_DIR}/zstd_build)
set(ZSTD_INSTALL_DIR ${CMAKE_BINARY_DIR}/zstd_install)

if(MSVC)
    set(ZSTD_LIB_NAME zstd_static.lib)
else()
    set(ZSTD_LIB_NAME libzstd.a)
endif()

# Build zstd in-tree (was ExternalProject_Add)
# Force static-only, no programs/tests to match previous ExternalProject settings.
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "Build command-line programs" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "Build test suite" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "Build shared libraries" FORCE)
set(ZSTD_BUILD_STATIC ON  CACHE BOOL "Build static libraries" FORCE)

add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/zstd-dev ${ZSTD_BUILD_DIR})

# Provide compatibility targets used elsewhere in this CMakeLists
if(TARGET libzstd_static)
    add_library(zstd_static ALIAS libzstd_static)
    set(ZSTD_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/third_party/zstd-dev/lib CACHE PATH "Zstd include dir" FORCE)
    if(DEFINED libzstd_BINARY_DIR)
        set(ZSTD_LIBRARY ${libzstd_BINARY_DIR}/${ZSTD_LIB_NAME} CACHE FILEPATH "Zstd library" FORCE)
    else()
        set(ZSTD_LIBRARY ${ZSTD_BUILD_DIR}/lib/${ZSTD_LIB_NAME} CACHE FILEPATH "Zstd library" FORCE)
    endif()
    set(ZSTD_USE_STATIC_LIBS ON CACHE BOOL "Configure for static zstd libraries" FORCE)
else()
    # Fallback to imported target pointing to install location (if used)
    add_library(zstd_static STATIC IMPORTED GLOBAL)
    set_target_properties(zstd_static PROPERTIES IMPORTED_LOCATION ${ZSTD_INSTALL_DIR}/lib/${ZSTD_LIB_NAME})
endif()

set(CURL_BUILD_DIR ${CMAKE_BINARY_DIR}/curl_build)
set(CURL_INSTALL_DIR ${CMAKE_BINARY_DIR}/curl_install)

if(MSVC)
    set(CURL_LIB_NAME libcurl.lib)
else()
    # MinGW builds of curl append a -d suffix for Debug builds (libcurl-d.a).
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(CURL_LIB_NAME libcurl-d.a)
    else()
        set(CURL_LIB_NAME libcurl.a)
    endif()
endif()

# Configure curl SSL backend selection for in-tree build
# When building libcurl in-tree (`add_subdirectory`), set the curl
# SSL backend cache variables directly so the curl CMake picks the
# correct backend (Schannel on Windows, OpenSSL on Unix-like).
if(WIN32)
    set(CURL_USE_SCHANNEL ON CACHE BOOL "Enable Windows native SSL/TLS (Schannel)" FORCE)
    set(CURL_USE_OPENSSL OFF CACHE BOOL "Disable OpenSSL for SSL/TLS on Windows" FORCE)
elseif(APPLE)
    # Prefer OpenSSL on macOS if available; curl can still enable Apple SecTrust
    # for certificate verification (USE_APPLE_SECTRUST) if configured later.
    set(CURL_USE_OPENSSL ON CACHE BOOL "Enable OpenSSL for SSL/TLS on macOS" FORCE)
    set(CURL_USE_SCHANNEL OFF CACHE BOOL "Disable Schannel on non-Windows" FORCE)
else()
    set(CURL_USE_OPENSSL ON CACHE BOOL "Enable OpenSSL for SSL/TLS on Unix-like systems" FORCE)
    set(CURL_USE_SCHANNEL OFF CACHE BOOL "Disable Schannel on non-Windows" FORCE)
endif()

 # ExternalProject_Add(curl_ext) removed — build curl in-tree via add_subdirectory.
 # Curl's important CMake options (BROTLI/ZSTD/SSL/etc.) are preserved below for in-tree build.

# Build libcurl from bundled source instead of ExternalProject
# Configure curl build options for in-tree build
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "Build static libraries" FORCE)
set(BUILD_CURL_EXE OFF CACHE BOOL "Build curl executable" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "Disable testing for curl" FORCE)
set(CURL_USE_PKGCONFIG OFF CACHE BOOL "Disable pkg-config for curl" FORCE)
set(CURL_ENABLE_SSL ON CACHE BOOL "Enable SSL support in curl" FORCE)
set(ENABLE_THREADED_RESOLVER ON CACHE BOOL "Enable threaded resolver" FORCE)
set(CURL_BROTLI ON CACHE BOOL "Enable Brotli in curl" FORCE)
set(CURL_ZSTD ON CACHE BOOL "Enable Zstd in curl" FORCE)
set(CURL_ZLIB ON CACHE BOOL "Enable zlib in curl" FORCE)
set(CURL_DROP_UNUSED ON CACHE BOOL "Drop unused code/data from curl" FORCE)

# Disable authentication and policy features not needed for a simple
# download manager to reduce footprint and avoid pulling GSSAPI/SSPI.
set(CURL_DISABLE_NEGOTIATE_AUTH ON CACHE BOOL "Disable Negotiate (SPNEGO) authentication" FORCE)
set(CURL_DISABLE_KERBEROS_AUTH ON CACHE BOOL "Disable Kerberos authentication" FORCE)
set(CURL_DISABLE_HSTS ON CACHE BOOL "Disable HSTS support" FORCE)
# Disable SCP/SFTP and SSH backends to keep libcurl HTTP/HTTPS-only for the
# download manager. These MUST be set before add_subdirectory(curl-8.19.0).
set(CURL_DISABLE_SCP ON CACHE BOOL "Disable SCP protocol" FORCE)
set(CURL_DISABLE_SFTP ON CACHE BOOL "Disable SFTP protocol" FORCE)
# Force libssh/libssh2 off so curl won't find or link SSH backends.
set(CURL_USE_LIBSSH2 OFF CACHE BOOL "Disable libssh2 support" FORCE)
set(CURL_USE_LIBSSH OFF CACHE BOOL "Disable libssh support" FORCE)
# Provide zlib include/library locations (use vendored zlib)
set(ZLIB_INCLUDE_DIR ${ZLIB_SRC_DIR} CACHE PATH "Zlib include dir" FORCE)
set(ZLIB_LIBRARY ${ZLIB_STATIC_LIBRARY} CACHE FILEPATH "Zlib library" FORCE)

# Help curl find our built brotli/zstd outputs
set(CMAKE_INCLUDE_PATH "${CMAKE_SOURCE_DIR}/third_party/zstd-dev/lib;${CMAKE_SOURCE_DIR}/third_party/brotli/c/include;${ZLIB_SRC_DIR}" CACHE STRING "Additional include paths for curl" FORCE)
set(CMAKE_LIBRARY_PATH "${ZSTD_BUILD_DIR}/lib;${BROTLI_BUILD_DIR}/lib;${ZLIB_OUTPUT_DIR}" CACHE STRING "Additional library paths for curl" FORCE)

# Add curl as a subdirectory so it is built as part of the main build
# Limit protocols/features to only what's needed by a download manager
# Prefer HTTP/HTTPS only. This overrides individual CURL_DISABLE_* options.
set(HTTP_ONLY ON CACHE BOOL "Disable all protocols except HTTP (This overrides all CURL_DISABLE_* options)" FORCE)
set(CURL_DISABLE_ALTSVC ON CACHE BOOL "Disable alt-svc support" FORCE)
set(CURL_DISABLE_LIBCURL_OPTION ON CACHE BOOL "Disable --libcurl option from curl tool" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "Disable libpsl" FORCE)
set(USE_LIBIDN2 OFF CACHE BOOL "Disable libidn2" FORCE)

add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/curl-8.19.0 ${CURL_BUILD_DIR})

# Ensure the produced libcurl target (libcurl_static) builds after external libs
add_dependencies(libcurl_static libzstd_static brotlidec_static brotlienc_static brotlicommon_static zlib_static)

# Tune libcurl_static to reduce size: enable sectioning and garbage-collect unused
if(TARGET libcurl_static)
    if(MSVC)
        target_compile_options(libcurl_static PRIVATE $<$<CONFIG:Release>:/Gy>)
        # final exe already uses /OPT:REF /OPT:ICF
    else()
        target_compile_options(libcurl_static PRIVATE $<$<CONFIG:Release>:-ffunction-sections> $<$<CONFIG:Release>:-fdata-sections> $<$<CONFIG:Release>:-O3>)
        target_link_options(libcurl_static PRIVATE $<$<CONFIG:Release>:-Wl,--gc-sections> $<$<CONFIG:Release>:-s>)
    endif()
endif()

# Helper: enable sectioning/stripping for a target in Release builds
function(enable_dead_code_stripping target)
    if(MSVC)
        target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:/Gy>)
    else()
        target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:-ffunction-sections> $<$<CONFIG:Release>:-fdata-sections>)
    endif()
endfunction()

# ------------------------------------------------------------
# libui-ng (Native CMake Build - Fixed Linker Errors)
# ------------------------------------------------------------
set(LIBUI_SRC_DIR ${CMAKE_SOURCE_DIR}/third_party/libui-ng)

if(LUDO_USE_PREBUILT_LIBUI)
    if(NOT EXISTS "${LUDO_LIBUI_LIBRARY}")
        message(FATAL_ERROR "LUDO_USE_PREBUILT_LIBUI=ON but LUDO_LIBUI_LIBRARY does not exist: ${LUDO_LIBUI_LIBRARY}")
    endif()

    add_library(libui_static STATIC IMPORTED GLOBAL)
    set_target_properties(libui_static PROPERTIES
        IMPORTED_LOCATION ${LUDO_LIBUI_LIBRARY}
        INTERFACE_INCLUDE_DIRECTORIES ${LUDO_LIBUI_INCLUDE_DIR}
    )
else()
    # 1. Gather Source Files (Capturing both .c and .cpp)
    file(GLOB LIBUI_COMMON_SOURCES 
        "${LIBUI_SRC_DIR}/common/*.c"
        "${LIBUI_SRC_DIR}/common/*.cpp"
    )
    
    # Remove ONLY the specific conflicting legacy files
    list(REMOVE_ITEM LIBUI_COMMON_SOURCES 
        "${LIBUI_SRC_DIR}/common/OLD_table.c"
        "${LIBUI_SRC_DIR}/common/test.c"
    )
    
    if(WIN32)
        # On Windows, we need the .cpp files from the windows directory.
        # CRITICAL: We no longer filter out "main" because windows/main.cpp 
        # contains the core uiMain/uiQuit implementation.
        file(GLOB LIBUI_OS_SOURCES "${LIBUI_SRC_DIR}/windows/*.cpp")
    elseif(APPLE)
        file(GLOB LIBUI_OS_SOURCES "${LIBUI_SRC_DIR}/darwin/*.m")
    else()
        file(GLOB LIBUI_OS_SOURCES "${LIBUI_SRC_DIR}/unix/*.c")
    endif()

    # 2. Create the Static Target
    add_library(libui_static STATIC ${LIBUI_COMMON_SOURCES} ${LIBUI_OS_SOURCES})
    target_include_directories(libui_static PUBLIC ${LIBUI_SRC_DIR})
    
    # 3. Apply settings from libui's original meson.build
    target_compile_definitions(libui_static PRIVATE libui_EXPORTS _UI_STATIC)
    
    if(MSVC)
        target_compile_options(libui_static PRIVATE /wd4100 /bigobj)
    else()
        # Ensure we don't treat warnings as errors for external library code
        target_compile_options(libui_static PRIVATE -Wno-unused-parameter -Wno-switch -Wno-deprecated-declarations)
    endif()

    # 4. OS-Specific Linking (Propagated to Ludo)
    if(WIN32)
        target_link_libraries(libui_static INTERFACE
            user32 kernel32 gdi32 comctl32 uxtheme msimg32 winmm
            comdlg32 d2d1 dwrite ole32 oleaut32 oleacc usp10
            gdiplus windowscodecs
        )
    elseif(APPLE)
        target_link_libraries(libui_static INTERFACE "-framework Cocoa")
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
        target_include_directories(libui_static PRIVATE ${GTK3_INCLUDE_DIRS})
        target_link_libraries(libui_static INTERFACE ${GTK3_LIBRARIES} m dl)
    endif()
endif()

# ------------------------------------------------------------
# LUDO application sources
# ------------------------------------------------------------
set(LUDO_SOURCES
    src/main.c
    src/config.c
    src/gui.c
    src/win_toolbar_icons.cpp
    src/lua_engine.c
    src/http_module.c
    src/ludo_module.c
    src/download_manager.c
    src/thread_queue.c
    third_party/libuilua/libuilua.c
)

configure_file(${CMAKE_SOURCE_DIR}/src/version.h.in ${CMAKE_BINARY_DIR}/generated/version.h @ONLY)

if(WIN32)
    configure_file(${CMAKE_SOURCE_DIR}/src/ludo.rc.in ${CMAKE_BINARY_DIR}/ludo.rc @ONLY)
    list(APPEND LUDO_SOURCES ${CMAKE_BINARY_DIR}/ludo.rc)
    add_executable(ludo WIN32 ${LUDO_SOURCES})
else()
    add_executable(ludo ${LUDO_SOURCES})
endif()

    # Apply dead-code stripping flags to main executable and in-repo static libs
    if(TARGET ludo)
        if(MSVC)
            target_compile_options(ludo PRIVATE $<$<CONFIG:Release>:/Gy>)
        else()
            target_compile_options(ludo PRIVATE $<$<CONFIG:Release>:-ffunction-sections> $<$<CONFIG:Release>:-fdata-sections>)
            target_link_options(ludo PRIVATE $<$<CONFIG:Release>:-Wl,--gc-sections> $<$<CONFIG:Release>:-s>)
        endif()
    endif()

    # Enable sectioning on internal static libs to let linkers drop unused code
    if(TARGET zlib_static)
        enable_dead_code_stripping(zlib_static)
    endif()
    if(TARGET lua)
        enable_dead_code_stripping(lua)
    endif()
    if(TARGET libui_static)
        enable_dead_code_stripping(libui_static)
    endif()
set(LUDO_EXTERNAL_DEPENDENCIES libzstd_static brotlidec_static brotlienc_static brotlicommon_static zlib_static)
add_dependencies(ludo ${LUDO_EXTERNAL_DEPENDENCIES})

target_include_directories(ludo PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/libuilua
    "$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>"
)

target_link_libraries(ludo PRIVATE
    libui_static
    libcurl_static
    zstd_static
    brotlidec_static
    brotlienc_static
    brotlicommon_static
    zlib_static
    lua
    Threads::Threads 
)

if(MSVC)
    target_compile_options(ludo PRIVATE $<$<CONFIG:Release>:/O2>)
    target_link_options(ludo PRIVATE $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
else()
    target_compile_options(ludo PRIVATE $<$<CONFIG:Release>:-O3>)
    target_link_options(ludo PRIVATE $<$<CONFIG:Release>:-s>)
endif()

target_compile_definitions(ludo PRIVATE
    CURL_STATICLIB
    $<$<CONFIG:Debug>:DEBUG>
)

if(WIN32)
    target_link_libraries(ludo PRIVATE
        user32 kernel32 gdi32 comctl32 uxtheme msimg32 winmm
        comdlg32 d2d1 dwrite ole32 oleaut32 oleacc usp10
        gdiplus ws2_32 wldap32 crypt32 normaliz secur32 bcrypt iphlpapi
    )
    if(MINGW)
        set_property(TARGET ludo PROPERTY LINKER_LANGUAGE CXX)
        target_link_options(ludo PRIVATE -static -static-libstdc++ -static-libgcc)
    endif()
elseif(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    target_include_directories(ludo PRIVATE ${GTK3_INCLUDE_DIRS})
    target_link_libraries(ludo PRIVATE ${GTK3_LIBRARIES} m dl) 
elseif(APPLE)
    target_link_libraries(ludo PRIVATE
        "-framework Cocoa"
        "-framework CoreFoundation"
    )
endif()

# Output Copy Commands
# Executes all copy and directory creation tasks in a single post-build step.
add_custom_command(TARGET ludo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/plugins $<TARGET_FILE_DIR:ludo>/plugins
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/snippets $<TARGET_FILE_DIR:ludo>/snippets
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/res $<TARGET_FILE_DIR:ludo>/res
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/config.ini $<TARGET_FILE_DIR:ludo>/config.ini
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:ludo>/downloads
    COMMENT "Copying runtime assets (plugins, snippets, res, config) and initializing output directories..."
)

install(TARGETS ludo DESTINATION bin)
install(FILES config.ini DESTINATION bin)
install(DIRECTORY plugins/ DESTINATION bin/plugins)
install(DIRECTORY snippets/ DESTINATION bin/snippets)
install(DIRECTORY res/ DESTINATION bin/res)