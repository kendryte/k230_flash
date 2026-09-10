cmake_minimum_required(VERSION 3.18)

if(NOT DEFINED TARGET_PLATFORM OR NOT TARGET_PLATFORM STREQUAL "macos")
    message(FATAL_ERROR "mac-install.cmake requires TARGET_PLATFORM=macos")
endif()

if(NOT DEFINED INSTALL_PREFIX OR INSTALL_PREFIX STREQUAL "")
    message(FATAL_ERROR "INSTALL_PREFIX is required")
endif()
if(NOT DEFINED EXECUTABLE_NAME OR EXECUTABLE_NAME STREQUAL "")
    message(FATAL_ERROR "EXECUTABLE_NAME is required")
endif()

set(CLI_PATH "${INSTALL_PREFIX}/bin/${EXECUTABLE_NAME}")
if(NOT EXISTS "${CLI_PATH}")
    message(FATAL_ERROR "Installed CLI not found: ${CLI_PATH}")
endif()

find_program(FILE_EXECUTABLE file REQUIRED)
find_program(OTOOL_EXECUTABLE otool REQUIRED)

function(is_macho path result_var)
    execute_process(
        COMMAND "${FILE_EXECUTABLE}" -b "${path}"
        OUTPUT_VARIABLE file_description
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE file_result
    )
    if(file_result STREQUAL "0" AND file_description MATCHES "Mach-O")
        set(${result_var} TRUE PARENT_SCOPE)
    else()
        set(${result_var} FALSE PARENT_SCOPE)
    endif()
endfunction()

function(sign_macho_tree search_root)
    if(NOT IS_DIRECTORY "${search_root}")
        return()
    endif()

    file(GLOB_RECURSE sign_candidates LIST_DIRECTORIES FALSE "${search_root}/*")
    list(REMOVE_ITEM sign_candidates "${CLI_PATH}")
    list(APPEND sign_candidates "${CLI_PATH}")
    foreach(candidate IN LISTS sign_candidates)
        if(IS_SYMLINK "${candidate}")
            continue()
        endif()

        is_macho("${candidate}" candidate_is_macho)
        if(candidate_is_macho)
            file(RELATIVE_PATH display_path "${INSTALL_PREFIX}" "${candidate}")
            message(STATUS "Signing ${display_path}")
            execute_process(
                COMMAND "${CODESIGN_EXECUTABLE}"
                    --force
                    --timestamp
                    --options runtime
                    ${CODESIGN_KEYCHAIN_ARGS}
                    --sign "${SIGN_IDENTITY}"
                    "${candidate}"
                RESULT_VARIABLE sign_result
                COMMAND_ECHO STDOUT
            )
            if(NOT sign_result STREQUAL "0")
                message(FATAL_ERROR "Failed to sign ${candidate}: ${sign_result}")
            endif()
        endif()
    endforeach()
endfunction()

if(DEFINED SIGN_IDENTITY AND NOT SIGN_IDENTITY STREQUAL "")
    find_program(CODESIGN_EXECUTABLE codesign REQUIRED)
    set(CODESIGN_KEYCHAIN_ARGS)
    if(DEFINED KEYCHAIN_PATH AND NOT KEYCHAIN_PATH STREQUAL "")
        list(APPEND CODESIGN_KEYCHAIN_ARGS --keychain "${KEYCHAIN_PATH}")
    endif()

    # Shared libraries are independent code objects and must be signed before
    # the executable that loads them.
    sign_macho_tree("${INSTALL_PREFIX}/bin")

    file(GLOB_RECURSE verify_candidates LIST_DIRECTORIES FALSE
        "${INSTALL_PREFIX}/bin/*"
    )
    foreach(candidate IN LISTS verify_candidates)
        if(IS_SYMLINK "${candidate}")
            continue()
        endif()

        is_macho("${candidate}" candidate_is_macho)
        if(candidate_is_macho)
            execute_process(
                COMMAND "${CODESIGN_EXECUTABLE}" --verify --strict --verbose=2 "${candidate}"
                RESULT_VARIABLE verify_result
                COMMAND_ECHO STDOUT
            )
            if(NOT verify_result STREQUAL "0")
                message(FATAL_ERROR "Signature verification failed for ${candidate}")
            endif()
        endif()
    endforeach()
else()
    message(STATUS "K230_FLASH_MACOS_SIGN_IDENTITY is empty; installing unsigned binaries")
endif()

