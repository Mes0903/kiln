# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

dmake is a simplified CMake-like build system written in C++23. It parses a subset of CMake language syntax and builds C++ projects using g++.

## Build System

**Build the project:**
```bash
mkdir -p build && cd build && cmake .. && make
```

**Run tests:**
```bash
./build/dmake_tests
```

**Run a specific test:**
```bash
./build/dmake_tests "[test-name]"  # e.g., ./build/dmake_tests "[interpreter]"
```

**Run dmake CLI:**
```bash
./build/dmake <path-to-project> [-j N] [-DVAR=VAL] [--config CONFIG] [-B BUILD_DIR]
```

Examples:
```bash
# Default: build/debug
./build/dmake .

# Release build: build/release
./build/dmake . --config release

# Custom build root: out/debug
./build/dmake . -B out

# Custom root + config: out/release
./build/dmake . -B out --config release
```

**Dependencies:**
- CMake 3.15+
- C++23 compiler
- CLI11 (for argument parsing)
- Catch2 v3 (for testing)

## Architecture

### Three-Layer Design
...

## CMake Language Support

dmake implements a subset of CMake language features. The interpreter follows CMake semantics for variable handling and control flow.

### If Conditions

The `if()` command uses a recursive descent parser with proper operator precedence matching CMake's behavior. Reference: https://cmake.org/cmake/help/latest/command/if.html

**Operator Precedence** (highest to lowest):
1. Unary tests (`DEFINED`, `TARGET`, etc.)
2. Binary comparisons (`EQUAL`, `LESS`, `STREQUAL`, etc.)
3. `NOT` (prefix operator)
4. `AND` (no short-circuit evaluation)
5. `OR` (no short-circuit evaluation)

**Variable Dereferencing**:
- Unquoted arguments that are NOT keywords or numeric constants are automatically dereferenced as variables
- Undefined variables evaluate to empty string
- Quoted arguments are never dereferenced
- Keywords: `NOT`, `AND`, `OR`, `DEFINED`, `TARGET`, `EQUAL`, `LESS`, `GREATER`, `STREQUAL`, `STRLESS`, `STRGREATER`, `VERSION_*`, etc.

**Truthiness** (case-insensitive):
- Falsy: Empty string, `0`, `OFF`, `NO`, `FALSE`, `N`, `IGNORE`, `NOTFOUND`, `*-NOTFOUND`
- Everything else is truthy (including `ON`, `YES`, `TRUE`, `Y`, non-zero numbers, non-empty strings)

**Supported Operators**:
- Logical: `AND`, `OR`, `NOT`
- Numeric: `EQUAL`, `LESS`, `GREATER`, `LESS_EQUAL`, `GREATER_EQUAL`, `NOT_EQUAL`
- String: `STREQUAL`, `STRLESS`, `STRGREATER`, `STRLESS_EQUAL`, `STRGREATER_EQUAL`
- Version: `VERSION_EQUAL`, `VERSION_LESS`, `VERSION_GREATER`, `VERSION_LESS_EQUAL`, `VERSION_GREATER_EQUAL`
- Unary: `DEFINED` (checks if variable exists), `TARGET` (checks if target exists)
- File tests: `EXISTS`, `IS_DIRECTORY`, `IS_SYMLINK`, `IS_ABSOLUTE`
- Other: `MATCHES` (regex), `IN_LIST`

**Implementation**: The `evaluate_condition` method in `interperter.cpp:483-584` implements the full parser with proper precedence handling.

## Multi-Configuration Build Support

dmake supports Cargo-style multiple build configurations with separate output directories.

### Build Directory Structure

```
project/
  build/
    debug/         # Default configuration
      .dmake_cache
      *.o, executables, etc.
    release/
      .dmake_cache
      *.o, executables, etc.
    relwithdebinfo/
      .dmake_cache
      *.o, executables, etc.
```

### Configuration Logic

**Build directory determination:**
```
build_root = -B value OR <project_dir>/build (default)
config = --config value OR "debug" (default)
final_build_dir = build_root / config
```

**CMAKE_BUILD_TYPE variable:**
- Set automatically based on `--config` flag
- Normalized to proper case: "debug" → "Debug", "release" → "Release"
- Available to CMakeLists.txt scripts for conditional logic
- Can be overridden with `-DCMAKE_BUILD_TYPE=Custom`

### Standard Configurations

