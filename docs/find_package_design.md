# find_package(CONFIG) Design

Implementation plan for supporting `find_package(<PackageName> [version] [REQUIRED] [QUIET] [CONFIG])` in dmake.

## Overview
The `find_package` command in `CONFIG` mode looks for package configuration files provided by the package itself (typically `<PackageName>Config.cmake` or `<package-name>-config.cmake`).

## Core Logic
1.  **Argument Parsing**: Use `CommandParser` to extract:
    - `PackageName` (Required positional)
    - `Version` (Optional positional)
    - `CONFIG` / `NO_MODULE` (Flags)
    - `REQUIRED` (Flag)
    - `QUIET` (Flag)
2.  **Search Strategy**:
    - Check `CMAKE_PREFIX_PATH` (both as a dmake variable and environment variable).
    - Check standard system paths:
        - `/usr/lib/cmake/<PackageName>`
        - `/usr/local/lib/cmake/<PackageName>`
        - `/usr/share/cmake/<PackageName>`
        - (On macOS) `/opt/homebrew/lib/cmake/<PackageName>`
    - In each directory, look for:
        - `<PackageName>Config.cmake`
        - `<lowercased-name>-config.cmake`
3.  **Execution**:
    - If found, `include()` the file within the current interpreter scope.
    - Set `<PackageName>_FOUND` to `TRUE`.
    - Set `<PackageName>_DIR` to the directory containing the file.
4.  **Reporting**:
    - If not found and `REQUIRED`, fatal error.
    - If not found and NOT `QUIET`, print status message.

## Unhappy Paths & Edge Cases
- **Missing Package**: If `REQUIRED` is absent, set `<Package>_FOUND` to `FALSE` and continue.
- **Malformed Config**: Errors inside the loaded `.cmake` file must report the correct file path and line number using dmake's `InterpreterError` mechanism.
- **Version Mismatch**: Currently, version checking is **NOT implemented**. If a version is requested, dmake will print a warning and proceed with whatever version it finds.
- **Circular Dependencies**: If `AConfig.cmake` finds `B` which finds `A`, the existing `include` and `Interpreter` stack should prevent infinite recursion or report a stack overflow if it's too deep (though standard CMake allows re-inclusion in some cases).
- **Filesystem Permissions**: Skip directories that cannot be read without crashing.

## Assumptions
- We are focusing on `CONFIG` mode first. `MODULE` mode (searching for `FindXXX.cmake` in `CMAKE_MODULE_PATH`) is deferred.
- Variable names are case-sensitive matching CMake conventions (e.g., `PackageName_FOUND`).
- `CMAKE_PREFIX_PATH` is a semicolon-separated list.
