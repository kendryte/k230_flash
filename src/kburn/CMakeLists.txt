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
set(K230_FLASH_VERSION_PATCH 6)

set(K230_FLASH_VERSION_STRING ${K230_FLASH_VERSION_MAJOR}.${K230_FLASH_VERSION_MINOR}.${K230_FLASH_VERSION_PATCH})
message(STATUS "K230_FLASH_VERSION_STRING = ${K230_FLASH_VERSION_STRING}")

option(IS_CIBUILDWHEEL "Build in cibuildwheel docker" OFF)

if(DEFINED ENV{CI})
	set(IS_CI TRUE)
else()
	set(IS_CI FALSE)
endif()

execute_process(
    COMMAND git rev-parse HEAD
    OUTPUT_VARIABLE GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

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
	PREFIX lib
)

if(APPLE)
    # macOS-specific rpath settings
    target_link_options(kburn PRIVATE
        "-Wl,-rpath,@loader_path"
        "-Wl,-rpath,@loader_path/../lib"
        "-Wl,-rpath,@executable_path"
    )
elseif(UNIX AND NOT APPLE)
    # Linux-specific rpath settings
    target_link_options(kburn PRIVATE
        "-Wl,-rpath,$ORIGIN"
    )
elseif(WIN32)

endif()

# Set RPATH for installed binaries (common for macOS and Linux)
if(NOT WIN32)
    set_target_properties(kburn PROPERTIES
        INSTALL_RPATH "$ORIGIN"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

####################################### spdlog ################################
set(SPDLOG_BUILD_PIC ON)

if(APPLE)
	set(SPDLOG_FMT_EXTERNAL_HO ON)
endif()

add_subdirectory(3rd-party/spdlog)

add_dependencies(kburn spdlog)
target_link_libraries(kburn PUBLIC spdlog $<$<BOOL:${MINGW}>:ws2_32>)

####################################### libusb ################################
set(BUILD_SHARED_LIBS ON)
set(LIBUSB_INSTALL_TARGETS OFF)
add_subdirectory(3rd-party/libusb-cmake)

add_dependencies(kburn usb-1.0)
target_link_libraries(kburn PUBLIC usb-1.0)

# Add k230_flash_cli project
add_subdirectory(cli)

if(LINUX AND IS_CIBUILDWHEEL)
	message("Build by CIBUILDWHEEL")

	install(TARGETS kburn usb-1.0
		DESTINATION /lib/kburn)
else()
	install(TARGETS kburn usb-1.0)
endif()
