# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

dmake is a modern, fast build system written in C++23 that uses CMake's language as input but provides a more predictable and faster build experience. It features:
- **Cargo-style multi-config builds** with separate `build/<config>/` directories
- **Parallel, incremental builds** with signature-based caching
- **No configure step** - interprets CMakeLists.txt directly on every build
- **CMake compatibility** - mimics CMake behavior for easy project migration
- **Experimental C++20 modules support** with scan-at-build architecture

## Build System

**Build the project:**
```bash
mkdir -p build && cd build && cmake .. && make
```

**Run unit tests:**
```bash
./build/dmake_tests
```

**Run integration tests:**

```bash
# Requires path to dmake binary
./tests/integration/run_tests.sh ./build/dmake
```

**Adding integration tests:**
1. Create a new directory in `tests/integration/<test-name>`
2. Add a `CMakeLists.txt` and source files
3. Add a `test.sh` script that:
   - Takes the dmake binary path as `$1`
   - Executes build commands
   - Verifies outputs/behavior (return 0 for success)

**Run a specific test:**
```bash
./build/dmake_tests "[test-name]"  # e.g., ./build/dmake_tests "[interpreter]"
```

**Run dmake CLI:**
```bash
# Default: build current directory
dmake [targets...] [-j N] [-DVAR=VAL] [-DVAR:TYPE=VAL] [--config CONFIG]

# Subcommands
dmake build [project] [targets...]   # Build specific project/targets
dmake run <target> [-- args...]      # Build and run a target
dmake test [pattern]                 # Build and run tests (parallel)
dmake clean                          # Clean build directory

# Script mode
dmake -P <script.cmake> [-DVAR=VAL]

# CMake tool mode
dmake -E <command> [args...]         # CMake-compatible tool commands

# Variable definitions (CMake-compatible)
-DVAR=value              # Set variable to value
-DVAR:TYPE=value         # Set variable with type annotation (type is stripped)
-DVAR                    # Set variable to ON (boolean flag)
```

```bash
./build/dmake                           # Build in build/debug
./build/dmake my_lib --config release -j 8
./build/dmake run my_app -- --verbose
./build/dmake test parser
./build/dmake -E copy file.txt dest/
```

**Dependencies:**
- CMake 3.15+ (for bootstrapping only)
- C++23 compiler (g++ with `-std=c++23`)
- CLI11 (header-only argument parsing)
- Catch2 v3 (unit testing framework)
- Glaze (header-only JSON library for compile_commands.json)

## Architecture

dmake consists of four main layers:

1. **Parser Layer** (`cmake-language.hpp/cpp`): Recursive descent parser for CMake syntax with support for nested variable references, control flow, and quoted/unquoted argument handling.
2. **Interpreter Layer** (`interperter.hpp/cpp`): AST execution engine with modular builtin registration system.
3. **Target System** (`target.hpp/cpp`): Unified target representation with transitive property resolution.
4. **Build System** (`build_system.hpp/cpp`): Parallel task execution with signature-based incremental builds.

### Target System

- **Unified Target Class**: All build units are represented by a single `Target` class with type differentiation.
- **Target Types**: `EXECUTABLE`, `SHARED_LIBRARY`, `STATIC_LIBRARY`, `OBJECT_LIBRARY`, `INTERFACE_LIBRARY`, `CUSTOM` (via `CustomTarget` subclass).
- **Property Model**: Supports `PUBLIC`, `PRIVATE`, and `INTERFACE` visibility for properties (includes, definitions, options, libraries).
- **Transitive Resolution**: `Target::resolve()` implements recursive dependency resolution with cycle detection, merging properties across the dependency graph.
- **Task Generation**: `Target::generate_tasks()` produces granular `BuildTask`s for the build graph based on target type.

**Resolved Properties:**
- `INCLUDE_DIRECTORIES`, `COMPILE_DEFINITIONS`, `COMPILE_OPTIONS`
- `LINK_DIRECTORIES`, `LINK_LIBRARIES` (order-preserving)
- `PRECOMPILE_HEADERS`

**File Sets**: Named collections (HEADERS, CXX_MODULES) with visibility. CXX_MODULES auto-mark sources as module interfaces.

