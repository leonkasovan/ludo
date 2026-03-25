cmake_minimum_required(VERSION 3.16)
project(ludo C CXX RC)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

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
# Multi-config generators (Visual Studio, Ninja Multi-Config)
set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "Available build configurations" FORCE)

# Single-config generators (Ninja, Makefiles)
if(NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release")
endif()

# ------------------------------------------------------------
# Build options
# ------------------------------------------------------------
option(LUDO_STATIC_CURL "Link libcurl statically" ON)
option(LUDO_USE_PREBUILT_LIBUI "Use a prebuilt libui static library instead of building with Meson" OFF)
set(LUDO_LIBUI_LIBRARY "" CACHE FILEPATH "Path to prebuilt libui static library (e.g. C:/path/to/libui.a)")
set(LUDO_LIBUI_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/third_party/libui-ng" CACHE PATH "Path to libui headers (ui.h)")

# ------------------------------------------------------------
# Lua 5.2.4 (built from source)
# ------------------------------------------------------------
set(LUA_SRC_DIR ${CMAKE_SOURCE_DIR}/third_party/lua-5.2.4/src)

set(LUA_SOURCES
    ${LUA_SRC_DIR}/lapi.c
    ${LUA_SRC_DIR}/lauxlib.c
    ${LUA_SRC_DIR}/lbaselib.c
    ${LUA_SRC_DIR}/lbitlib.c
    ${LUA_SRC_DIR}/lcode.c
    ${LUA_SRC_DIR}/lcorolib.c
    ${LUA_SRC_DIR}/lctype.c
    ${LUA_SRC_DIR}/ldblib.c
    ${LUA_SRC_DIR}/ldebug.c
    ${LUA_SRC_DIR}/ldo.c
    ${LUA_SRC_DIR}/ldump.c
    ${LUA_SRC_DIR}/lfunc.c
    ${LUA_SRC_DIR}/lgc.c
    ${LUA_SRC_DIR}/linit.c
    ${LUA_SRC_DIR}/liolib.c
    ${LUA_SRC_DIR}/llex.c
    ${LUA_SRC_DIR}/lmathlib.c
    ${LUA_SRC_DIR}/lmem.c
    ${LUA_SRC_DIR}/loadlib.c
    ${LUA_SRC_DIR}/lobject.c
    ${LUA_SRC_DIR}/lopcodes.c
    ${LUA_SRC_DIR}/loslib.c
    ${LUA_SRC_DIR}/lparser.c
    ${LUA_SRC_DIR}/lstate.c
    ${LUA_SRC_DIR}/lstring.c
    ${LUA_SRC_DIR}/lstrlib.c
    ${LUA_SRC_DIR}/ltable.c
    ${LUA_SRC_DIR}/ltablib.c
    ${LUA_SRC_DIR}/ltm.c
    ${LUA_SRC_DIR}/lundump.c
    ${LUA_SRC_DIR}/lvm.c
    ${LUA_SRC_DIR}/lzio.c
)

add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua PUBLIC ${LUA_SRC_DIR})

if(WIN32)
    # Disable the lua_readline dependency on Windows
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

if(MINGW)
    set(BROTLI_LIB_NAME libbrotlidec-static.a)
elseif(WIN32)
    set(BROTLI_LIB_NAME brotlidec-static.lib)
else()
    set(BROTLI_LIB_NAME libbrotlidec-static.a)
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
        ${BROTLI_INSTALL_DIR}/lib/libbrotlidec-static.a
        ${BROTLI_INSTALL_DIR}/lib/libbrotlienc-static.a
        ${BROTLI_INSTALL_DIR}/lib/libbrotlicommon-static.a
)

