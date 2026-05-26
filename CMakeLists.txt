cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(dongle_hello)

if(NOT NCS_VERSION_MAJOR EQUAL 3 OR NOT NCS_VERSION_MINOR EQUAL 2 OR NCS_VERSION_PATCH LESS 3)
	message(FATAL_ERROR "Dongle hello requires NCS v3.2.3+, found v${NCS_VERSION}")
endif()

target_sources(app PRIVATE src/main.c)
