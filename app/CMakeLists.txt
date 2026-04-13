cmake_minimum_required(VERSION 3.22)

set(ZEPHYR_TOOLCHAIN_VARIANT zephyr)

# Tell Zephyr to search app/boards/ for custom board definitions
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(app)

target_sources(app PRIVATE
	src/main.c
	src/temp/ssr.c
	src/display/display.c
	src/stepper/drv8452_spi.c
	src/stepper/stepper.c
	src/temp/temp.c
	src/temp/temp_sensor.c
	src/temp/temp_control.c
	src/states/states.c)
