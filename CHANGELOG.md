# Changelog

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
