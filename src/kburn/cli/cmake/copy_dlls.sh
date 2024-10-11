#!/bin/bash

set -e  # Exit immediately if a command exits with a non-zero status.

OUTPUT_EXE_NAME="$1"        # The first argument is the output executable name.
TOOLCHAIN_ROOT="$2"          # The second argument is the toolchain root directory.
INSTALL_PREFIX="$3"          # The third argument is the install prefix.
OBJDUMP_COMMAND="$4"         # The fourth argument is the objdump command.

# Check if the required arguments are provided
if [ -z "$OUTPUT_EXE_NAME" ] || [ -z "$TOOLCHAIN_ROOT" ] || [ -z "$INSTALL_PREFIX" ] || [ -z "$OBJDUMP_COMMAND" ]; then
    echo "Usage: $0 <output_exe_name> <toolchain_root> <install_prefix> <objdump_command>"
    exit 1
fi

# Extract DLL names using objdump
DLL_NAMES=$($OBJDUMP_COMMAND -p "$INSTALL_PREFIX/bin/$OUTPUT_EXE_NAME.exe" | grep "DLL Name" | awk '{print $3}')

# Check if DLL_NAMES is empty
if [ -z "$DLL_NAMES" ]; then
    echo "No DLL names found for $OUTPUT_EXE_NAME."
    exit 1
fi

# Skip specific DLLs and copy the rest
for DLL in $DLL_NAMES; do
    case "$DLL" in
        "libunwind.dll" | "libc++.dll")
            FOUND_DLL=$(find "$TOOLCHAIN_ROOT" -name "$DLL" 2>/dev/null)
            if [ -n "$FOUND_DLL" ]; then
                echo "Copying DLL: $FOUND_DLL to $INSTALL_PREFIX/bin"
                cp "$FOUND_DLL" "$INSTALL_PREFIX/bin"
            else
                echo "Error: Required DLL not found: $DLL"
                exit 1
            fi
            ;;
        *)
            echo "Skipping DLL: $DLL"
            ;;
    esac
done

echo "All required DLLs have been copied."