**target_sources()**: Basic form adds sources with visibility. FILE_SET form organizes files. Validates CXX_MODULES scope and file existence.

**target_compile_features()**: Specifies required compiler features. Auto-sets minimum language standard. Supports meta-features (cxx_std_*, c_std_*) and individual features. PUBLIC/INTERFACE propagate.

### Build System

- **Signature-Based Caching**: Task signatures include command, compiler version, dmake version, and all input file timestamps (including discovered headers).
- **Header Dependency Tracking**: Parses `.d` files (Make-style dependency files) or runs `g++ -H -E` as fallback.
- **Stat Cache**: Minimizes filesystem I/O by caching file modification times.
- **Parallel Execution**: Thread-per-task model with dependency-aware scheduling and configurable job limits.
- **Output Capture**: Buffers stdout/stderr to prevent interleaving, preserves colors with `-fdiagnostics-color=always`.
- **Dynamic Dependencies**: Build graph supports mutation at runtime for C++20 module dependencies.
- **Compile Commands Export**: Generates `compile_commands.json` for IDE integration.

### Test Runner

`dmake test` runs tests in parallel with output buffering. Supports regex filtering. Requires `enable_testing()` and `add_test()`.

**Test Properties** (via `set_tests_properties()`):
- **TIMEOUT**: Maximum execution time in seconds. Tests exceeding this duration are marked as TIMEOUT and fail.
- **SKIP_RETURN_CODE**: Exit code that indicates test was skipped (commonly 77). Skipped tests count as passed.
- Unsupported properties generate warnings but are stored for future use.

Test results: PASSED (green), FAILED (red), SKIPPED (yellow), TIMEOUT (magenta).

### Command Parser Utility

`CommandParser` (`dmake/command_parser.hpp`) provides builder-style API for parsing CMake commands:
- `add_positional`, `add_flag`, `add_value`, `add_list`, `add_multi_list`, `add_default_list`
- Use `PARSE_OR_RETURN(parser, interp, args)` macro for validation

## CMake Language Support

dmake implements a comprehensive subset of CMake language features. The interpreter follows CMake semantics for variable handling and control flow.

### Parser Features

The parser is a **recursive descent parser** supporting:

**Variable References:**
- Simple: `${VAR}`
- Nested: `${${PREFIX}_SUFFIX}`, `${${VAR}}`
- Environment: `$ENV{PATH}`
- Cache: `$CACHE{VAR}`

**Argument Types:**
- **Unquoted**: Variable expansion, whitespace splitting, escape sequences
- **Quoted**: Single value, variable expansion, escape sequences, preserves whitespace
- **Bracket**: Multi-line strings, no expansion, syntax: `[=[content]=]`

**Control Flow:**
- `if(condition)/elseif(condition)/else()/endif()`
- `foreach(var ...)/endforeach()` - Simple, RANGE, IN LISTS/ITEMS variants
- `while(condition)/endwhile()`
- `function(name args...)/endfunction()` - Creates new scope, ARGN support
- `macro(name args...)/endmacro()` - Text substitution, no new scope
- `return()` - Early exit from function/file
- `break()`, `continue()` - Loop control

**Scoping:**
- Each `add_subdirectory()` creates child scope
- Functions create new scope, macros don't
- `set(VAR value PARENT_SCOPE)` modifies parent scope
- Global scope accessible from all contexts

### Supported Builtin Commands

**Project & Configuration:**
- `project()` - Set project name and version
- `cmake_minimum_required()` - Version requirement check
- `option()` - Define cache variable with ON/OFF value
- `set()`, `unset()` - Variable manipulation with PARENT_SCOPE support
- `mark_as_advanced()` - Mark variables as advanced

**Targets:**
- `add_executable()`, `add_library()` - Create build targets
- `add_custom_target()` - Custom targets with COMMAND/DEPENDS/ALL
- `target_sources()` - Add sources to targets with FILE_SET support (HEADERS, CXX_MODULES)
- `target_include_directories()`, `target_compile_definitions()`, `target_compile_options()`
- `target_compile_features()` - Specify required compiler features (C++11/14/17/20/23/26, C99/11/17/23)
- `target_link_libraries()`, `target_link_directories()`
- `target_precompile_headers()` - PCH support
- `set_target_properties()`, `get_target_property()`

