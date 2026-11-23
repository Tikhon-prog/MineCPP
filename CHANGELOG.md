# Changelog

## 0.1.0 — First public implementation

- Created the C++20 CLI application and CMake build.
- Added local installed-version discovery and MineCPP profile listing.
- Added JAR inspection and ZIP/JAR extraction for stored and deflate entries.
- Added CRC validation and path traversal protection during extraction.
- Added transactional version import into an isolated MineCPP profile.
- Added local vanilla Java launch preparation using existing libraries, assets and native classifiers.
- Added offline player identity, legacy and modern game argument handling, and dry-run output.
- Added integration tests for ZIP import, unsafe paths, local dependencies, native extraction and offline launch arguments.
- Added GitHub Actions builds for Linux, Windows and macOS.
