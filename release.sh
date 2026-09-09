#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: ./release.sh

Builds, installs, validates, and packages k230_flash for the selected platform.
The current host platform is selected unless K230_FLASH_TARGET_OS is set.

Common environment variables:
  K230_FLASH_TARGET_OS     linux, macos, or windows
  K230_FLASH_TARGET_ARCH   artifact architecture label
  K230_FLASH_BUILD_DIR     persistent CMake build directory
  K230_FLASH_DIST_DIR      artifact output directory
  CMAKE_GENERATOR          default: Ninja when available, otherwise Makefiles
  CMAKE_PREFIX_PATH        additional CMake package search path
  CMAKE_TOOLCHAIN_FILE     optional cross-compilation toolchain
  BUILD_WITH_MINGW         pass ON for the LLVM-MinGW toolchain

macOS environment variables:
  MACOS_SIGN_IDENTITY      required Developer ID Application identity
  MACOS_ARCHITECTURES      CMake architecture list (default: current machine)
  MACOS_DEPLOYMENT_TARGET  optional deployment target
  MACOS_NOTARY_PROFILE     optional notarytool keychain profile
  MACOS_KEYCHAIN           optional dedicated signing/notarization keychain
  MACOS_ALLOW_UNSIGNED=1   CI validation only; name the artifact unsigned
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi
if [[ $# -ne 0 ]]; then
    echo "Error: unexpected argument: $1" >&2
    usage >&2
    exit 1
fi

REPO_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)

detect_host_os() {
    local uname_value
    uname_value=$(uname -s | tr '[:upper:]' '[:lower:]')
    case "$uname_value" in
        darwin) echo macos ;;
        linux) echo linux ;;
        *mingw*|*msys*|*cygwin*) echo windows ;;
        *)
            echo "Error: unsupported host OS: $uname_value" >&2
            return 1
            ;;
    esac
}

TARGET_OS=${K230_FLASH_TARGET_OS:-$(detect_host_os)}
case "$TARGET_OS" in
    linux|macos|windows) ;;
    *)
        echo "Error: K230_FLASH_TARGET_OS must be linux, macos, or windows." >&2
        exit 1
        ;;
esac

# GitHub environment secrets can contain a trailing newline when pasted from
# a file. These values are passed to CMake and macOS tools, where that newline
# becomes part of the identity, keychain path, or notarization profile.
if [[ "$TARGET_OS" == "macos" ]]; then
    MACOS_SIGN_IDENTITY=$(printf '%s' "${MACOS_SIGN_IDENTITY:-}" | tr -d '\r\n')
    MACOS_KEYCHAIN=$(printf '%s' "${MACOS_KEYCHAIN:-}" | tr -d '\r\n')
    MACOS_NOTARY_PROFILE=$(printf '%s' "${MACOS_NOTARY_PROFILE:-}" | tr -d '\r\n')
    export MACOS_SIGN_IDENTITY MACOS_KEYCHAIN MACOS_NOTARY_PROFILE
fi

for command_name in cmake; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Error: required command not found: $command_name" >&2
        exit 1
    fi
done

if [[ -z "${CMAKE_GENERATOR:-}" ]]; then
    if command -v ninja >/dev/null 2>&1; then
        CMAKE_GENERATOR=Ninja
    else
        CMAKE_GENERATOR="Unix Makefiles"
    fi
elif [[ "$CMAKE_GENERATOR" == "Ninja" ]] && ! command -v ninja >/dev/null 2>&1; then
    echo "Error: Ninja was selected but the ninja command was not found." >&2
    exit 1
fi

if [[ -n "${K230_FLASH_REVISION:-}" ]]; then
    REVISION=$K230_FLASH_REVISION
elif command -v git >/dev/null 2>&1; then
    if git -C "$REPO_ROOT" submodule status --recursive | grep -q '^-'; then
        echo "Error: Git submodules are not initialized." >&2
        echo "Run: git submodule update --init --recursive" >&2
        exit 1
    fi
    REVISION=$(git -C "$REPO_ROOT" describe --long --tags --dirty --always 2>/dev/null || echo unknown)
