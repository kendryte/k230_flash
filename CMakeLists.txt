cmake_minimum_required(VERSION 3.18)

if(NOT DEFINED VERSION_INFO)
    set(VERSION_INFO "0.0.1")
endif()

project(kburn VERSION ${VERSION_INFO})

# libkburn
option(CIBUILDWHEEL "Is USE cibuildwheel" OFF)

if (NOT CIBUILDWHEEL)
    add_subdirectory(src/kburn)
else()
    message("Is cibuildwheel")
endif()

# pybind
add_subdirectory(src/python/kburn/native)
