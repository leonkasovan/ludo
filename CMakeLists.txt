cmake_minimum_required(VERSION 3.16)

if(WIN32)
    project(ludo C CXX RC)
else()
    project(ludo C)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(WIN32)
    set(CMAKE_CXX_STANDARD 11)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

# -------------------------
# Versioning (auto-increment build)
# -------------------------
set(LUDO_VERSION_MAJOR 1)
set(LUDO_VERSION_MINOR 0)
set(LUDO_VERSION_PATCH 0)

set(LUDO_VERSION_FILE "${CMAKE_BINARY_DIR}/build_version.txt")
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

# Enable sanitizers for Debug builds on Linux
if(UNIX AND NOT APPLE AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Enabling AddressSanitizer, LeakSanitizer, and UndefinedBehaviorSanitizer for Debug build on Linux")
    set(SANITIZER_FLAGS "-fsanitize=address -fsanitize=leak -fsanitize=undefined -fno-omit-frame-pointer")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} ${SANITIZER_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} ${SANITIZER_FLAGS}")
endif()

option(BUILD_GUI "Build the GUI executable (ludo)" ON)

# ------------------------------------------------------------
# System Dependencies
# ------------------------------------------------------------
find_package(Threads REQUIRED)
# ------------------------------------------------------------
# Build options
# ------------------------------------------------------------

set(LUDO_LIB_OUTPUT_DIR ${CMAKE_BINARY_DIR}/libs)

function(set_static_archive_output target)
    if(CMAKE_CONFIGURATION_TYPES)
        # Keep config-specific static libraries separate when using multi-config generators.
        set_target_properties(${target} PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY "${LUDO_LIB_OUTPUT_DIR}/$<CONFIG>"
        )
    else()
        set_target_properties(${target} PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY ${LUDO_LIB_OUTPUT_DIR}
        )
    endif()
endfunction()

# ------------------------------------------------------------
# 1) zlib 1.2.8 (built from vendored source, static)
# ------------------------------------------------------------
set(ZLIB_SRC_DIR ${CMAKE_SOURCE_DIR}/third_party/zlib-1.2.8)

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
set_static_archive_output(zlib_static)
set_target_properties(zlib_static PROPERTIES
    OUTPUT_NAME zlibstatic
)

if(MSVC)
    set(ZLIB_STATIC_LIBRARY ${LUDO_LIB_OUTPUT_DIR}/zlibstatic.lib)
else()
    set(ZLIB_STATIC_LIBRARY ${LUDO_LIB_OUTPUT_DIR}/libzlibstatic.a)
endif()

# ------------------------------------------------------------
# lua-5.2.4 (source list)
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

# ------------------------------------------------------------
# 2) brotli (built from source, static)
# ------------------------------------------------------------
set(BROTLI_SRC_DIR ${CMAKE_SOURCE_DIR}/third_party/brotli)
file(GLOB BROTLI_COMMON_SOURCES "${BROTLI_SRC_DIR}/c/common/*.c")
file(GLOB BROTLI_DEC_SOURCES "${BROTLI_SRC_DIR}/c/dec/*.c")
file(GLOB BROTLI_ENC_SOURCES "${BROTLI_SRC_DIR}/c/enc/*.c")

add_library(brotlicommon_static STATIC ${BROTLI_COMMON_SOURCES})
add_library(brotlidec_static STATIC ${BROTLI_DEC_SOURCES})
add_library(brotlienc_static STATIC ${BROTLI_ENC_SOURCES})

target_include_directories(brotlicommon_static PUBLIC ${BROTLI_SRC_DIR}/c/include)
target_include_directories(brotlidec_static PUBLIC ${BROTLI_SRC_DIR}/c/include)
target_include_directories(brotlienc_static PUBLIC ${BROTLI_SRC_DIR}/c/include)
target_link_libraries(brotlidec_static PUBLIC brotlicommon_static)
target_link_libraries(brotlienc_static PUBLIC brotlicommon_static)

