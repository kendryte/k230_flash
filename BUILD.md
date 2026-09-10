# K230 Flash Build Guide

[English README](README_en.md) | 简体中文 README

This document is for developers and release maintainers. End users should use
the [README](README.md) and the published platform package.

## Local release build

Initialize submodules and run the platform release script:

```bash
git submodule update --init --recursive
./release.sh
```

Artifacts are written to `dist/`. Linux produces a `.tar.gz`; Windows and
macOS produce a `.zip`. Every archive has a matching `.sha256` file. Windows
cross-builds can set `CMAKE_TOOLCHAIN_FILE` and `K230_FLASH_TARGET_OS`.

## CI architectures

Release builds provide separate x86_64 and ARM64 packages for Linux, Windows,
and macOS. Architecture names are included in archive and artifact names.

- Linux: native builds on `ubuntu-22.04` (x86_64) and `ubuntu-22.04-arm`
  (ARM64), using Ubuntu 22.04 as the minimum release baseline. Ubuntu 20.04
  is not a supported target; no older-container or Qt source-build setup is used.
- Windows: LLVM-MinGW cross-builds on Linux, selecting `x86_64` or `aarch64`
  through `LLVM_MINGW_TARGET`. Each target bundles its matching runtime DLLs.
- macOS: Intel builds on `macos-15-intel`; ARM64 builds on the self-hosted Mac
  labeled `self-hosted`, `macOS`, `ARM64`, and `shenzhen_mac` to reduce hosted
  runner costs. Both architectures target macOS 13.0, independently of the
  runner's macOS version, and produce unsigned ZIP inputs without signing secrets.

On tags, a separate self-hosted `sign-macos` job verifies input checksums and
architectures, signs the relocated binaries, and notarizes the final ZIPs.
It does not compile code or execute the Intel CLI. Release upload waits for
both architectures and downloads only final artifacts, excluding `macos-unsigned-*`.
Branch runs retain unsigned validation artifacts and do not publish a release.
ARM64 build and signing jobs share a concurrency group within this repository.
The group uses `queue: max` so a newer branch build does not replace a pending
tag-signing job. The final release check requires all six platform/architecture
packages and verifies each matching checksum, rejecting stale duplicates and
unsigned inputs. Checksums are written with LF line endings on every platform.

## macOS signing

The macOS release script requires a Developer ID Application identity. The
certificate and private key must already be installed in the build account's
keychain.

```bash
security find-identity -v -p codesigning

MACOS_SIGN_IDENTITY="Developer ID Application: Example (TEAMID)" \
  ./release.sh
```

Optional variables:

- `MACOS_DEPLOYMENT_TARGET`: minimum macOS version (default `13.0`). Both CI
  architectures explicitly set it to `13.0`; a newer runner is still used to build.
- `MACOS_KEYCHAIN`: absolute path to a dedicated keychain. Leave empty to use
  the account's normal keychain search list.
- `MACOS_KEYCHAIN_PASSWORD`: password used by CI to unlock the signing
  keychain. Store it as a secret in the `macos-signing` environment.
- `MACOS_NOTARY_PROFILE`: the local `notarytool` keychain profile name. Leave
  empty to sign without notarization.

Create a notary profile on the signing Mac with `notarytool`, then pass the
profile name through `MACOS_NOTARY_PROFILE`:

```bash
xcrun notarytool store-credentials "K230Notary" \
  --apple-id "apple-id@example.com" \
  --team-id "TEAMID" \
  --password "app-specific-password"

MACOS_SIGN_IDENTITY="Developer ID Application: Example (TEAMID)" \
MACOS_NOTARY_PROFILE="K230Notary" \
  ./release.sh
```

Do not put Apple credentials, certificates, or private keys in the repository.

For validation-only unsigned builds, explicitly set `MACOS_ALLOW_UNSIGNED=1`.
Do not use unsigned artifacts for distribution.

The tag-only signing job requires all four `macos-signing` environment secrets:
`MACOS_SIGN_IDENTITY`, `MACOS_KEYCHAIN` (an explicit absolute path),
`MACOS_KEYCHAIN_PASSWORD`, and `MACOS_NOTARY_PROFILE`. Store the notarytool
profile in that same keychain under the runner account. CI unlocks the keychain,
configures signing-key access, and checks the profile before processing inputs.
The signing Mac needs CMake and Xcode command-line tools, but no target compiler
or Qt installation. Install these prerequisites before registering the runner.

The CMake install hook in `src/cli/cmake/mac-install.cmake` verifies
dependencies, signs installed binaries, creates the archive, and optionally
submits the archive for notarization. The CI path requires an `Accepted` result
before publishing a ZIP and its checksum. ZIP archives and standalone CLI
binaries are not stapled; this differs from the GUI's stapled app/DMG flow.

Automated release configuration is maintained in
`.github/workflows/build.yml`. Keep workflow changes under maintainer review.

## Platform install hooks

The top-level CMake install step invokes the platform helper after installation:

- `win-install.cmake` validates the Windows layout and bundles LLVM-MinGW DLLs.
- `linux-install.cmake` validates `$ORIGIN` RPATH and shared-library resolution.
- `mac-install.cmake` validates dependencies and handles signing and packaging.

Raw image addresses passed to the CLI must be aligned to the selected medium's
erase size. Writes do not reboot the board by default; pass `--auto-reboot` to
request a reboot after a successful write.