else
    REVISION=${K230_FLASH_REVISION:-}
    if [[ -z "$REVISION" ]]; then
        echo "Error: git is unavailable; set K230_FLASH_REVISION explicitly." >&2
        exit 1
    fi
fi
SAFE_REVISION=$(printf '%s' "$REVISION" | tr '/ :' '---' | tr -cd '[:alnum:]_.+-')
if [[ "$TARGET_OS" == "macos" ]]; then
    MACOS_ARCHITECTURES=${MACOS_ARCHITECTURES:-$(uname -m)}
    DEFAULT_TARGET_ARCH=${MACOS_ARCHITECTURES//;/+}
    if [[ -z "${MACOS_SIGN_IDENTITY:-}" && "${MACOS_ALLOW_UNSIGNED:-0}" == "1" ]]; then
        DEFAULT_TARGET_ARCH+="-unsigned"
    fi
else
    DEFAULT_TARGET_ARCH=$(uname -m)
fi
TARGET_ARCH=${K230_FLASH_TARGET_ARCH:-$DEFAULT_TARGET_ARCH}
SAFE_TARGET_ARCH=$(printf '%s' "$TARGET_ARCH" | tr '; /:' '----')
BUILD_DIR=${K230_FLASH_BUILD_DIR:-"$REPO_ROOT/build/release/$TARGET_OS"}
DIST_DIR=${K230_FLASH_DIST_DIR:-"$REPO_ROOT/dist"}
PACKAGE_NAME="k230_flash_cli-${TARGET_OS}-${SAFE_TARGET_ARCH}-${SAFE_REVISION}"
STAGING_DIR=$(mktemp -d "${TMPDIR:-/tmp}/k230_flash-release.XXXXXX")
PACKAGE_ROOT="$STAGING_DIR/$PACKAGE_NAME"

case "$TARGET_OS" in
    linux) ARCHIVE_PATH="$DIST_DIR/${PACKAGE_NAME}.tar.gz" ;;
    macos|windows) ARCHIVE_PATH="$DIST_DIR/${PACKAGE_NAME}.zip" ;;
