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

ExternalProject_Add(brotli_ext
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/third_party/brotli
    BINARY_DIR ${BROTLI_BUILD_DIR}
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX=${BROTLI_INSTALL_DIR}
        -DCMAKE_INSTALL_LIBDIR=lib 
        -DBROTLI_DISABLE_TESTS=ON
        -DBROTLI_BUNDLED_MODE=OFF
        -DBROTLI_BUILD_TOOLS=OFF
        -DBROTLI_BUILD_TESTS=OFF
        -DBROTLI_BUILD_SHARED_LIBS=OFF
        -DBROTLI_BUILD_STATIC_LIBS=ON
        -DBROTLI_BUILD_FOR_PACKAGE=ON
    BUILD_COMMAND ${CMAKE_COMMAND} --build ${BROTLI_BUILD_DIR} ${BROTLI_BUILD_CONFIG_ARG} --parallel
    INSTALL_COMMAND ${CMAKE_COMMAND} --install ${BROTLI_BUILD_DIR} ${BROTLI_BUILD_CONFIG_ARG}
    BUILD_BYPRODUCTS
        ${BROTLI_INSTALL_DIR}/lib/${BROTLI_LIB_NAME}
        ${BROTLI_INSTALL_DIR}/lib/${BROTLI_ENC_NAME}
        ${BROTLI_INSTALL_DIR}/lib/${BROTLI_COM_NAME}
)

add_library(brotlidec_static STATIC IMPORTED GLOBAL)
add_library(brotlienc_static STATIC IMPORTED GLOBAL)
add_library(brotlicommon_static STATIC IMPORTED GLOBAL)
set_target_properties(brotlidec_static PROPERTIES IMPORTED_LOCATION ${BROTLI_INSTALL_DIR}/lib/${BROTLI_LIB_NAME})
set_target_properties(brotlienc_static PROPERTIES IMPORTED_LOCATION ${BROTLI_INSTALL_DIR}/lib/${BROTLI_ENC_NAME})
set_target_properties(brotlicommon_static PROPERTIES IMPORTED_LOCATION ${BROTLI_INSTALL_DIR}/lib/${BROTLI_COM_NAME})

set(ZSTD_BUILD_DIR ${CMAKE_BINARY_DIR}/zstd_build)
set(ZSTD_INSTALL_DIR ${CMAKE_BINARY_DIR}/zstd_install)

if(MSVC)
    set(ZSTD_LIB_NAME zstd_static.lib)
else()
    set(ZSTD_LIB_NAME libzstd.a)
endif()

ExternalProject_Add(zstd_ext
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/third_party/zstd-dev
    BINARY_DIR ${ZSTD_BUILD_DIR}
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX=${ZSTD_INSTALL_DIR}
        -DCMAKE_INSTALL_LIBDIR=lib
        -DZSTD_BUILD_PROGRAMS=OFF
        -DZSTD_BUILD_TESTS=OFF
        -DZSTD_BUILD_SHARED=OFF
        -DZSTD_BUILD_STATIC=ON
    INSTALL_COMMAND ${CMAKE_COMMAND} --install ${ZSTD_BUILD_DIR}
    BUILD_BYPRODUCTS ${ZSTD_INSTALL_DIR}/lib/${ZSTD_LIB_NAME}
)

add_library(zstd_static STATIC IMPORTED GLOBAL)
set_target_properties(zstd_static PROPERTIES IMPORTED_LOCATION ${ZSTD_INSTALL_DIR}/lib/${ZSTD_LIB_NAME})

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

# Dynamic SSL selection based on target OS
if(WIN32)
    set(CURL_SSL_ARGS "-DCURL_USE_SCHANNEL=ON" "-DCURL_USE_OPENSSL=OFF")
elseif(APPLE)
    set(CURL_SSL_ARGS "-DCURL_USE_SECTRANSP=ON" "-DCURL_USE_OPENSSL=OFF")
else()
    set(CURL_SSL_ARGS "-DCURL_USE_OPENSSL=ON" "-DCURL_USE_SCHANNEL=OFF")
endif()

