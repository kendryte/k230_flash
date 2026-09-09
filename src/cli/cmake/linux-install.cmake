cmake_minimum_required(VERSION 3.18)

if(NOT DEFINED TARGET_PLATFORM OR NOT TARGET_PLATFORM STREQUAL "linux")
    message(FATAL_ERROR "linux-install.cmake requires TARGET_PLATFORM=linux")
endif()
if(NOT DEFINED INSTALL_PREFIX OR INSTALL_PREFIX STREQUAL "")
    message(FATAL_ERROR "INSTALL_PREFIX is required")
endif()
if(NOT DEFINED EXECUTABLE_NAME OR EXECUTABLE_NAME STREQUAL "")
    message(FATAL_ERROR "EXECUTABLE_NAME is required")
endif()

set(CLI_PATH "${INSTALL_PREFIX}/bin/${EXECUTABLE_NAME}")
if(NOT EXISTS "${CLI_PATH}")
    message(FATAL_ERROR "Installed CLI not found: ${CLI_PATH}")
endif()

file(GLOB KBURN_LIBRARIES LIST_DIRECTORIES FALSE "${INSTALL_PREFIX}/bin/libkburn.so*")
file(GLOB LIBUSB_LIBRARIES LIST_DIRECTORIES FALSE "${INSTALL_PREFIX}/bin/libusb-1.0.so*")
if(NOT KBURN_LIBRARIES)
    message(FATAL_ERROR "Installed libkburn was not found under ${INSTALL_PREFIX}/bin")
endif()
if(NOT LIBUSB_LIBRARIES)
    message(FATAL_ERROR "Installed libusb was not found under ${INSTALL_PREFIX}/bin")
endif()

find_program(READELF_EXECUTABLE readelf REQUIRED)
find_program(LDD_EXECUTABLE ldd REQUIRED)

execute_process(
    COMMAND "${READELF_EXECUTABLE}" -d "${CLI_PATH}"
    OUTPUT_VARIABLE dynamic_section
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE readelf_result
)
if(NOT readelf_result STREQUAL "0")
    message(FATAL_ERROR "Failed to inspect ${CLI_PATH} with readelf")
endif()

string(FIND "${dynamic_section}" "$ORIGIN" cli_lib_rpath_position)
if(cli_lib_rpath_position EQUAL -1)
    message(FATAL_ERROR "Installed CLI is missing the $ORIGIN runtime path")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "LD_LIBRARY_PATH=${INSTALL_PREFIX}/bin:$ENV{LD_LIBRARY_PATH}"
        "${LDD_EXECUTABLE}" "${CLI_PATH}"
    OUTPUT_VARIABLE linked_libraries
    ERROR_VARIABLE linked_libraries_error
    RESULT_VARIABLE ldd_result
)
if(NOT ldd_result STREQUAL "0")
    message(FATAL_ERROR
        "Failed to resolve installed CLI dependencies:\n${linked_libraries_error}")
endif()

string(FIND "${linked_libraries}" "not found" missing_dependency_position)
if(NOT missing_dependency_position EQUAL -1)
    message(FATAL_ERROR "Installed CLI has unresolved dependencies:\n${linked_libraries}")
endif()

message(STATUS "Installed Linux dependency paths:\n${linked_libraries}")
