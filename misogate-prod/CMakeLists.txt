cmake_minimum_required(VERSION 3.20.0)

# Point CMake at Zephyr
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(misogate_gateway)

# Your source files
target_sources(app PRIVATE
  src/main.c
  src/packet.c
  src/crypto_min.c
  src/siphash.c
  src/mqtt/mqtt.c
)

# Include directories
zephyr_include_directories(src)
zephyr_include_directories(src/mqtt)
