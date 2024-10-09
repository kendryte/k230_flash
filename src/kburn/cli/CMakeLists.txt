cmake_minimum_required(VERSION 3.4...3.18)

project(k230_flash VERSION ${VERSION_INFO})

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
