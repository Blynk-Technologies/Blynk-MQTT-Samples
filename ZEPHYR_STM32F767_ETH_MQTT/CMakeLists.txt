# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(blynk_mqtt_ota)

# Add your include directory for headers
zephyr_include_directories(include)

# Add your app source files
target_sources(app PRIVATE
    src/main.c
    src/mqtt_client.c
    src/ota_manager.c
    src/utils.c
    src/network_manager.c
)