ExternalProject_Add(curl_ext
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/third_party/curl-8.19.0
    BINARY_DIR ${CURL_BUILD_DIR}
    DEPENDS zstd_ext brotli_ext zlib_static
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX=${CURL_INSTALL_DIR}
        -DCMAKE_INSTALL_LIBDIR=lib
        -DBUILD_SHARED_LIBS=OFF
        -DBUILD_STATIC_LIBS=ON
        -DBUILD_CURL_EXE=OFF
        -DBUILD_TESTING=OFF
        ${CURL_SSL_ARGS}
        # Disable Unused Protocols (Mail, Chat, Legacy)
        -DCURL_DISABLE_DICT=ON
        -DCURL_DISABLE_GOPHER=ON
        -DCURL_DISABLE_IMAP=ON
        -DCURL_DISABLE_POP3=ON
        -DCURL_DISABLE_SMTP=ON
        -DCURL_DISABLE_RTSP=ON
        -DCURL_DISABLE_SMB=ON
        -DCURL_DISABLE_TELNET=ON
        -DCURL_DISABLE_TFTP=ON
        -DCURL_DISABLE_MQTT=ON
        -DCURL_DISABLE_WEBSOCKETS=ON
        -DCURL_DISABLE_FILE=ON
        -DCURL_DISABLE_LDAP=ON
        -DCURL_DISABLE_LDAPS=ON
        # Disable Unused Features
        -DCURL_DISABLE_ALTSVC=ON
        -DCURL_USE_LIBPSL=OFF
        -DUSE_LIBIDN2=OFF
        -DUSE_NGHTTP2=OFF
        -DCURL_USE_LIBSSH2=OFF
        -DCURL_USE_LIBSSH=OFF
        # Performance and Core Features (Keep these)
        -DCURL_BROTLI=ON
        -DCURL_ZSTD=ON
        -DCURL_ZLIB=ON
        -DCURL_ENABLE_SSL=ON
        -DENABLE_THREADED_RESOLVER=ON
        -DZLIB_INCLUDE_DIR=${ZLIB_SRC_DIR}
        -DZLIB_LIBRARY=${ZLIB_STATIC_LIBRARY}
        -DCURL_USE_PKGCONFIG=OFF
        -DCMAKE_INCLUDE_PATH=${ZSTD_INSTALL_DIR}/include;${BROTLI_INSTALL_DIR}/include;${ZLIB_SRC_DIR}
        -DCMAKE_LIBRARY_PATH=${ZSTD_INSTALL_DIR}/lib;${BROTLI_INSTALL_DIR}/lib;${ZLIB_OUTPUT_DIR}
    INSTALL_COMMAND ${CMAKE_COMMAND} --install ${CURL_BUILD_DIR}
    BUILD_BYPRODUCTS ${CURL_INSTALL_DIR}/lib/${CURL_LIB_NAME}
)

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

# Provide zlib include/library locations (use vendored zlib)
set(ZLIB_INCLUDE_DIR ${ZLIB_SRC_DIR} CACHE PATH "Zlib include dir" FORCE)
set(ZLIB_LIBRARY ${ZLIB_STATIC_LIBRARY} CACHE FILEPATH "Zlib library" FORCE)

# Help curl find our built brotli/zstd outputs
set(CMAKE_INCLUDE_PATH "${ZSTD_INSTALL_DIR}/include;${BROTLI_INSTALL_DIR}/include;${ZLIB_SRC_DIR}" CACHE STRING "Additional include paths for curl" FORCE)
set(CMAKE_LIBRARY_PATH "${ZSTD_INSTALL_DIR}/lib;${BROTLI_INSTALL_DIR}/lib;${ZLIB_OUTPUT_DIR}" CACHE STRING "Additional library paths for curl" FORCE)

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
add_dependencies(libcurl_static zstd_ext brotli_ext zlib_static)

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
set(LUDO_EXTERNAL_DEPENDENCIES curl_ext zstd_ext brotli_ext zlib_static)
add_dependencies(ludo ${LUDO_EXTERNAL_DEPENDENCIES})

target_include_directories(ludo PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/libuilua
    "$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/curl_install/include>"
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
add_custom_command(TARGET ludo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/plugins $<TARGET_FILE_DIR:ludo>/plugins
    COMMENT "Copying plugins directory..."
)

add_custom_command(TARGET ludo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/snippets $<TARGET_FILE_DIR:ludo>/snippets
    COMMENT "Copying snippets directory..."
)

add_custom_command(TARGET ludo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/res $<TARGET_FILE_DIR:ludo>/res
    COMMENT "Copying UI resources..."
)

add_custom_command(TARGET ludo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/config.ini $<TARGET_FILE_DIR:ludo>/config.ini
    COMMENT "Copying config.ini..."
)

add_custom_command(TARGET ludo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:ludo>/downloads
    COMMENT "Creating downloads directory..."
)

install(TARGETS ludo DESTINATION bin)
install(FILES config.ini DESTINATION bin)
install(DIRECTORY plugins/ DESTINATION bin/plugins)
install(DIRECTORY snippets/ DESTINATION bin/snippets)
install(DIRECTORY res/ DESTINATION bin/res)