**Directory Management:**
- `add_subdirectory()` - Process subdirectories with isolated scope
- `include_directories()`, `link_directories()` - Global directory settings
- `include()` - Include another CMake file
- `aux_source_directory()` - Find all source files in directory

**Find Commands:**
- `find_file()`, `find_path()`, `find_library()`, `find_program()`
- `find_package()` - Module and Config mode with version checking

**String Operations:**
- `string(REPLACE/TOUPPER/TOLOWER/SUBSTRING/LENGTH/STRIP/FIND/COMPARE/REGEX)`

**List Operations:**
- `list(APPEND/LENGTH/GET/FIND/SORT/REVERSE/REMOVE_DUPLICATES/SUBLIST/TRANSFORM/FILTER/...)`

**File Operations:**
- `file(READ/WRITE/GLOB/GLOB_RECURSE/MAKE_DIRECTORY/REMOVE/COPY/RENAME/...)`

**Control Flow:**
- `if()/elseif()/else()/endif()` - Conditional execution
- `foreach()/endforeach()` - Iteration (simple, RANGE, IN LISTS/ITEMS)
- `while()/endwhile()` - Loops
- `function()/endfunction()`, `macro()/endmacro()` - User-defined commands
- `return()`, `break()`, `continue()` - Flow control

**Process Execution:**
- `execute_process()` - Run external commands with pipeline support, timeout, output capture

**Testing:**
- `enable_testing()`, `add_test()` - Test registration
- `set_tests_properties()` - Set test properties (TIMEOUT, SKIP_RETURN_CODE supported)
- `try_compile()` - Test compilation of source code with aggressive caching
- `try_run()` - Compile and execute test programs (caches compilation, not execution)

**Math & Messaging:**
- `math(EXPR)` - Arithmetic expression evaluation
- `message()` - Output with STATUS/WARNING/FATAL_ERROR modes

### If Conditions

The `if()` command uses a recursive descent parser with proper operator precedence matching CMake's behavior.

**Key Features**:
- Automatic variable dereferencing for unquoted non-keyword arguments
- Proper operator precedence: unary tests > binary comparisons > NOT > AND > OR
- CMake-compatible truthiness: falsy = empty, `0`, `OFF`, `NO`, `FALSE`, `N`, `IGNORE`, `*-NOTFOUND`
- Operators: logical (AND/OR/NOT), numeric (EQUAL/LESS/GREATER), string (STREQUAL/STRLESS), version comparisons, unary tests (DEFINED/TARGET), file tests (EXISTS/IS_DIRECTORY)

**Implementation**: `evaluate_condition()` in `interperter.cpp`

## Multi-Configuration Build Support

Cargo-style separate directories: `build/{debug,release,relwithdebinfo,minsizerel}/`. Structure: `build_root/config` where config defaults to "debug". CMAKE_BUILD_TYPE set automatically and normalized. Each config has own `.dmake_cache`.

**Standard configs**: debug (`-g -O0`), release (`-O3 -DNDEBUG`), relwithdebinfo (`-g -O2 -DNDEBUG`), minsizerel (`-Os -DNDEBUG`). Override via CMakeLists.txt or `-D` flag.

## Advanced Features

### C++20 Modules (Experimental)
Three-stage scan-at-build pipeline: 1) Scanner tasks extract module/import declarations to `.ddi` files, 2) Collator resolves modules and dynamically injects dependencies, 3) Compile tasks use module mapper. Auto-detected by extension (`.cppm`, `.ixx`, `.ccm`).

### Precompiled Headers
Auto-generates PCH wrapper at `<binary_dir>/objs/<target>_pch.hpp` containing all PRECOMPILE_HEADERS. Applied via `-include` to all sources. Incremental regeneration.

### Find Package System
Module mode searches for `Find<Package>.cmake`. Config mode searches for `<Package>Config.cmake`. Supports version checking and sets standard variables.

### Custom Targets
`add_custom_target()` supports multiple sequential COMMANDs, DEPENDS, ALL flag, WORKING_DIRECTORY, COMMENT.

### Directory Scan Caching
Caches directory listings with mtime validation for `find_*()` commands. O(1) lookups.

