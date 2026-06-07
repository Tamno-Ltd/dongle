cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(dongle)

if(NOT NCS_VERSION_MAJOR EQUAL 3 OR NOT NCS_VERSION_MINOR EQUAL 2 OR NCS_VERSION_PATCH LESS 3)
	message(FATAL_ERROR "Dongle requires NCS v3.2.3+, found v${NCS_VERSION}")
endif()

# cpuapp (application core) image: USB HID mouse + cpurad IPC receiver.
target_sources(app PRIVATE
	src/main.c
	usb/usb_hid.c
)

# Repo root for "usb/usb_hid.h"; include/ for the shared "dongle_ipc.h".
target_include_directories(app PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/include
)
