cmake_minimum_required(VERSION 3.18)

if(NOT DEFINED VERSION_INFO)
    set(VERSION_INFO "0.0.1")
endif()

project(kburn VERSION ${VERSION_INFO})

# libkburn
add_subdirectory(src/kburn)

# pybind
add_subdirectory(src/python/kburn/native)
