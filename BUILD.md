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

The CMake install hook in `src/cli/cmake/mac-install.cmake` verifies
dependencies, signs installed binaries, creates the archive, and optionally
submits and staples the notarization result.

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
