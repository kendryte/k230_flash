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

install(TARGETS ${PROJECT_NAME})

get_target_property(OUTPUT_EXE_NAME ${PROJECT_NAME} OUTPUT_NAME)

if(WIN32 AND BUILD_WITH_MINGW)
    install(CODE "
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E echo \"Running copy_dlls.sh\"
            COMMAND bash \"\${CMAKE_CURRENT_SOURCE_DIR}/cli/cmake/copy_dlls.sh\"
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
