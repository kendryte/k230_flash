cmake_minimum_required(VERSION 3.18)

function(load_binary target variable src)
	file(RELATIVE_PATH rel_path "${CMAKE_CURRENT_LIST_DIR}" "${src}")
	set(tgt "${CMAKE_CURRENT_BINARY_DIR}/${rel_path}.generate.c")
	set(tgt_hdr "${CMAKE_CURRENT_BINARY_DIR}/autogen/generated.${variable}.h")

	if("${src}" IS_NEWER_THAN "${tgt}")
		file(READ "${src}" content HEX)
		file(SIZE "${src}" size)

		string(REGEX REPLACE "[0-9a-f][0-9a-f]" "0x\\0," content_rep "${content}")

		file(
			WRITE "${tgt}"
			"#include <stdint.h>\n#include <stddef.h>\nconst char ${variable}[${size}] = {\n${content_rep}\n};\nconst size_t ${variable}_size = ${size};"
		)
		file(
			WRITE "${tgt_hdr}"
			"#include <stdint.h>\n#include <stddef.h>\nextern const char ${variable}[${size}];\nextern const size_t ${variable}_size;"
		)
	endif()

	target_include_directories("${target}" PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/autogen")
	target_sources("${target}" PRIVATE "${tgt}")
endfunction()

set(K230_FLASH_VERSION_MAJOR 0)
set(K230_FLASH_VERSION_MINOR 0)
set(K230_FLASH_VERSION_PATCH 10)

set(K230_FLASH_VERSION_STRING ${K230_FLASH_VERSION_MAJOR}.${K230_FLASH_VERSION_MINOR}.${K230_FLASH_VERSION_PATCH})
message(STATUS "K230_FLASH_VERSION_STRING = ${K230_FLASH_VERSION_STRING}")

option(IS_CIBUILDWHEEL "Build in cibuildwheel docker" OFF)

if(DEFINED ENV{CI})
	set(IS_CI TRUE)
else()
	set(IS_CI FALSE)
endif()

# Fetch the GIT_COMMIT environment variable
set(GIT_COMMIT_ENV $ENV{GIT_COMMIT})

# Fallback in case the environment variable is not set
if(NOT GIT_COMMIT_ENV)
    execute_process(
        COMMAND git -C "${CMAKE_SOURCE_DIR}" rev-parse HEAD
        OUTPUT_VARIABLE GIT_COMMIT_ENV
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()

# Print the commit hash
message(STATUS "GIT_COMMIT = ${GIT_COMMIT_ENV}")
set(GIT_COMMIT "${GIT_COMMIT_ENV}")

if(NOT DEFINED K230_FLASH_VERSION_STRING)
    set(K230_FLASH_VERSION_STRING "0.0.1")
endif()

project(libkburn VERSION ${K230_FLASH_VERSION_STRING})

set(COMPILE_TIME $DATE)
string(TIMESTAMP COMPILE_TIME_STRING "%Y-%m-%d %H:%M")
configure_file("${CMAKE_CURRENT_LIST_DIR}/include/version_info.h.in" "${CMAKE_CURRENT_BINARY_DIR}/autogen/version_info.h")

add_library(kburn SHARED)

file(GLOB K230_SRCS "burner_k230/*.cpp")

set(SRCS
    kburn.cpp
    kdimage.cpp
    ${K230_SRCS}
)

target_sources(kburn PRIVATE ${SRCS})
target_include_directories(kburn PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(kburn PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/autogen)

load_binary(kburn "k230_loader_mmc" "${CMAKE_CURRENT_LIST_DIR}/burner_k230/loader/loader_mmc.bin")
load_binary(kburn "k230_loader_spi_nand" "${CMAKE_CURRENT_LIST_DIR}/burner_k230/loader/loader_spi_nand.bin")
load_binary(kburn "k230_loader_spi_nor" "${CMAKE_CURRENT_LIST_DIR}/burner_k230/loader/loader_spi_nor.bin")

target_compile_definitions(kburn PRIVATE
    $<$<CONFIG:Debug>:IS_DEBUG=1>
    $<$<CONFIG:Release>:IS_DEBUG=0>
)

set_target_properties(kburn PROPERTIES
    LANGUAGE CXX
    LINKER_LANGUAGE CXX
    CXX_STANDARD 17
	PREFIX lib
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if(APPLE)
    set_target_properties(kburn PROPERTIES
        BUILD_RPATH "${CMAKE_BINARY_DIR}/bin"
        INSTALL_RPATH "@loader_path"
        BUILD_WITH_INSTALL_RPATH FALSE
    )
elseif(UNIX)
    set_target_properties(kburn PROPERTIES
        BUILD_RPATH "${CMAKE_BINARY_DIR}/bin"
        INSTALL_RPATH "$ORIGIN"
        BUILD_WITH_INSTALL_RPATH FALSE
    )
endif()

####################################### spdlog ################################
set(SPDLOG_BUILD_PIC ON)

add_subdirectory(3rd-party/spdlog)

add_dependencies(kburn spdlog)
target_link_libraries(kburn PUBLIC spdlog $<$<BOOL:${MINGW}>:ws2_32>)

####################################### libusb ################################
set(BUILD_SHARED_LIBS ON)
set(LIBUSB_INSTALL_TARGETS OFF)
add_subdirectory(3rd-party/libusb-cmake)

# Keep the runtime bundle flat on every desktop platform, matching the CLI
# executable and kburn library output directories.
set_target_properties(usb-1.0 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_dependencies(kburn usb-1.0)
target_link_libraries(kburn PUBLIC usb-1.0)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND IS_CIBUILDWHEEL)
	message("Build by CIBUILDWHEEL")

	install(TARGETS kburn usb-1.0
		RUNTIME DESTINATION bin
		LIBRARY DESTINATION bin
		ARCHIVE DESTINATION bin)
else()
	install(TARGETS kburn usb-1.0
		RUNTIME DESTINATION bin
		LIBRARY DESTINATION bin
		ARCHIVE DESTINATION bin
	)
endif()
