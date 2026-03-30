# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Read version from VERSION file and inject into Kconfig
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" ver_lines)
foreach(line ${ver_lines})
    if(line MATCHES "^VERSION_MAJOR = ([0-9]+)")
        set(VER_MAJOR ${CMAKE_MATCH_1})
    elseif(line MATCHES "^VERSION_MINOR = ([0-9]+)")
        set(VER_MINOR ${CMAKE_MATCH_1})
    elseif(line MATCHES "^PATCHLEVEL = ([0-9]+)")
        set(VER_PATCH ${CMAKE_MATCH_1})
    endif()
endforeach()

set(APP_VER "${VER_MAJOR}.${VER_MINOR}.${VER_PATCH}")
message(STATUS "App version: ${APP_VER}")

set(VERSION_CONF "${CMAKE_BINARY_DIR}/version.conf")
file(WRITE ${VERSION_CONF}
    "CONFIG_FIRMWARE_VERSION=\"${APP_VER}\"\n"
    "CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION=\"${APP_VER}+0\"\n"
)
list(APPEND OVERLAY_CONFIG "${VERSION_CONF}")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(blynk_https)

target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE src/cloud/cloud.c)
target_sources(app PRIVATE src/credentials/credentials.c)
