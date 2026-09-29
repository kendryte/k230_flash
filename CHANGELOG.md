# Changelog

## 0.0.12 - 2026-09-28

### Added

- Added public OTP programming with a dedicated embedded OTP loader.
- Added optional device-side SHA-256 readback verification through
  `flash --verify`.
- Added `--version` output and startup logging with the build commit ID.
- Added regression coverage for KDImage parsing and validation.

### Changed

- Stream KDImage partitions directly from the source image instead of
  extracting temporary files, with 64-bit offset and size handling.
- Updated the embedded MMC, OTP, SPI NAND, and SPI NOR loaders.

### Fixed

- Validate KDImage header and partition-table checksums, metadata, source
  hashes, and partition ranges before burning.
- Harden loader communication against stale USB responses, synchronization
  failures, invalid transfer ranges, and probe timeouts.

## 0.0.11 - 2026-09-11

### Fixed

- Use complete physical USB topology paths so devices connected through
  different hub branches no longer receive the same device address.
- Skip devices whose physical USB path is unavailable instead of exposing an
  ambiguous fallback address.

### Added

- Added regression coverage for direct, hub-connected, maximum-depth, invalid,
  and truncated USB port paths.

## 0.0.10 - 2026-09-09

### Added

- Refactored the command interface around `devices`, `flash`, `read`, and
  `erase` subcommands. Flash accepts positional `ADDRESS FILE` pairs.
- Added signed macOS release packages with optional notarization support.
- Added SHA-256 checksum files for release archives.
- Added Chinese documentation alongside the English README.

### Changed

- Restricted release packages to authorized version-tag builds.
- Moved developer build and signing instructions from the user README to
  `BUILD.md`.
