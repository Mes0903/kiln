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
./build/dmake <path-to-project>  # e.g., ./build/dmake test_project
```

**Dependencies:**
- CMake 3.15+
- C++23 compiler
- CLI11 (for argument parsing)
- Catch2 v3 (for testing)

## Architecture

### Three-Layer Design

1. **Parser Layer** (`dmake/cmake-language.{hpp,cpp}`)
   - Parses CMake syntax into an Abstract Syntax Tree (AST)
   - Returns `std::expected<std::vector<AstNode>, ParseError>`
   - Tracks location (row/col) for error reporting
   - AST nodes: `CommandInvocation`, `IfBlock`
   - Handles CMake features: variable references `${VAR}`, quoted/unquoted arguments, comments (line and bracket)

2. **Interpreter Layer** (`dmake/interperter.{hpp,cpp}`)
   - Executes the AST using a builtin function system
   - Returns `std::expected<void, InterpreterError>` for error propagation
   - Manages variables, targets, and call stack for subdirectories
   - Built-in commands: `message`, `set`, `add_executable`, `add_library`, `target_include_directories`, `target_link_libraries`, `set_target_properties`, `add_subdirectory`, `include`
   - Target types: `ExecutableTarget`, `LibraryTarget` (shared/static)

3. **CLI Layer** (`dmake-cli/main.cpp`)
   - Entry point using CLI11 for argument parsing
   - Orchestrates parsing → interpretation → building
   - Pretty-prints errors with file location and context (Rust-style error formatting)

### Error Handling Pattern

All errors use `std::expected` for proper propagation:

- **Parse errors**: `ParseError{row, col, reason}` from parser
- **Interpreter errors**: `InterpreterError{file, row, col, message}` from interpreter
- **Fatal errors**: Builtins call `set_fatal_error()` instead of throwing or exiting
- The CLI uses a lambda `print_error_context()` to display errors with colored output and source line context

### Key Design Decisions

**Parent-child interpreter hierarchy**: `add_subdirectory()` creates a new `Interpreter` with a parent pointer to share builtins and propagate errors while maintaining separate variable scopes via a call stack.

**include() vs add_subdirectory()**:
- `include()` executes in the current scope (shares variables)
- `add_subdirectory()` creates a new scope with parent access

**Builtin function signature**: `std::function<void(const std::vector<Argument>&)>` - builtins signal fatal errors via `set_fatal_error()` rather than return values, checked after each builtin call in `execute_command()`.

**Target build ordering**: Libraries are built before executables (see `run_build()` in interperter.cpp:394-420) to ensure dependencies are available.

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
