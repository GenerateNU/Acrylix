cmake_minimum_required(VERSION 3.22)

set(ZEPHYR_TOOLCHAIN_VARIANT zephyr)
## had to set this for esp32 to be able to correctly create the .elf file
##set(CMAKE_OBJCOPY $ENV{HOME}/zephyr-sdk-0.17.2/xtensa-espressif_esp32_zephyr-elf/bin/xtensa-espressif_esp32_zephyr-elf-objcopy)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(app)

# this didn't work when this was called app. It must be called app because... TO-DO...
target_sources(app PRIVATE src/main.c)