# Import all brotli static libraries
add_library(brotlidec_static STATIC IMPORTED GLOBAL)
add_library(brotlienc_static STATIC IMPORTED GLOBAL)
add_library(brotlicommon_static STATIC IMPORTED GLOBAL)
add_dependencies(brotlidec_static brotli_ext)
add_dependencies(brotlienc_static brotli_ext)
add_dependencies(brotlicommon_static brotli_ext)
set_target_properties(brotlidec_static PROPERTIES IMPORTED_LOCATION ${BROTLI_INSTALL_DIR}/lib/libbrotlidec-static.a)
set_target_properties(brotlienc_static PROPERTIES IMPORTED_LOCATION ${BROTLI_INSTALL_DIR}/lib/libbrotlienc-static.a)
set_target_properties(brotlicommon_static PROPERTIES IMPORTED_LOCATION ${BROTLI_INSTALL_DIR}/lib/libbrotlicommon-static.a)

set(ZSTD_BUILD_DIR ${CMAKE_BINARY_DIR}/zstd_build)
set(ZSTD_INSTALL_DIR ${CMAKE_BINARY_DIR}/zstd_install)

if(MINGW)
    set(ZSTD_LIB_NAME libzstd.a)
elseif(WIN32)
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
        -DZSTD_BUILD_PROGRAMS=OFF
        -DZSTD_BUILD_TESTS=OFF
        -DZSTD_BUILD_SHARED=OFF
        -DZSTD_BUILD_STATIC=ON
    INSTALL_COMMAND ${CMAKE_COMMAND} --install ${ZSTD_BUILD_DIR}
    BUILD_BYPRODUCTS ${ZSTD_INSTALL_DIR}/lib/${ZSTD_LIB_NAME}
)

add_library(zstd_static STATIC IMPORTED GLOBAL)
add_dependencies(zstd_static zstd_ext)

set_target_properties(zstd_static PROPERTIES
    IMPORTED_LOCATION ${ZSTD_INSTALL_DIR}/lib/${ZSTD_LIB_NAME}
)

set(CURL_BUILD_DIR ${CMAKE_BINARY_DIR}/curl_build)
set(CURL_INSTALL_DIR ${CMAKE_BINARY_DIR}/curl_install)

if(MINGW)
    # MinGW static libcurl build for a self-contained app directory.
    set(CURL_BUILD_SHARED OFF)
    set(CURL_LIB_NAME libcurl.a)
elseif(WIN32)
    set(CURL_BUILD_SHARED OFF)
    set(CURL_LIB_NAME libcurl.lib)
else()
    set(CURL_BUILD_SHARED OFF)
    set(CURL_LIB_NAME libcurl.a)
endif()

# Configure and build curl as an external project
ExternalProject_Add(curl_ext
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/third_party/curl-8.19.0
    BINARY_DIR ${CURL_BUILD_DIR}
    DEPENDS zstd_ext brotli_ext
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX=${CURL_INSTALL_DIR}
        -DBUILD_SHARED_LIBS=${CURL_BUILD_SHARED}
        -DBUILD_STATIC_LIBS=ON
        -DBUILD_CURL_EXE=OFF
        -DBUILD_TESTING=OFF
        -DCURL_USE_OPENSSL=OFF
        -DCURL_USE_SCHANNEL=ON      # Windows native TLS
        -DUSE_WIN32_LDAP=OFF
        -DCURL_DISABLE_LDAP=ON
        -DCURL_DISABLE_LDAPS=ON
        -DUSE_NGHTTP2=OFF
        -DCURL_USE_LIBSSH2=OFF
        -DCURL_USE_LIBSSH=OFF
        -DCURL_USE_LIBPSL=OFF
        -DUSE_LIBIDN2=OFF
        -DCURL_BROTLI=ON
        -DCURL_ZSTD=ON
        -DCURL_ZLIB=ON
        -DCURL_USE_PKGCONFIG=OFF
        -DCMAKE_INCLUDE_PATH=${ZSTD_INSTALL_DIR}/include;${BROTLI_INSTALL_DIR}/include
        -DCMAKE_LIBRARY_PATH=${ZSTD_INSTALL_DIR}/lib;${BROTLI_INSTALL_DIR}/lib
        -DCURL_ENABLE_SSL=ON
    INSTALL_COMMAND ${CMAKE_COMMAND} --install ${CURL_BUILD_DIR}
    BUILD_BYPRODUCTS ${CURL_INSTALL_DIR}/lib/${CURL_LIB_NAME}
)

