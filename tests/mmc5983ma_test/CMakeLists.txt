# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mmc5983ma_test)

# Include driver headers
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../magsens/src
)

# Test sources
target_sources(app PRIVATE
    src/test_mmc5983ma.c
)

# Driver source (the testable functions)
target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../magsens/src/mmc5983ma.c
)
