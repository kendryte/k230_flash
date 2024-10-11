cmake_minimum_required(VERSION 3.18)

project(kburn_top)

# libkburn
add_subdirectory(src/kburn)

# pybind
add_subdirectory(src/python/kburn/native)