set_static_archive_output(brotlicommon_static)
set_static_archive_output(brotlidec_static)
set_static_archive_output(brotlienc_static)

# ------------------------------------------------------------
# 3) zstd-dev (built from source, static)
# ------------------------------------------------------------
set(ZSTD_SRC_DIR ${CMAKE_SOURCE_DIR}/third_party/zstd-dev)
file(GLOB ZSTD_COMMON_SOURCES "${ZSTD_SRC_DIR}/lib/common/*.c")
file(GLOB ZSTD_COMPRESS_SOURCES "${ZSTD_SRC_DIR}/lib/compress/*.c")
file(GLOB ZSTD_DECOMPRESS_SOURCES "${ZSTD_SRC_DIR}/lib/decompress/*.c")
file(GLOB ZSTD_DICT_SOURCES "${ZSTD_SRC_DIR}/lib/dictBuilder/*.c")
file(GLOB ZSTD_DEPRECATED_SOURCES "${ZSTD_SRC_DIR}/lib/deprecated/*.c")
file(GLOB ZSTD_LEGACY_SOURCES "${ZSTD_SRC_DIR}/lib/legacy/*.c")

add_library(zstd_static STATIC
    ${ZSTD_COMMON_SOURCES}
    ${ZSTD_COMPRESS_SOURCES}
    ${ZSTD_DECOMPRESS_SOURCES}
    ${ZSTD_DICT_SOURCES}
    ${ZSTD_DEPRECATED_SOURCES}
    ${ZSTD_LEGACY_SOURCES}
)
target_include_directories(zstd_static PUBLIC ${ZSTD_SRC_DIR}/lib ${ZSTD_SRC_DIR}/lib/common)
target_compile_definitions(zstd_static PRIVATE ZSTD_DISABLE_ASM)
set_static_archive_output(zstd_static)
set_target_properties(zstd_static PROPERTIES OUTPUT_NAME zstd)

# ------------------------------------------------------------
# 4) curl-8.19.0 (built from source, static)
# ------------------------------------------------------------
set(CURL_SRC_DIR ${CMAKE_SOURCE_DIR}/third_party/curl-8.19.0)
file(GLOB_RECURSE CURL_SOURCES "${CURL_SRC_DIR}/lib/*.c")

if(WIN32)
    add_library(libcurl_static STATIC ${CURL_SOURCES})
    target_include_directories(libcurl_static
        PUBLIC
            ${CURL_SRC_DIR}/include
        PRIVATE
            ${CURL_SRC_DIR}/lib
            ${BROTLI_SRC_DIR}/c/include
            ${ZSTD_SRC_DIR}/lib
            ${ZLIB_SRC_DIR}
    )

    target_compile_definitions(libcurl_static PRIVATE
        BUILDING_LIBCURL
        CURL_STATICLIB
        HAVE_LIBZ
        HAVE_BROTLI
        HAVE_ZSTD
        CURL_DISABLE_ALTSVC
        CURL_DISABLE_DICT
        CURL_DISABLE_FILE
        CURL_DISABLE_FTP
        CURL_DISABLE_GOPHER
        CURL_DISABLE_IMAP
        CURL_DISABLE_IPFS
        CURL_DISABLE_LDAP
        CURL_DISABLE_LDAPS
        CURL_DISABLE_MQTT
        CURL_DISABLE_NEGOTIATE_AUTH
        CURL_DISABLE_POP3
        CURL_DISABLE_RTSP
        CURL_DISABLE_SMB
        CURL_DISABLE_SMTP
        CURL_DISABLE_TELNET
        CURL_DISABLE_TFTP
        USE_THREADS_WIN32
        USE_SCHANNEL
        USE_WINDOWS_SSPI
    )

    target_link_libraries(libcurl_static PUBLIC
        zlib_static
        brotlidec_static
        brotlienc_static
        brotlicommon_static
        zstd_static
    )

    set_static_archive_output(libcurl_static)
    set_target_properties(libcurl_static PROPERTIES OUTPUT_NAME curl)

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
else()
    find_package(CURL REQUIRED)
    add_library(libcurl_static INTERFACE)
    target_link_libraries(libcurl_static INTERFACE CURL::libcurl)
