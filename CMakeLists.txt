cmake_minimum_required(VERSION 3.16)
project(ludo C CXX RC)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11) # Added: Required for win_toolbar_icons.cpp
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
set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "Available build configurations" FORCE)

if(NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release")
endif()

# ------------------------------------------------------------
# System Dependencies
# ------------------------------------------------------------
find_package(ZLIB REQUIRED) # Added: Clean cross-platform Zlib
find_package(Threads REQUIRED) # Moved up for global access

# ------------------------------------------------------------
# Build options
# ------------------------------------------------------------
option(LUDO_USE_PREBUILT_LIBUI "Use a prebuilt libui static library instead of building with Meson" OFF)
set(LUDO_LIBUI_LIBRARY "" CACHE FILEPATH "Path to prebuilt libui static library (e.g. C:/path/to/libui.a)")
set(LUDO_LIBUI_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/third_party/libui-ng" CACHE PATH "Path to libui headers (ui.h)")

# ------------------------------------------------------------
# Lua 5.2.4 (built from source)
# ------------------------------------------------------------
set(LUA_SRC_DIR ${CMAKE_SOURCE_DIR}/third_party/lua-5.2.4/src)

file(GLOB LUA_SOURCES "${LUA_SRC_DIR}/l*.c") # Slightly cleaner if you want to use glob, or keep your manual list.
# Reverting to your manual list for safety:
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

if(MINGW OR UNIX)
    set(BROTLI_LIB_NAME libbrotlidec-static.a)
    set(BROTLI_ENC_NAME libbrotlienc-static.a)
    set(BROTLI_COM_NAME libbrotlicommon-static.a)
else()
    set(BROTLI_LIB_NAME brotlidec-static.lib)
    set(BROTLI_ENC_NAME brotlienc-static.lib)
    set(BROTLI_COM_NAME brotlicommon-static.lib)
endif()

ExternalProject_Add(brotli_ext
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/third_party/brotli
    BINARY_DIR ${BROTLI_BUILD_DIR}
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX=${BROTLI_INSTALL_DIR}
        -DCMAKE_INSTALL_LIBDIR=lib # Added: Forces output to 'lib' instead of 'lib64'
        -DBROTLI_DISABLE_TESTS=ON
        -DBROTLI_BUNDLED_MODE=OFF
        -DBROTLI_BUILD_TOOLS=OFF
        -DBROTLI_BUILD_TESTS=OFF
        -DBROTLI_BUILD_SHARED_LIBS=OFF
        -DBROTLI_BUILD_STATIC_LIBS=ON
        -DBROTLI_BUILD_FOR_PACKAGE=ON
    INSTALL_COMMAND ${CMAKE_COMMAND} --install ${BROTLI_BUILD_DIR}
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

if(MINGW OR UNIX)
    set(ZSTD_LIB_NAME libzstd.a)
else()
    set(ZSTD_LIB_NAME zstd_static.lib)
endif()

ExternalProject_Add(zstd_ext
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/third_party/zstd-dev
    BINARY_DIR ${ZSTD_BUILD_DIR}
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX=${ZSTD_INSTALL_DIR}
        -DCMAKE_INSTALL_LIBDIR=lib # Added: Forces output to 'lib'
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

if(MINGW OR UNIX)
    set(CURL_LIB_NAME libcurl.a)
else()
    set(CURL_LIB_NAME libcurl.lib)
endif()

ExternalProject_Add(curl_ext
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/third_party/curl-8.19.0
    BINARY_DIR ${CURL_BUILD_DIR}
    DEPENDS zstd_ext brotli_ext
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX=${CURL_INSTALL_DIR}
        -DCMAKE_INSTALL_LIBDIR=lib # Added: Forces output to 'lib'
        -DBUILD_SHARED_LIBS=OFF
        -DBUILD_STATIC_LIBS=ON
        -DBUILD_CURL_EXE=OFF
        -DBUILD_TESTING=OFF
        -DCURL_USE_OPENSSL=OFF
        -DCURL_USE_SCHANNEL=ON
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
set_target_properties(libcurl_static PROPERTIES IMPORTED_LOCATION ${CURL_INSTALL_DIR}/lib/${CURL_LIB_NAME})

# ------------------------------------------------------------
# libui-ng
# ------------------------------------------------------------
set(LIBUI_LIB_NAME libui.a)

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
    find_program(MESON_EXECUTABLE meson)
    if(NOT MESON_EXECUTABLE)
        message(FATAL_ERROR "Meson not found.")
    endif()
    find_program(NINJA_EXECUTABLE ninja REQUIRED)

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
                ${LIBUI_BUILD_DIR}
                ${CMAKE_SOURCE_DIR}/third_party/libui-ng
        BUILD_COMMAND ${NINJA_EXECUTABLE} -C ${LIBUI_BUILD_DIR}
        INSTALL_COMMAND ${NINJA_EXECUTABLE} -C ${LIBUI_BUILD_DIR} install
        BUILD_BYPRODUCTS ${LIBUI_INSTALL_DIR}/lib/${LIBUI_LIB_NAME}
    )

    add_library(libui_static STATIC IMPORTED GLOBAL)
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
)

# Generate version header and resource file
configure_file(${CMAKE_SOURCE_DIR}/src/version.h.in ${CMAKE_BINARY_DIR}/generated/version.h @ONLY)

if(WIN32)
    # Fix: Only add RC files on Windows to prevent Linux/Mac build errors
    configure_file(${CMAKE_SOURCE_DIR}/src/ludo.rc.in ${CMAKE_BINARY_DIR}/ludo.rc @ONLY)
    list(APPEND LUDO_SOURCES ${CMAKE_BINARY_DIR}/ludo.rc)
    add_executable(ludo WIN32 ${LUDO_SOURCES})
else()
    add_executable(ludo ${LUDO_SOURCES})
endif()

# Fix: Force CMake to wait for external projects to finish before compiling/linking ludo
set(LUDO_EXTERNAL_DEPENDENCIES curl_ext zstd_ext brotli_ext)
if(NOT LUDO_USE_PREBUILT_LIBUI)
    list(APPEND LUDO_EXTERNAL_DEPENDENCIES libui_ext)
endif()
add_dependencies(ludo ${LUDO_EXTERNAL_DEPENDENCIES})

target_include_directories(ludo PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/libuilua
    "$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/curl_install/include>"
    "$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>"
    # Note: LUA and libui-ng includes are inherited automatically via target_link_libraries
)

target_link_libraries(ludo PRIVATE
    libui_static
    libcurl_static
    zstd_static
    brotlidec_static
    brotlienc_static
    brotlicommon_static
    lua
    ZLIB::ZLIB    # Fix: Use CMake's official ZLIB target
    Threads::Threads # Fix: Unified threading link for cross-platform
)

if(MSVC)
    target_compile_options(ludo PRIVATE $<$<CONFIG:Release>:/O2>)
    target_link_options(ludo PRIVATE $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
else()
    target_compile_options(ludo PRIVATE $<$<CONFIG:Release>:-O3>)
    target_link_options(ludo PRIVATE $<$<CONFIG:Release>:-s>)
endif()

target_compile_definitions(ludo PRIVATE CURL_STATICLIB)

if(WIN32)
    target_link_libraries(ludo PRIVATE
        user32 kernel32 gdi32 comctl32 uxtheme msimg32
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
    target_link_libraries(ludo PRIVATE ${GTK3_LIBRARIES} m dl) # Removed redundant 'pthread'
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
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/res $<TARGET_FILE_DIR:ludo>/res
    COMMENT "Copying UI resources..."
)

add_custom_command(TARGET ludo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:ludo>/downloads
    COMMENT "Creating downloads directory..."
)

install(TARGETS ludo DESTINATION bin)
install(DIRECTORY plugins/ DESTINATION bin/plugins)