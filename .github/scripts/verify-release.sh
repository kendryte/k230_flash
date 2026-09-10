#!/usr/bin/env bash
set -euo pipefail
cd "${1:?Usage: verify-release.sh RELEASE_DIR}"
shopt -s nullglob

verify_package() {
    local digest recorded extra
    read -r digest recorded extra < "$1.sha256"
    recorded="${recorded%$'\r'}"
    if [[ ! "$digest" =~ ^[[:xdigit:]]{64}$ || "${recorded#\*}" != "$1" || -n "$extra" ]]; then
        echo "Checksum manifest does not identify $1" >&2
        return 1
    fi
    if [[ -n "$(sed -n '2,$p' "$1.sha256")" ]]; then
        echo "Expected a single checksum entry for $1" >&2
        return 1
    fi
    printf '%s  %s\n' "$digest" "$1" | sha256sum --check --strict
}

for path in *; do
    if [[ ! -f "$path" || -L "$path" || "$path" == *unsigned* ]]; then
        echo "Unexpected release entry: $path" >&2
        exit 1
    fi
done

for platform in linux windows macos; do
    extension=zip
    [[ "$platform" != linux ]] || extension=tar.gz
    for arch in x86_64 arm64; do
        packages=(k230_flash_cli-"$platform"-"$arch"-*."$extension")
        if [[ ${#packages[@]} -ne 1 ]]; then
            echo "Expected exactly one $platform $arch package" >&2
            exit 1
        fi
        verify_package "${packages[0]}"
    done
done

packages=(*.zip *.tar.gz)
checksums=(*.sha256)
if [[ ${#packages[@]} -ne 6 || ${#checksums[@]} -ne 6 ]]; then
    echo "Unexpected extra packages or checksums in release directory" >&2
    exit 1
fi
