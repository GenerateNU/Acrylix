cmake_minimum_required(VERSION 3.22)

set(ZEPHYR_TOOLCHAIN_VARIANT zephyr)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(app)

# Smoke test build: only main.c. display.c requires CONFIG_LVGL=y and the lvgl
# module to be present. Restore the full source list after the smoke test passes
# (PB5 toggling confirmed) and west update has fetched lvgl.
#
# Full build source list (restore after smoke test):
#   src/display/display.c
#   src/stepper/stepper.c
#   src/temp/temp.c
#   src/states/states.c
target_sources(app PRIVATE
	src/main.c)