### Toolchain Abstraction
Abstract compiler interface with GnuCompiler implementation. Per-language compiler selection. Extensible for clang, MSVC.

### CMake Tool Mode
`dmake -E <command>` provides CMake-compatible commands: echo, touch, remove, make_directory, copy, rename.

## Incremental & Parallel Build System

- Thread-per-task model with `-j` flag (defaults to hardware threads)
- Output buffered to prevent interleaving, colors preserved with `-fdiagnostics-color=always`
- Task signatures in `<build_dir>/.dmake_cache` include command, compiler version, dmake version, input mtimes
- Header discovery via `.d` files or `g++ -H -E` fallback
- Stat cache minimizes filesystem I/O

## Centralized Cache System

JSON-based cache at `<build_dir>/.dmake_subsystem_cache.json` using Glaze serialization. Thread-safe with mutex protection and atomic writes. Subsystems: `TryCompile`, `FileListing`. Implementation: `dmake/cache_store.hpp/cpp`

### try_compile() Implementation

Implements CMake's `try_compile` command in SOURCE mode with aggressive caching.

**Supported**:
- Source variants: `SOURCES`, `SOURCE_FROM_CONTENT`, `SOURCE_FROM_VAR`, `SOURCE_FROM_FILE`
- Compilation options: `COMPILE_DEFINITIONS`, `CXX_STANDARD`, `C_STANDARD`
- Linking: `LINK_LIBRARIES`, `LINK_OPTIONS`, `CMAKE_FLAGS`
- Output capture: `OUTPUT_VARIABLE`

**Caching**:
- Signature includes compiler, source content (Blake2b), definitions, libraries, and header dependencies (with mtimes)
- Cache validated by checking header mtimes
- Temp directory preserved on failure for debugging

**Implementation**: `dmake/builtins/try_compile.cpp`

### try_run() Implementation

Implements CMake's `try_run` command for compiling and executing test programs.

**Supported**:
- Same source variants and compilation options as `try_compile`
- Execution control: `WORKING_DIRECTORY`, `ARGS`
- Output capture: `COMPILE_OUTPUT_VARIABLE`, `RUN_OUTPUT_VARIABLE`
- Cross-compilation support with `CMAKE_CROSSCOMPILING_EMULATOR`

**Caching**:
- Compilation is cached (same as `try_compile`)
- Execution is NEVER cached - runs every time if compilation succeeds

**Implementation**: `dmake/builtins/try_compile.cpp`

## Error Handling

- **Stalls**: Prints remaining tasks and missing dependencies
- **Cycles**: Detected during graph construction with full path reported
- **System errors**: Compiler failures are fatal, cache errors have detailed messages

## Testing

### Unit Tests
Catch2 v3 tests in `tests/interpreter_tests.cpp`, `tests/language_tests.cpp`, etc. Use `run_script()` helper for interpreter tests. Run with `./build/dmake_tests "[test-name]"`.

### Integration Tests
Location: `tests/integration/*/`. Each subdirectory has `CMakeLists.txt`, sources, and `test.sh` that takes dmake path and verifies outputs (exit 0 on success). Run with `./tests/integration/run_tests.sh ./build/dmake`.

## Self-Hosting

dmake can build itself. Once you have an initial binary: `./build/dmake` (output in `build/debug/dmake`).

## Compile Features System

The compile features system (`dmake/compile_features.hpp/cpp`) provides CMake-compatible compiler feature detection and automatic standard selection.

- Singleton database with C/C++ meta-features and individual features
- Automatic standard deduction (e.g., `cxx_lambdas` → C++11)
- Features validated and stored in `COMPILE_FEATURES` property
- PUBLIC/INTERFACE features propagate through dependency graph
- Standard selection: `max(explicit_standard, feature_required_standard)`

## Code Locations

### Core Components

**Parser**: `dmake/cmake-language.hpp/cpp`
- Recursive descent parser with manual position tracking
- Entry point: `Parser::parse()` - returns AST or error
- Modify `parse_*` methods to change grammar

**Interpreter**: `dmake/interperter.hpp/cpp`
- AST execution engine with builtin registry
- Variable scopes with parent/child relationships
- If condition evaluation: `evaluate_condition()` method (recursive descent with proper precedence)

