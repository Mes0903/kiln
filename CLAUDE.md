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
./build/dmake <path-to-project> [-j N] [-DVAR=VAL]
```

**Dependencies:**
- CMake 3.15+
- C++23 compiler
- CLI11 (for argument parsing)
- Catch2 v3 (for testing)

## Architecture

### Three-Layer Design
...
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

**Modifying error output**: Update the `print_error_context` lambda in dmake-cli/main.cpp:45-71.

**Adding target properties**: Extend the `Target` class hierarchy (interperter.hpp:26-67) and update `set_target_properties` builtin.

**Parser grammar changes**: Modify `parse_*` methods in cmake-language.cpp. The parser is a recursive descent parser with manual position tracking.

## Some food for your own through
* (when writing code) Do not write code before stating assumptions.
* (when writing code) Produce code you wouldn't want to debug at 3am
* Do not claim correctness you haven't verified.
* Do not handle only the happy path.
* Under what conditions does this work?
* Solve problems you weren't asked to solve unless instrumental
* Import complexity you don't need
