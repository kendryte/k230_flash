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
target_link_libraries(kburn_python PRIVATE kburn)

set_target_properties(kburn_python PROPERTIES
    CXX_STANDARD 17
    OUTPUT_NAME _kburn
)

if(APPLE)
    # macOS-specific rpath settings
    target_link_options(kburn_python PRIVATE
        "-Wl,-rpath,@loader_path"
        "-Wl,-rpath,@loader_path/../lib"
        "-Wl,-rpath,@executable_path"
    )
elseif(UNIX AND NOT APPLE)
    # Linux-specific rpath settings
    target_link_options(kburn_python PRIVATE
        "-Wl,-rpath,$ORIGIN"
        "-Wl,-rpath,$ORIGIN/../lib"
    )
elseif(WIN32)

endif()

# Set RPATH for installed binaries (common for macOS and Linux)
if(NOT WIN32)
    set_target_properties(kburn_python PROPERTIES
        INSTALL_RPATH "$ORIGIN;$ORIGIN/../lib"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

install(TARGETS kburn_python
    DESTINATION lib
)
