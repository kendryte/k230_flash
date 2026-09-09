cmake_minimum_required(VERSION 3.18)

project(kburn_top)

set(K230_FLASH_MACOS_SIGN_IDENTITY "" CACHE STRING
    "Developer ID Application identity used to sign installed macOS binaries")
set(K230_FLASH_MACOS_NOTARY_PROFILE "" CACHE STRING
    "notarytool keychain profile used to notarize the macOS archive")
set(K230_FLASH_MACOS_KEYCHAIN "" CACHE FILEPATH
    "Optional keychain used for macOS signing and notarization")
set(K230_FLASH_MACOS_ARCHIVE_PATH "" CACHE FILEPATH
    "Optional output ZIP created after installing and signing on macOS")

# libkburn
add_subdirectory(src/kburn)

# cli
add_subdirectory(src/cli)

install(FILES LICENSE README.md DESTINATION .)

# Run the platform post-install step last, after all runtime files and
# documentation are present in the installation prefix.
if(WIN32)
    install(CODE "
        execute_process(
            COMMAND \"${CMAKE_COMMAND}\"
                \"-DTARGET_PLATFORM=windows\"
                \"-DINSTALL_PREFIX=\${CMAKE_INSTALL_PREFIX}\"
                \"-DEXECUTABLE_NAME=k230_flash_cli\"
                \"-DBUILD_WITH_MINGW=${BUILD_WITH_MINGW}\"
                \"-DTOOLCHAIN_ROOT=${TOOLCHAIN_ROOT}\"
                \"-DOBJDUMP_COMMAND=${OBJDUMP_COMMAND}\"
                -P \"${CMAKE_CURRENT_SOURCE_DIR}/src/cli/cmake/win-install.cmake\"
            RESULT_VARIABLE PLATFORM_INSTALL_RESULT
            COMMAND_ECHO STDOUT
        )
        if(NOT PLATFORM_INSTALL_RESULT STREQUAL \"0\")
            message(FATAL_ERROR \"Windows install processing failed: \${PLATFORM_INSTALL_RESULT}\")
        endif()
    ")
elseif(APPLE)
    install(CODE "
        execute_process(
            COMMAND \"${CMAKE_COMMAND}\"
                \"-DTARGET_PLATFORM=macos\"
                \"-DINSTALL_PREFIX=\${CMAKE_INSTALL_PREFIX}\"
                \"-DCMAKE_CACHEFILE_DIR=${CMAKE_BINARY_DIR}\"
                \"-DEXECUTABLE_NAME=k230_flash_cli\"
                \"-DSIGN_IDENTITY=${K230_FLASH_MACOS_SIGN_IDENTITY}\"
                \"-DNOTARY_PROFILE=${K230_FLASH_MACOS_NOTARY_PROFILE}\"
                \"-DKEYCHAIN_PATH=${K230_FLASH_MACOS_KEYCHAIN}\"
                \"-DARCHIVE_PATH=${K230_FLASH_MACOS_ARCHIVE_PATH}\"
                -P \"${CMAKE_CURRENT_SOURCE_DIR}/src/cli/cmake/mac-install.cmake\"
            RESULT_VARIABLE PLATFORM_INSTALL_RESULT
            COMMAND_ECHO STDOUT
        )
        if(NOT PLATFORM_INSTALL_RESULT STREQUAL \"0\")
            message(FATAL_ERROR \"macOS install processing failed: \${PLATFORM_INSTALL_RESULT}\")
        endif()
    ")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT IS_CIBUILDWHEEL)
    install(CODE "
        execute_process(
            COMMAND \"${CMAKE_COMMAND}\"
                \"-DTARGET_PLATFORM=linux\"
                \"-DINSTALL_PREFIX=\${CMAKE_INSTALL_PREFIX}\"
                \"-DEXECUTABLE_NAME=k230_flash_cli\"
                -P \"${CMAKE_CURRENT_SOURCE_DIR}/src/cli/cmake/linux-install.cmake\"
            RESULT_VARIABLE PLATFORM_INSTALL_RESULT
            COMMAND_ECHO STDOUT
        )
        if(NOT PLATFORM_INSTALL_RESULT STREQUAL \"0\")
            message(FATAL_ERROR \"Linux install processing failed: \${PLATFORM_INSTALL_RESULT}\")
        endif()
    ")
endif()
