#
# Copyright (c) 2020 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(nrf_connect_sdk_intermediate)

# NORDIC SDK APP START
target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE src/json_payload/json_payload.c)
target_sources(app PRIVATE src/mqtt/mqtt.c)
# target_sources(app PRIVATE src/memfault/memfault_integration.c)
# NORDIC SDK APP END

zephyr_include_directories(src)
zephyr_include_directories(src/json_payload)
zephyr_include_directories(src/mqtt)
zephyr_include_directories(src/memfault)
zephyr_include_directories(config)

# Make folder containing certificates global so that it can be located by the MQTT helper library.
zephyr_include_directories_ifdef(CONFIG_MQTT_HELPER_PROVISION_CERTIFICATES certs)
# Make folder containing private key global so that it can be located by the MCUboot.
zephyr_include_directories_ifdef(CONFIG_BOOT_SIGNATURE_KEY_FILE keys)