**Builtins**: Modular registration in `dmake/builtins/*.cpp`
- `message.cpp`, `variable.cpp`, `list.cpp`, `string.cpp`, `math.cpp`
- `target.cpp` - target creation and configuration including `target_sources()` with FILE_SET support
- `project.cpp`, `file.cpp`, `find_commands.cpp`, `find_package.cpp`
- `process.cpp`, `test.cpp`
- **Register in**: `Interpreter` constructor, inside `if (parent_ == nullptr)` block for root-only commands

**Target System**: `dmake/target.hpp/cpp`
- Unified `Target` class with type enum
- File Sets: `FileSet` struct stores organized file collections (HEADERS, CXX_MODULES)
- `add_file_set()` adds file sets to targets
- `is_in_cxx_modules_file_set()` checks if source is in CXX_MODULES file set
- Property resolution: `Target::resolve()` - handles transitive dependencies
- Task generation: `Target::generate_tasks()` - creates BuildTasks for graph
- Custom targets: `CustomTarget` subclass

**Build System**: `dmake/build_system.hpp/cpp`
- `BuildGraph` class - task DAG with parallel execution
- Signature-based caching in `.dmake_cache`
- Header dependency tracking via `.d` files or `g++ -H -E`

**CLI**: `dmake-cli/main.cpp`
- Argument parsing with CLI11
- Error output formatting: `print_error_context()` lambda
- Verb-based subcommands: build, run, test, clean

### Key Utilities

**CommandParser**: `dmake/command_parser.hpp`
- Builder-style API for parsing CMake command arguments
- Usage: `PARSE_OR_RETURN(parser, interp, args)` macro

**CMakeList**: `dmake/cmake_list.hpp`
- Semicolon-separated list operations
- Used throughout for list variable manipulation

**LanguageClassifier**: In `dmake/target.cpp`
- `from_path()` - detects file language (C, C++, headers, module interfaces)

**FileCache**: `dmake/file_cache.hpp`
- Directory scan caching for `find_*()` commands
- Mtime-based invalidation

**Module Scanner**: `dmake/module_scanner.hpp/cpp`
- C++20 module dependency extraction
- `.ddi` file generation and parsing

**CacheStore**: `dmake/cache_store.hpp/cpp`
- Centralized JSON-based persistent cache
- Template-based subsystem API with CacheSubsystem enum
- Glaze serialization for structured data
- Thread-safe operations with atomic file writes

**try_compile**: `dmake/builtins/try_compile.cpp`
- SOURCE mode implementation with all variants
- Transparent signature computation (human-readable)
- Header dependency tracking via .d files
- Cache validation with mtime checks

### Adding New Features

**New builtin**: Create in `dmake/builtins/`, implement as lambda with `CommandParser`, register in `Interpreter` constructor

**New target property**: Add to `Target` class, update `resolve()` if transitive, modify `generate_tasks()` for usage

**New if() operator**: Add keyword in `interperter.cpp`, modify `evaluate_condition()` parser

## Key Implementation Notes

**Build Graph**:
- DFS-based cycle detection with full path reporting
- Ready queue with parallel dispatch up to job limit
- Dynamic dependency injection for C++20 modules

**Property Resolution**:
- `Target::resolve()` implements depth-first transitive resolution with cycle detection
- Merges PRIVATE/PUBLIC/INTERFACE properties across dependency graph

**Signatures**:
- Blake2b hash of: command, compiler version, dmake version, input file mtimes
- Header discovery via `.d` files or `g++ -H -E` fallback

**Design Decisions**:
- No configure step - interprets CMakeLists.txt on every build
- Multi-config by default with Cargo-style `build/<config>/` structure
- Signature-based caching catches flag changes, more reliable than timestamp-only
- CMake compatibility first - semantic compatibility over feature parity

## Some food for your own thought
* (when writing code) Do not write code before stating assumptions.
* (when writing code) Produce code you wouldn't want to debug at 3am
* Always state assumptions before implementing.
* Do not claim correctness you haven't verified.
* Do not handle only the happy path.
* Under what conditions does this work?
* Solve problems you weren't asked to solve unless instrumental
* Import complexity you don't need