add_library(libcurl_static STATIC IMPORTED GLOBAL)
add_dependencies(libcurl_static curl_ext)

set_target_properties(libcurl_static PROPERTIES
    IMPORTED_LOCATION ${CURL_INSTALL_DIR}/lib/${CURL_LIB_NAME}
)


# ------------------------------------------------------------
# libui-ng (built via meson + ninja as external project)
# ------------------------------------------------------------
if(WIN32)
    set(LIBUI_LIB_NAME libui.a)
else()
    set(LIBUI_LIB_NAME libui.a)
endif()

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
    # Prefer the MinGW64 Meson/Ninja path on Windows builds; full absolute path
    # is required because cmake --build uses cmd.exe and UNC-style paths can fail.
    if(MINGW)
        set(MESON_EXECUTABLE "D:/msys64/mingw64/bin/meson")
        set(NINJA_EXECUTABLE "D:/msys64/mingw64/bin/ninja")
    endif()

    if(NOT MESON_EXECUTABLE OR NOT EXISTS "${MESON_EXECUTABLE}")
        find_program(MESON_EXECUTABLE NAMES meson
            HINTS D:/msys64/mingw64/bin /mingw64/bin /usr/bin
            NO_DEFAULT_PATH)
    endif()
    if(NOT MESON_EXECUTABLE)
        find_program(MESON_EXECUTABLE NAMES meson)
    endif()
    if(NOT MESON_EXECUTABLE)
        message(FATAL_ERROR
            "Meson not found. Either install Meson, or configure with:\n"
            "  -DLUDO_USE_PREBUILT_LIBUI=ON -DLUDO_LIBUI_LIBRARY=<path to libui.a>\n"
            "Optional: -DLUDO_LIBUI_INCLUDE_DIR=<path to headers containing ui.h>")
    endif()

    if(NOT NINJA_EXECUTABLE OR NOT EXISTS "${NINJA_EXECUTABLE}")
        find_program(NINJA_EXECUTABLE NAMES ninja
            PATHS /mingw64/bin /usr/bin
            NO_DEFAULT_PATH)
    endif()
    if(NOT NINJA_EXECUTABLE)
        find_program(NINJA_EXECUTABLE NAMES ninja)
    endif()
    if(NOT NINJA_EXECUTABLE)
        message(FATAL_ERROR "Ninja not found. Install ninja and retry.")
    endif()

    set(LIBUI_BUILD_DIR ${CMAKE_BINARY_DIR}/libui_build)
    set(LIBUI_INSTALL_DIR ${CMAKE_BINARY_DIR}/libui_install)

    ExternalProject_Add(libui_ext
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/third_party/libui-ng
        BINARY_DIR ${LIBUI_BUILD_DIR}
        CONFIGURE_COMMAND
            ${MESON_EXECUTABLE} setup
                --prefix=${LIBUI_INSTALL_DIR}
                --libdir=lib
                --default-library=static
                -Dbuildtype=$<IF:$<CONFIG:Debug>,debug,release>
                -Dtests=false
                -Dexamples=false
                ${LIBUI_BUILD_DIR}
                ${CMAKE_SOURCE_DIR}/third_party/libui-ng
        BUILD_COMMAND
            ${NINJA_EXECUTABLE} -C ${LIBUI_BUILD_DIR}
        INSTALL_COMMAND
            ${NINJA_EXECUTABLE} -C ${LIBUI_BUILD_DIR} install
        BUILD_BYPRODUCTS ${LIBUI_INSTALL_DIR}/lib/${LIBUI_LIB_NAME}
    )

    add_library(libui_static STATIC IMPORTED GLOBAL)
    add_dependencies(libui_static libui_ext)

    set_target_properties(libui_static PROPERTIES
        IMPORTED_LOCATION ${LIBUI_INSTALL_DIR}/lib/${LIBUI_LIB_NAME}
        INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_SOURCE_DIR}/third_party/libui-ng
    )
