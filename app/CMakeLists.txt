cmake_minimum_required(VERSION 3.22)

set(ZEPHYR_TOOLCHAIN_VARIANT zephyr)
set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(app)

target_include_directories(app PRIVATE src)

target_sources(app PRIVATE
	src/main.c
	src/display/display.c
	src/states/states.c
)

# Re-enable when ready:
# src/stepper/stepper.c
# src/temp/temp.c

