cmake_minimum_required(VERSION 3.18)

project(kburn_python VERSION ${VERSION_INFO})

find_package(Python3 COMPONENTS Interpreter Development.Module REQUIRED)

# Get the path to pybind11
execute_process(
    COMMAND ${Python3_EXECUTABLE} -c "import pybind11; print(pybind11.get_cmake_dir())"
    OUTPUT_VARIABLE PYBIND11_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
message(STATUS "Pybind11 path: ${PYBIND11_DIR}")

find_package(pybind11 REQUIRED PATHS ${PYBIND11_DIR} NO_DEFAULT_PATH)

pybind11_add_module(kburn_python ffi.cpp)

add_dependencies(kburn_python kburn)
target_link_libraries(kburn_python PUBLIC kburn)

set_target_properties(kburn_python PROPERTIES
    CXX_STANDARD 17
    OUTPUT_NAME _kburn
    INSTALL_RPATH "\$ORIGIN"
    BUILD_WITH_INSTALL_RPATH TRUE
)

if(APPLE)
    target_link_options(kburn_python PRIVATE
        "-Wl,-rpath,@loader_path"
        "-Wl,-rpath,@executable_path"
    )
    install(TARGETS kburn_python
        LIBRARY DESTINATION lib
    )
else()
    install(TARGETS kburn_python)
endif()
