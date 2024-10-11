cmake_minimum_required(VERSION 3.4...3.18)

option(BUILD_WITH_MINGW "Build in Dokcer MingW" OFF)

project(k230_flash)

add_executable(${PROJECT_NAME} k230_flash.cpp)

add_dependencies(${PROJECT_NAME} kburn)
target_link_libraries(${PROJECT_NAME} PRIVATE kburn)

set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 17
    OUTPUT_NAME k230_flash_cli
)

add_subdirectory(CLI11)
target_link_libraries(${PROJECT_NAME} PRIVATE CLI11::CLI11)

if(APPLE)
    # macOS-specific rpath settings
    target_link_options(${PROJECT_NAME} PRIVATE
        "-Wl,-rpath,@loader_path"
        "-Wl,-rpath,@loader_path/../lib"
        "-Wl,-rpath,@executable_path"
    )
elseif(UNIX AND NOT APPLE)
    # Linux-specific rpath settings
    target_link_options(${PROJECT_NAME} PRIVATE
        "-Wl,-rpath,$ORIGIN"
        "-Wl,-rpath,$ORIGIN/../lib"
    )
elseif(WIN32)

endif()

# Set RPATH for installed binaries (common for macOS and Linux)
if(NOT WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        INSTALL_RPATH "$ORIGIN"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

install(TARGETS ${PROJECT_NAME})

get_target_property(OUTPUT_EXE_NAME ${PROJECT_NAME} OUTPUT_NAME)

message(STATUS "CMAKE_CURRENT_LIST_DIR ${CMAKE_CURRENT_LIST_DIR}")
message(STATUS "CMAKE_CURRENT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}")
message(STATUS "CMAKE_SOURCE_DIR ${CMAKE_SOURCE_DIR}")

if(WIN32 AND BUILD_WITH_MINGW)
    install(CODE "
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E echo \"Running copy_dlls.sh\"
            COMMAND bash \"\${CMAKE_CURRENT_LIST_DIR}/cmake/copy_dlls.sh\"
                    \"${OUTPUT_EXE_NAME}\" \"${TOOLCHAIN_ROOT}\" \"${CMAKE_INSTALL_PREFIX}\" \"${OBJDUMP_COMMAND}\"
            RESULT_VARIABLE RESULT
            COMMAND_ECHO STDOUT
            COMMAND_ERROR_IS_FATAL ANY
        )
        if(NOT RESULT EQUAL 0)
            message(FATAL_ERROR \"Failed to execute copy_dlls.sh.\")
        endif()
    ")
endif()
