# =============================================================================
# Unit Test CMakeLists.txt
# =============================================================================
# This builds a Ztest suite that can run on native_sim for CI
# =============================================================================

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(unit_test)

# Include the source directories from your main applications
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../misogate/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../misogate/src/json_payload
    ${CMAKE_CURRENT_SOURCE_DIR}/../../misogate/src/mqtt
)

# Test source files
target_sources(app PRIVATE
    src/main.c
    src/test_json_payload.c
)

# Include the actual source files being tested (if they don't have HW dependencies)
# Uncomment and adjust as needed:
# target_sources(app PRIVATE
#     ${CMAKE_CURRENT_SOURCE_DIR}/../../misogate/src/json_payload/json_payload.c
# )