endif()

# ------------------------------------------------------------
# LUDO application sources
# ------------------------------------------------------------
set(LUDO_SOURCES
    src/main.c
    src/gui.c
    src/win_toolbar_icons.cpp
    src/lua_engine.c
    src/http_module.c
    src/ludo_module.c
    src/download_manager.c
    src/thread_queue.c
    third_party/libuilua/libuilua.c
    ${CMAKE_BINARY_DIR}/ludo.rc
)

# Generate version header and resource file
configure_file(${CMAKE_SOURCE_DIR}/src/version.h.in ${CMAKE_BINARY_DIR}/generated/version.h @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/src/ludo.rc.in ${CMAKE_BINARY_DIR}/ludo.rc @ONLY)

if(WIN32)
    # Windows application (no console window in release)
    add_executable(ludo WIN32 ${LUDO_SOURCES})
else()
    add_executable(ludo ${LUDO_SOURCES})
endif()

target_include_directories(ludo PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/libui-ng
    ${CMAKE_SOURCE_DIR}/third_party/libuilua
    ${LUA_SRC_DIR}
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
    lua
    z
)

# Release build tuning: optimize and strip symbols for a smaller binary.
if(MSVC)
    target_compile_options(ludo PRIVATE $<$<CONFIG:Release>:/O2>)
    target_link_options(ludo PRIVATE $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
else()
    target_compile_options(ludo PRIVATE $<$<CONFIG:Release>:-O3>)
    target_link_options(ludo PRIVATE $<$<CONFIG:Release>:-s>)
endif()

# Required when linking static libcurl.
target_compile_definitions(ludo PRIVATE CURL_STATICLIB)

if(WIN32)
    target_link_libraries(ludo PRIVATE
        # libui-ng Windows dependencies
        user32 kernel32 gdi32 comctl32 uxtheme msimg32
        comdlg32 d2d1 dwrite ole32 oleaut32 oleacc usp10
        gdiplus
        # libcurl Windows dependencies
        ws2_32 wldap32 crypt32 normaliz secur32 bcrypt iphlpapi
    )
    if(MINGW)
        # libui-ng contains C++ objects on Windows; use C++ linker for final link.
        set_property(TARGET ludo PROPERTY LINKER_LANGUAGE CXX)
        # Prefer static GCC/C++ runtimes so ludo.exe does not depend on
        # libstdc++-6.dll / libgcc_s_seh-1.dll beside the executable.
        target_link_options(ludo PRIVATE -static -static-libstdc++ -static-libgcc)
    endif()
elseif(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    target_include_directories(ludo PRIVATE ${GTK3_INCLUDE_DIRS})
    target_link_libraries(ludo PRIVATE ${GTK3_LIBRARIES} pthread m dl)
elseif(APPLE)
    target_link_libraries(ludo PRIVATE
        "-framework Cocoa"
        "-framework CoreFoundation"
        pthread
    )
endif()

# Pthreads on non-Windows platforms
if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(ludo PRIVATE Threads::Threads)
endif()

# Copy plugins directory to build output
add_custom_command(TARGET ludo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/plugins
        $<TARGET_FILE_DIR:ludo>/plugins
    COMMENT "Copying plugins directory..."
)

# Copy toolbar/icon resources to build output
add_custom_command(TARGET ludo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/res
        $<TARGET_FILE_DIR:ludo>/res
    COMMENT "Copying UI resources..."
)

# Copy default output directory
add_custom_command(TARGET ludo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:ludo>/downloads
    COMMENT "Creating downloads directory..."
)

# ------------------------------------------------------------
# Install rules
# ------------------------------------------------------------
install(TARGETS ludo DESTINATION bin)
install(DIRECTORY plugins/ DESTINATION bin/plugins)
