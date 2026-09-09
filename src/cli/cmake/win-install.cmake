cmake_minimum_required(VERSION 3.18)

if(NOT DEFINED TARGET_PLATFORM OR NOT TARGET_PLATFORM STREQUAL "windows")
    message(FATAL_ERROR "win-install.cmake requires TARGET_PLATFORM=windows")
endif()
if(NOT DEFINED INSTALL_PREFIX OR INSTALL_PREFIX STREQUAL "")
    message(FATAL_ERROR "INSTALL_PREFIX is required")
endif()
if(NOT DEFINED EXECUTABLE_NAME OR EXECUTABLE_NAME STREQUAL "")
    message(FATAL_ERROR "EXECUTABLE_NAME is required")
endif()

set(BIN_DIR "${INSTALL_PREFIX}/bin")
set(CLI_PATH "${BIN_DIR}/${EXECUTABLE_NAME}.exe")
if(NOT EXISTS "${CLI_PATH}")
    message(FATAL_ERROR "Installed CLI not found: ${CLI_PATH}")
endif()

file(GLOB KBURN_DLLS LIST_DIRECTORIES FALSE "${BIN_DIR}/*kburn*.dll")
file(GLOB LIBUSB_DLLS LIST_DIRECTORIES FALSE "${BIN_DIR}/*usb*.dll")
if(NOT KBURN_DLLS)
    message(FATAL_ERROR "Installed kburn DLL was not found under ${BIN_DIR}")
endif()
if(NOT LIBUSB_DLLS)
    message(FATAL_ERROR "Installed libusb DLL was not found under ${BIN_DIR}")
endif()

if(BUILD_WITH_MINGW)
    if(NOT DEFINED TOOLCHAIN_ROOT OR TOOLCHAIN_ROOT STREQUAL "")
        message(FATAL_ERROR "TOOLCHAIN_ROOT is required for a MinGW install")
    endif()
    if(NOT DEFINED OBJDUMP_COMMAND OR OBJDUMP_COMMAND STREQUAL "")
        message(FATAL_ERROR "OBJDUMP_COMMAND is required for a MinGW install")
    endif()

    if(IS_ABSOLUTE "${OBJDUMP_COMMAND}" AND EXISTS "${OBJDUMP_COMMAND}")
        set(OBJDUMP_EXECUTABLE "${OBJDUMP_COMMAND}")
    else()
        find_program(OBJDUMP_EXECUTABLE NAMES "${OBJDUMP_COMMAND}" REQUIRED)
    endif()

    execute_process(
        COMMAND "${OBJDUMP_EXECUTABLE}" -p "${CLI_PATH}"
        OUTPUT_VARIABLE pe_headers
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE objdump_result
    )
    if(NOT objdump_result STREQUAL "0")
        message(FATAL_ERROR "Failed to inspect ${CLI_PATH} with objdump")
    endif()
    string(TOLOWER "${pe_headers}" pe_headers_lower)

    get_filename_component(toolchain_parent "${TOOLCHAIN_ROOT}" DIRECTORY)
    foreach(runtime_dll IN ITEMS libunwind.dll libc++.dll)
        string(FIND "${pe_headers_lower}" "${runtime_dll}" runtime_dependency_position)
        if(runtime_dependency_position EQUAL -1)
            continue()
        endif()

        unset(runtime_dll_path CACHE)
        find_file(runtime_dll_path
            NAMES "${runtime_dll}"
            PATHS "${TOOLCHAIN_ROOT}" "${TOOLCHAIN_ROOT}/bin" "${toolchain_parent}/bin"
            NO_DEFAULT_PATH
        )
        if(NOT runtime_dll_path)
            message(FATAL_ERROR "Required toolchain runtime not found: ${runtime_dll}")
        endif()

        message(STATUS "Installing ${runtime_dll_path}")
        file(INSTALL "${runtime_dll_path}" DESTINATION "${BIN_DIR}")
    endforeach()
endif()

file(REMOVE_RECURSE "${INSTALL_PREFIX}/lib")
message(STATUS "Installed Windows runtime files in ${BIN_DIR}")