function(validate_macho_dependencies path)
    execute_process(
        COMMAND "${OTOOL_EXECUTABLE}" -L "${path}"
        OUTPUT_VARIABLE linked_libraries
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE otool_result
    )
    if(NOT otool_result STREQUAL "0")
        message(FATAL_ERROR "Failed to inspect ${path} with otool")
    endif()

    file(RELATIVE_PATH display_path "${INSTALL_PREFIX}" "${path}")
    message(STATUS "Dependency paths for ${display_path}:\n${linked_libraries}")

    if(DEFINED CMAKE_CACHEFILE_DIR AND NOT CMAKE_CACHEFILE_DIR STREQUAL "")
        string(FIND "${linked_libraries}" "${CMAKE_CACHEFILE_DIR}" build_path_position)
        if(NOT build_path_position EQUAL -1)
            message(FATAL_ERROR "${display_path} still references the build directory")
        endif()
    endif()

    string(REPLACE "\n" ";" dependency_lines "${linked_libraries}")
    foreach(dependency_line IN LISTS dependency_lines)
        string(STRIP "${dependency_line}" dependency_line)
        if(dependency_line MATCHES ":$")
            continue()
        endif()
        if(dependency_line MATCHES "^/"
                AND NOT dependency_line MATCHES "^/usr/lib/"
                AND NOT dependency_line MATCHES "^/System/Library/")
            message(FATAL_ERROR
                "${display_path} has an unbundled absolute dependency: ${dependency_line}")
        endif()
    endforeach()
endfunction()

file(GLOB_RECURSE dependency_candidates LIST_DIRECTORIES FALSE
    "${INSTALL_PREFIX}/bin/*"
)
foreach(candidate IN LISTS dependency_candidates)
    if(IS_SYMLINK "${candidate}")
        continue()
    endif()
    is_macho("${candidate}" candidate_is_macho)
    if(candidate_is_macho)
        validate_macho_dependencies("${candidate}")
    endif()
endforeach()

if(DEFINED ARCHIVE_PATH AND NOT ARCHIVE_PATH STREQUAL "")
    if(EXISTS "${ARCHIVE_PATH}")
        message(FATAL_ERROR "Archive already exists: ${ARCHIVE_PATH}")
    endif()

    get_filename_component(archive_directory "${ARCHIVE_PATH}" DIRECTORY)
    file(MAKE_DIRECTORY "${archive_directory}")
    find_program(DITTO_EXECUTABLE ditto REQUIRED)
    execute_process(
        COMMAND "${DITTO_EXECUTABLE}"
            -c -k --sequesterRsrc --keepParent
            "${INSTALL_PREFIX}"
            "${ARCHIVE_PATH}"
        RESULT_VARIABLE archive_result
        COMMAND_ECHO STDOUT
    )
    if(NOT archive_result STREQUAL "0")
        message(FATAL_ERROR "Failed to create archive ${ARCHIVE_PATH}")
    endif()
    if(NOT EXISTS "${ARCHIVE_PATH}")
        message(FATAL_ERROR "Archive command did not create ${ARCHIVE_PATH}")
    endif()
    message(STATUS "Created macOS archive: ${ARCHIVE_PATH}")

    if(DEFINED NOTARY_PROFILE AND NOT NOTARY_PROFILE STREQUAL "")
        if(NOT DEFINED SIGN_IDENTITY OR SIGN_IDENTITY STREQUAL "")
            message(FATAL_ERROR "Notarization requires K230_FLASH_MACOS_SIGN_IDENTITY")
        endif()

        find_program(XCRUN_EXECUTABLE xcrun REQUIRED)
        set(NOTARY_KEYCHAIN_ARGS)
        if(DEFINED KEYCHAIN_PATH AND NOT KEYCHAIN_PATH STREQUAL "")
            list(APPEND NOTARY_KEYCHAIN_ARGS --keychain "${KEYCHAIN_PATH}")
        endif()
        execute_process(
            COMMAND "${XCRUN_EXECUTABLE}" notarytool submit "${ARCHIVE_PATH}"
                --keychain-profile "${NOTARY_PROFILE}"
                ${NOTARY_KEYCHAIN_ARGS}
                --wait --output-format json
            RESULT_VARIABLE notary_result
            OUTPUT_VARIABLE notary_output
            COMMAND_ECHO STDOUT
        )
        file(WRITE "${ARCHIVE_PATH}.notary.json" "${notary_output}")
        if(NOT notary_result STREQUAL "0")
            message(FATAL_ERROR "Notarization failed for ${ARCHIVE_PATH}")
        endif()
        find_program(PLUTIL_EXECUTABLE plutil REQUIRED)
        execute_process(
            COMMAND "${PLUTIL_EXECUTABLE}" -extract status raw -o - "${ARCHIVE_PATH}.notary.json"
            RESULT_VARIABLE status_result
            OUTPUT_VARIABLE notary_status
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(NOT status_result STREQUAL "0" OR NOT notary_status STREQUAL "Accepted")
            message(FATAL_ERROR "Notarization not accepted; see ${ARCHIVE_PATH}.notary.json")
        endif()
        message(STATUS "Notarization accepted for ${ARCHIVE_PATH}")
    else()
        message(STATUS "K230_FLASH_MACOS_NOTARY_PROFILE is empty; skipping notarization")
    endif()
elseif(DEFINED NOTARY_PROFILE AND NOT NOTARY_PROFILE STREQUAL "")
    message(FATAL_ERROR "K230_FLASH_MACOS_ARCHIVE_PATH is required for notarization")
endif()