esac
ARCHIVE_FILENAME=${ARCHIVE_PATH##*/}
CHECKSUM_PATH="${ARCHIVE_PATH}.sha256"

cleanup() {
    if [[ -n "${STAGING_DIR:-}" && -d "$STAGING_DIR" ]]; then
        cmake -E remove_directory "$STAGING_DIR"
    fi
}
trap cleanup EXIT

for output_path in "$ARCHIVE_PATH" "$CHECKSUM_PATH"; do
    if [[ -e "$output_path" ]]; then
        echo "Error: artifact already exists: $output_path" >&2
        echo "Remove it explicitly before rebuilding the same revision." >&2
        exit 1
    fi
done

CMAKE_ARGS=(
    -S "$REPO_ROOT"
    -B "$BUILD_DIR"
    -G "$CMAKE_GENERATOR"
    -DCMAKE_BUILD_TYPE=Release
)

if [[ -n "${CMAKE_TOOLCHAIN_FILE:-}" ]]; then
    CMAKE_ARGS+=("-DCMAKE_TOOLCHAIN_FILE=$CMAKE_TOOLCHAIN_FILE")
fi
if [[ -n "${BUILD_WITH_MINGW:-}" ]]; then
    CMAKE_ARGS+=("-DBUILD_WITH_MINGW=$BUILD_WITH_MINGW")
fi

PACKAGE_PATHS=${CMAKE_PREFIX_PATH:-}
if [[ "$TARGET_OS" == "macos" ]]; then
    SIGN_IDENTITY=${MACOS_SIGN_IDENTITY:-}
    if [[ -z "$SIGN_IDENTITY" && "${MACOS_ALLOW_UNSIGNED:-0}" != "1" ]]; then
        echo "Error: MACOS_SIGN_IDENTITY is required for a macOS release." >&2
        echo "For CI-only validation, explicitly set MACOS_ALLOW_UNSIGNED=1." >&2
        exit 1
    fi
    if [[ -n "$SIGN_IDENTITY" ]] && ! command -v security >/dev/null 2>&1; then
        echo "Error: required command not found: security" >&2
        exit 1
    fi
    if [[ -n "$SIGN_IDENTITY" ]]; then
        if [[ -n "${MACOS_KEYCHAIN:-}" ]]; then
            identities=$(security find-identity -v -p codesigning "$MACOS_KEYCHAIN")
        else
            identities=$(security find-identity -v -p codesigning)
        fi
        if ! grep -F -- "$SIGN_IDENTITY" <<<"$identities" >/dev/null; then
            echo "Error: signing identity was not found: $SIGN_IDENTITY" >&2
            printf '%s\n' "$identities" >&2
            exit 1
        fi
    fi

    CMAKE_ARGS+=(
        "-DCMAKE_OSX_ARCHITECTURES=$MACOS_ARCHITECTURES"
        "-DK230_FLASH_MACOS_SIGN_IDENTITY=$SIGN_IDENTITY"
        "-DK230_FLASH_MACOS_NOTARY_PROFILE=${MACOS_NOTARY_PROFILE:-}"
        "-DK230_FLASH_MACOS_KEYCHAIN=${MACOS_KEYCHAIN:-}"
        "-DK230_FLASH_MACOS_ARCHIVE_PATH=$ARCHIVE_PATH"
    )
    if [[ -n "${MACOS_DEPLOYMENT_TARGET:-}" ]]; then
        CMAKE_ARGS+=("-DCMAKE_OSX_DEPLOYMENT_TARGET=$MACOS_DEPLOYMENT_TARGET")
    fi
fi
if [[ -n "$PACKAGE_PATHS" ]]; then
    CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=$PACKAGE_PATHS")
fi

echo "Building k230_flash for $TARGET_OS ($TARGET_ARCH), revision $REVISION"
export GIT_COMMIT="$REVISION"
cmake "${CMAKE_ARGS[@]}"

if ! grep -F -- "-DTARGET_PLATFORM=$TARGET_OS" "$BUILD_DIR/cmake_install.cmake" >/dev/null; then
    echo "Error: CMake did not configure the '$TARGET_OS' install hook." >&2
    echo "The requested target does not match the configured toolchain; set CMAKE_TOOLCHAIN_FILE when cross-compiling." >&2
    exit 1
fi

cmake --build "$BUILD_DIR" --config Release --parallel

mkdir -p "$PACKAGE_ROOT"
cmake --install "$BUILD_DIR" --config Release --prefix "$PACKAGE_ROOT"

CLI_PATH="$PACKAGE_ROOT/bin/k230_flash_cli"
if [[ "$TARGET_OS" == "windows" ]]; then
    CLI_PATH="${CLI_PATH}.exe"
fi
if [[ ! -f "$CLI_PATH" ]]; then
    echo "Error: installed CLI was not found at $CLI_PATH" >&2
    exit 1
fi

if [[ "$TARGET_OS" != "macos" ]]; then
    mkdir -p "$DIST_DIR"
    case "$TARGET_OS" in
        linux)
            (cd "$STAGING_DIR" && cmake -E tar czf "$ARCHIVE_PATH" -- "$PACKAGE_NAME")
            ;;
        windows)
            (cd "$STAGING_DIR" && cmake -E tar cf "$ARCHIVE_PATH" --format=zip -- "$PACKAGE_NAME")
            ;;
    esac
fi

if [[ ! -f "$ARCHIVE_PATH" ]]; then
    echo "Error: release artifact was not created: $ARCHIVE_PATH" >&2
    exit 1
fi

(cd "$DIST_DIR" && cmake -E sha256sum "$ARCHIVE_FILENAME") > "$CHECKSUM_PATH"
cmake -E cat "$CHECKSUM_PATH"
echo "Created release artifact: $ARCHIVE_PATH"
echo "Created checksum: $CHECKSUM_PATH"