endif()

# Helper: enable sectioning/stripping for a target in Release builds
function(enable_dead_code_stripping target)
    if(MSVC)
        target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:/Gy>)
    else()
        target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:-ffunction-sections> $<$<CONFIG:Release>:-fdata-sections>)
    endif()
endfunction()

if(BUILD_GUI)
# ------------------------------------------------------------
# 5) libui-ng (built from source, static)
# ------------------------------------------------------------
set(LIBUI_SRC_DIR ${CMAKE_SOURCE_DIR}/third_party/libui-ng)

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
    list(REMOVE_ITEM LIBUI_OS_SOURCES "${LIBUI_SRC_DIR}/unix/OLD_table.c")
endif()

# 2. Create the Static Target
add_library(libui_static STATIC ${LIBUI_COMMON_SOURCES} ${LIBUI_OS_SOURCES})
target_include_directories(libui_static PUBLIC ${LIBUI_SRC_DIR})
set_static_archive_output(libui_static)
set_target_properties(libui_static PROPERTIES OUTPUT_NAME libui)

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

if(BUILD_GUI)
# ------------------------------------------------------------
# 6) libuilua (built from source, static)
# ------------------------------------------------------------
set(LIBUILUA_SRC_DIR ${CMAKE_SOURCE_DIR}/third_party/libuilua)
add_library(libuilua_static STATIC ${LIBUILUA_SRC_DIR}/libuilua.c)
target_include_directories(libuilua_static PUBLIC ${LIBUILUA_SRC_DIR} ${LUA_SRC_DIR} ${LIBUI_SRC_DIR})
target_link_libraries(libuilua_static PUBLIC libui_static)
set_static_archive_output(libuilua_static)
set_target_properties(libuilua_static PROPERTIES OUTPUT_NAME libuilua)
endif()

