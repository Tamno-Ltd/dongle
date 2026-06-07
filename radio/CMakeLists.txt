cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(dongle_radio)

target_sources(app PRIVATE
	src/main.c
)

# Reach the shared IPC contract (include/dongle_ipc.h) at the dongle repo root.
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