| Config | CMAKE_BUILD_TYPE | Typical Flags |
|--------|------------------|---------------|
| debug (default) | Debug | -g -O0 |
| release | Release | -O3 -DNDEBUG |
| relwithdebinfo | RelWithDebInfo | -g -O2 -DNDEBUG |
| minsizerel | MinSizeRel | -Os -DNDEBUG |

**Note**: Compiler flags must be set via `target_compile_options()` in CMakeLists.txt. dmake does not automatically apply flags based on CMAKE_BUILD_TYPE.

### Cache Isolation

Each configuration maintains its own `.dmake_cache` in its build directory:
- Prevents false cache hits between configurations
- Allows switching between debug/release without full rebuilds
- Signatures include all relevant flags and definitions

### Examples

```bash
# Build debug (default)
dmake .
# Output: ./build/debug/myapp

# Build release
dmake . --config release
# Output: ./build/release/myapp

# Build to custom location
dmake . -B out --config debug
# Output: out/debug/myapp

# Override build type
dmake . --config release -DCMAKE_BUILD_TYPE=Custom
# Build dir: ./build/release, but CMAKE_BUILD_TYPE="Custom"
```

## Incremental & Parallel Build System

**Concurrency**:
- Parallel task execution using a thread-per-task model with concurrency limits.
- **CLI Flags**: 
    - `-j` or `--parallel` to set the number of concurrent jobs (defaults to hardware thread count).
    - `-D` to define variables (e.g., `-DDEBUG=ON` or `-DENABLE_FEATURE`).
- **Thread Safety**: Global mutexes protect output and internal caches (`stat_cache_`, `compiler_version_cache_`).

**Captured Output**:
- Compiler output is captured and buffered to prevent interleaving.
- Colors are preserved using `-fdiagnostics-color=always` (when stdout is a TTY).
- Detailed logs are only printed on failure or for warnings.

**Cache Location**:
- `<build_dir>/.dmake_cache` stores task signatures.

**Signature Logic**:
- Task signatures include: command string, compiler version, dmake version, and timestamps of all input files (including discovered headers).
- **Header Discovery**:
    1. Fast path: Parse `.d` files generated during compilation.
    2. Fallback: Run `g++ -H -E` to scan for headers if `.d` files are missing.
- **Stat Cache**: `BuildGraph` maintains an internal cache of file timestamps to minimize disk I/O.

## Error Handling & Debugging

**Build Graph Stalls**:
- If the build cannot progress (e.g., due to an unresolvable dependency cycle), `dmake` will print a detailed list of remaining tasks and their missing dependencies.

**Circular Dependencies**:
- Detected during graph construction before any commands are executed.
- Reports the full path of the cycle (e.g., `libA -> libB -> libA`).

**System Errors**:
- Compiler execution failures (e.g., `g++` not found) during version check or header scanning are fatal and stop the build.
- Directory creation and cache writing errors are handled gracefully with detailed error messages.

## Testing

Tests use Catch2 and are located in `tests/interpreter_tests.cpp`. The test helper `run_script()` creates a minimal interpreter with a custom `message()` builtin that captures output.

To add tests:
1. Add a new `TEST_CASE` in `tests/interpreter_tests.cpp`
2. Use `run_script()` for simple interpreter tests
3. For parser tests, create a `Parser` directly and check `parse()` results

## Code Locations

**Adding new builtins**: In `Interpreter` constructor (interperter.cpp:121-354), add to the `if (parent_ == nullptr)` block to register only in root interpreter.

**If condition evaluation**: The `evaluate_condition` method (interperter.cpp:483-584) implements recursive descent parsing with proper CMake operator precedence. Modify this to add new operators or change evaluation semantics.

**Modifying error output**: Update the `print_error_context` lambda in dmake-cli/main.cpp:45-71.

**Adding target properties**: Extend the `Target` class hierarchy (interperter.hpp:26-67) and update `set_target_properties` builtin.

**Parser grammar changes**: Modify `parse_*` methods in cmake-language.cpp. The parser is a recursive descent parser with manual position tracking.

## Some food for your own thought
* (when writing code) Do not write code before stating assumptions.
* (when writing code) Produce code you wouldn't want to debug at 3am
* Do not claim correctness you haven't verified.
* Do not handle only the happy path.
* Under what conditions does this work?
* Solve problems you weren't asked to solve unless instrumental
* Import complexity you don't need