# ------------------------------------------------------------
# 7) lua-5.2.4 (built from source, static)
# ------------------------------------------------------------
add_library(lua_static STATIC ${LUA_SOURCES})
target_include_directories(lua_static PUBLIC ${LUA_SRC_DIR})
set_static_archive_output(lua_static)
set_target_properties(lua_static PROPERTIES OUTPUT_NAME lua)
# Lua 5.2.4 hash function uses intentional signed integer wrapping (benign UBSan false-positive)
if(UNIX AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(lua_static PRIVATE -fno-sanitize=signed-integer-overflow)
endif()

if(WIN32)
    target_compile_definitions(lua_static PRIVATE LUA_USE_WINDOWS)
elseif(UNIX AND NOT APPLE)
    target_compile_definitions(lua_static PRIVATE LUA_USE_LINUX)
    target_link_libraries(lua_static PUBLIC m dl)
elseif(APPLE)
    target_compile_definitions(lua_static PRIVATE LUA_USE_MACOSX)
    target_link_libraries(lua_static PUBLIC m dl)
endif()

if(BUILD_GUI)
target_link_libraries(libuilua_static PUBLIC lua_static)
endif()

configure_file(${CMAKE_SOURCE_DIR}/src/version.h.in ${CMAKE_BINARY_DIR}/generated/version.h @ONLY)

if(TARGET zlib_static)
    enable_dead_code_stripping(zlib_static)
endif()
if(TARGET lua_static)
    enable_dead_code_stripping(lua_static)
endif()

if(BUILD_GUI)
# ------------------------------------------------------------
# LUDO executable (GUI)
# ------------------------------------------------------------
set(LUDO_SOURCES
    src/main.c
    src/config.c
    src/platform_utils.c
    src/gui.c
    src/lua_engine.c
    src/http_module.c
    src/ludo_module.c
    src/zip_module.c
    src/aes128.c
    src/download_manager.c
    src/thread_queue.c
    src/ipc/ipc_abstraction.c
    src/ipc/ludo_native_messaging.c
    third_party/cJSON/cJSON.c
)

if(WIN32)
    list(APPEND LUDO_SOURCES src/win_toolbar_icons.cpp)
endif()

if(WIN32)
    configure_file(${CMAKE_SOURCE_DIR}/src/ludo.rc.in ${CMAKE_BINARY_DIR}/ludo.rc @ONLY)
    list(APPEND LUDO_SOURCES ${CMAKE_BINARY_DIR}/ludo.rc)
    add_executable(ludo WIN32 ${LUDO_SOURCES})
else()
    add_executable(ludo ${LUDO_SOURCES})
endif()

    if(TARGET ludo)
        if(MSVC)
            target_compile_options(ludo PRIVATE $<$<CONFIG:Release>:/Gy>)
        else()
            target_compile_options(ludo PRIVATE $<$<CONFIG:Release>:-ffunction-sections> $<$<CONFIG:Release>:-fdata-sections>)
            target_link_options(ludo PRIVATE $<$<CONFIG:Release>:-Wl,--gc-sections> $<$<CONFIG:Release>:-s>)
        endif()
    endif()

    if(TARGET libui_static)
        enable_dead_code_stripping(libui_static)
    endif()
    if(TARGET libuilua_static)
        enable_dead_code_stripping(libuilua_static)
    endif()

set(LUDO_EXTERNAL_DEPENDENCIES libcurl_static libui_static libuilua_static lua_static zlib_static)
if(WIN32)
    list(APPEND LUDO_EXTERNAL_DEPENDENCIES zstd_static brotlidec_static brotlienc_static brotlicommon_static)
endif()
add_dependencies(ludo ${LUDO_EXTERNAL_DEPENDENCIES})

target_include_directories(ludo PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/libuilua
    "$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>"
)

target_link_libraries(ludo PRIVATE
    libui_static
    libcurl_static
    libuilua_static
    lua_static
    Threads::Threads
)

if(WIN32)
    target_link_libraries(ludo PRIVATE
        zstd_static
        brotlidec_static
        brotlienc_static
        brotlicommon_static
    )
endif()

target_link_libraries(ludo PRIVATE zlib_static)

if(MSVC)
    target_compile_options(ludo PRIVATE $<$<CONFIG:Release>:/O2>)
    target_link_options(ludo PRIVATE $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
else()
    target_compile_options(ludo PRIVATE $<$<CONFIG:Release>:-O3>)
    target_link_options(ludo PRIVATE $<$<CONFIG:Release>:-s>)
endif()

target_compile_definitions(ludo PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
)

target_compile_definitions(ludo PRIVATE CURL_STATICLIB)

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

# Set Debug output executable name to ludo-debug for Debug builds
if(CMAKE_CONFIGURATION_TYPES)
    set_target_properties(ludo PROPERTIES OUTPUT_NAME_DEBUG ludo-debug)
else()
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set_target_properties(ludo PROPERTIES OUTPUT_NAME ludo-debug)
    endif()
endif()

# Output Copy Commands
add_custom_command(TARGET ludo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/plugins $<TARGET_FILE_DIR:ludo>/plugins
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/snippets $<TARGET_FILE_DIR:ludo>/snippets
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/res $<TARGET_FILE_DIR:ludo>/res
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/tools $<TARGET_FILE_DIR:ludo>/tools
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/lualib $<TARGET_FILE_DIR:ludo>/lualib
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/config.ini $<TARGET_FILE_DIR:ludo>/config.ini
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/ludo_scripting.htm $<TARGET_FILE_DIR:ludo>/ludo_scripting.htm
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:ludo>/downloads
    COMMENT "Copying runtime assets (plugins, snippets, res, tools, lualib, config) and initializing output directories..."
)

install(TARGETS ludo DESTINATION bin)
install(FILES config.ini DESTINATION bin)
install(FILES ludo_scripting.htm DESTINATION bin)
install(DIRECTORY plugins/ DESTINATION bin/plugins)
install(DIRECTORY snippets/ DESTINATION bin/snippets)
install(DIRECTORY res/ DESTINATION bin/res)
install(DIRECTORY tools/ DESTINATION bin/tools)
install(DIRECTORY lualib/ DESTINATION bin/lualib)
endif()

# ------------------------------------------------------------
# ludocon — Pure console target (no GUI dependency)
# ------------------------------------------------------------
set(LUDOCON_SOURCES
    src/main.c
    src/config.c
    src/platform_utils.c
    src/console_log.c
    src/lua_engine.c
    src/http_module.c
    src/ludo_module.c
    src/zip_module.c
    src/aes128.c
    src/download_manager.c
    src/thread_queue.c
    third_party/cJSON/cJSON.c
)

add_executable(ludocon ${LUDOCON_SOURCES})

target_include_directories(ludocon PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    "$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>"
)

target_link_libraries(ludocon PRIVATE
    libcurl_static
    lua_static
    Threads::Threads
)

if(WIN32)
    target_link_libraries(ludocon PRIVATE
        zstd_static
        brotlidec_static
        brotlienc_static
        brotlicommon_static
    )
endif()

target_link_libraries(ludocon PRIVATE zlib_static)

target_compile_definitions(ludocon PRIVATE BUILD_CONSOLE
    $<$<CONFIG:Debug>:DEBUG>
)

target_compile_definitions(ludocon PRIVATE CURL_STATICLIB)

if(WIN32)
    target_link_libraries(ludocon PRIVATE
        ws2_32 wldap32 crypt32 normaliz secur32 bcrypt iphlpapi
    )
    if(MINGW)
        set_property(TARGET ludocon PROPERTY LINKER_LANGUAGE C)
        target_link_options(ludocon PRIVATE -static -static-libstdc++ -static-libgcc)
    endif()
else()
    target_link_libraries(ludocon PRIVATE m dl)
endif()

if(MSVC)
    target_compile_options(ludocon PRIVATE $<$<CONFIG:Release>:/O2>)
    target_link_options(ludocon PRIVATE $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
else()
    target_compile_options(ludocon PRIVATE $<$<CONFIG:Release>:-O3>)
    target_link_options(ludocon PRIVATE $<$<CONFIG:Release>:-s>)
endif()

# Debug output name
if(CMAKE_CONFIGURATION_TYPES)
    set_target_properties(ludocon PROPERTIES OUTPUT_NAME_DEBUG ludocon-debug)
else()
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set_target_properties(ludocon PROPERTIES OUTPUT_NAME ludocon-debug)
    endif()
endif()

# Copy runtime assets for ludocon too
add_custom_command(TARGET ludocon POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/plugins $<TARGET_FILE_DIR:ludocon>/plugins
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/snippets $<TARGET_FILE_DIR:ludocon>/snippets
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/lualib $<TARGET_FILE_DIR:ludocon>/lualib
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/config.ini $<TARGET_FILE_DIR:ludocon>/config.ini
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:ludocon>/downloads
    COMMENT "Copying runtime assets for ludocon..."
)

set(WENATIVEHOST_SOURCES
    src/ipc/wenativehost.c
    src/ipc/ipc_abstraction.c
    third_party/cJSON/cJSON.c
)

add_executable(wenativehost ${WENATIVEHOST_SOURCES})
target_include_directories(wenativehost PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(wenativehost PRIVATE Threads::Threads)
if(WIN32)
    target_link_libraries(wenativehost PRIVATE user32)
endif()
