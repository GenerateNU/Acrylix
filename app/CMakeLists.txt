cmake_minimum_required(VERSION 3.22)

set(ZEPHYR_TOOLCHAIN_VARIANT zephyr)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(app)

target_sources(app PRIVATE
	src/main.c)

# Subsystems re-enabled once smoke test confirms MCU is running:
# src/display/display.c
# src/stepper/stepper.c
# src/temp/temp.c
# src/states/states.c
