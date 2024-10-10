cmake_minimum_required(VERSION 3.18)

set(K230_FLASH_VERSION_MAJOR 0)
set(K230_FLASH_VERSION_MINOR 0)
set(K230_FLASH_VERSION_PATCH 2)

set(K230_FLASH_VERSION_STRING ${K230_FLASH_VERSION_MAJOR}.${K230_FLASH_VERSION_MINOR}.${K230_FLASH_VERSION_PATCH})
message(STATUS "K230_FLASH_VERSION_STRING = ${K230_FLASH_VERSION_STRING}")

project(kburn_top)

# libkburn
add_subdirectory(src/kburn)

# pybind
add_subdirectory(src/python/kburn/native)
