#!/usr/bin/env bash
set -euo pipefail
if [[ $# != 4 ]]; then
    echo "Usage: $0 ARCH REVISION INPUT_DIR WORK_DIR" >&2
    exit 2
fi
arch=$1
revision=$2
input=$3
work=$4
case "$arch" in x86_64|arm64) ;; *) exit 2 ;; esac
case "$revision" in ''|*[!a-zA-Z0-9._+-]*) exit 2 ;; esac
: "${MACOS_SIGN_IDENTITY:?Signing identity required}"
: "${MACOS_KEYCHAIN:?Signing keychain required}"
: "${MACOS_NOTARY_PROFILE:?Notarization profile required}"
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)

unsigned="k230_flash_cli-macos-$arch-unsigned-$revision"
package="k230_flash_cli-macos-$arch-$revision"
(cd "$input" && shasum -a 256 -c "$unsigned.zip.sha256")
mkdir -p "$work/$arch" "$work/output"
ditto -x -k "$input/$unsigned.zip" "$work/$arch"
prefix="$work/$arch/$package"
mv "$work/$arch/$unsigned" "$prefix"
for binary in k230_flash_cli libkburn.dylib libusb-1.0.dylib; do
    lipo "$prefix/bin/$binary" -verify_arch "$arch"
done

# Reuse the install hook on the relocated bundle; no compilation is performed.
archive="$work/$arch/$package.zip"
cmake -DTARGET_PLATFORM=macos "-DINSTALL_PREFIX=$prefix" \
    -DEXECUTABLE_NAME=k230_flash_cli \
    "-DSIGN_IDENTITY=$MACOS_SIGN_IDENTITY" "-DKEYCHAIN_PATH=$MACOS_KEYCHAIN" \
    "-DNOTARY_PROFILE=$MACOS_NOTARY_PROFILE" "-DARCHIVE_PATH=$archive" \
    -P "$repo/src/cli/cmake/mac-install.cmake"
test -f "$archive"
mv "$archive" "$work/output/$package.zip"
(cd "$work/output" && shasum -a 256 "$package.zip" > "$package.zip.sha256")
