cmake_minimum_required(VERSION 3.4...3.18)

option(BUILD_WITH_MINGW "Build in Dokcer MingW" OFF)

if(NOT DEFINED CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "Release")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "")
	set(CMAKE_BUILD_TYPE "Release")
endif()

project(k230_flash)

add_executable(${PROJECT_NAME} k230_flash.cpp)

add_dependencies(${PROJECT_NAME} kburn)
target_link_libraries(${PROJECT_NAME} PRIVATE kburn)

set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 17
    OUTPUT_NAME k230_flash_cli
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_subdirectory(CLI11)
target_link_libraries(${PROJECT_NAME} PRIVATE CLI11::CLI11)

if(BUILD_TESTING)
    add_test(
        NAME k230_flash_cli_version
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --version
    )
    set_tests_properties(k230_flash_cli_version PROPERTIES
        PASS_REGULAR_EXPRESSION
            "k230_flash_cli [0-9]+\\.[0-9]+\\.[0-9]+ \\(commit [^)]+\\)"
    )
endif()

if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        BUILD_RPATH "${CMAKE_BINARY_DIR}/bin"
        INSTALL_RPATH "@loader_path"
    )
elseif(UNIX)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        BUILD_RPATH "${CMAKE_BINARY_DIR}/bin"
        INSTALL_RPATH "$ORIGIN"
    )
endif()

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)
