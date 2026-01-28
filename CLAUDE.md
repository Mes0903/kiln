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

Examples:
```bash
# Build current directory in build/debug
./build/dmake

# Build specific target
./build/dmake my_lib

# Build release configuration with 8 jobs
./build/dmake --config release -j 8

# Build with variable definitions (CMake-compatible typed syntax)
./build/dmake -DCMAKE_CXX_FLAGS_DEBUG:STRING="-g -O0" -DENABLE_FEATURE:BOOL=ON

# Build and run with arguments
./build/dmake run my_app -- --verbose

# Run tests matching "parser"
./build/dmake test parser

# CMake tool mode
./build/dmake -E copy file.txt dest/
./build/dmake -E make_directory build/tmp
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

### Build System

- **Signature-Based Caching**: Task signatures include command, compiler version, dmake version, and all input file timestamps (including discovered headers).
- **Header Dependency Tracking**: Parses `.d` files (Make-style dependency files) or runs `g++ -H -E` as fallback.
- **Stat Cache**: Minimizes filesystem I/O by caching file modification times.
- **Parallel Execution**: Thread-per-task model with dependency-aware scheduling and configurable job limits.
- **Output Capture**: Buffers stdout/stderr to prevent interleaving, preserves colors with `-fdiagnostics-color=always`.
- **Dynamic Dependencies**: Build graph supports mutation at runtime for C++20 module dependencies.
- **Compile Commands Export**: Generates `compile_commands.json` for IDE integration.

### Test Runner

- **Parallel Execution**: `dmake test` runs tests in parallel using `std::async`.
- **Output Buffering**: Captures `stdout`/`stderr` from test processes to prevent interleaving.
- **Filtering**: Supports regex pattern matching for test names.
- **Integration**: Requires `enable_testing()` in CMakeLists.txt to register tests via `add_test()`.

### Command Parser Utility

The `CommandParser` utility (`dmake/command_parser.hpp`) provides a builder-style API for parsing CMake command arguments consistently across builtins.

**Capabilities:**
- `add_positional(var, label, required)`: Positional arguments (e.g., target name).
- `add_flag(keyword, bool_var)`: Boolean flags (e.g., `SHARED`).
- `add_value(keyword, string_var)`: Single value keywords (e.g., `WORKING_DIRECTORY <dir>`).
- `add_list(keyword, vector_var)`: Multi-value keywords (e.g., `SOURCES <s1> <s2>`).
- `add_multi_list(keyword, nested_vector_var)`: Repeated multi-value keywords (e.g., `COMMAND <c1> COMMAND <c2>`).
- `add_default_list(vector_var)`: Arguments not associated with any keyword.

**Standard Usage in Builtins:**
```cpp
CommandParser parser("my_command");
std::string target;
std::vector<std::string> sources;
bool verbose = false;

parser.add_positional(target, "target name");
parser.add_list("SOURCES", sources);
parser.add_flag("VERBOSE", verbose);

// Validates and reports errors to the interpreter automatically
PARSE_OR_RETURN(parser, interp, args);
```

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
- `target_include_directories()`, `target_compile_definitions()`, `target_compile_options()`
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
- `try_compile()` - Test compilation of source code with aggressive caching

**Math & Messaging:**
- `math(EXPR)` - Arithmetic expression evaluation
- `message()` - Output with STATUS/WARNING/FATAL_ERROR modes

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

**Implementation**: The `evaluate_condition` method in `interperter.cpp` (around line 1011-1400+) implements the full parser with proper precedence handling.

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

| Config | CMAKE_BUILD_TYPE | Default CMAKE_CXX_FLAGS_<CONFIG> |
|--------|------------------|----------------------------------|
| debug (default) | Debug | `-g -O0` |
| release | Release | `-O3 -DNDEBUG` |
| relwithdebinfo | RelWithDebInfo | `-g -O2 -DNDEBUG` |
| minsizerel | MinSizeRel | `-Os -DNDEBUG` |

**Compiler Flags**:
- Default CMAKE_CXX_FLAGS_<CONFIG> are automatically set for standard configurations
- Flags are applied to all targets created with `add_executable()` or `add_library()`
- Users can override defaults by setting variables in CMakeLists.txt or via `-D` flag:
  ```cmake
  set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fsanitize=address")
  ```
  or
  ```bash
  dmake . --config debug -DCMAKE_CXX_FLAGS_DEBUG="-g -O0 -fsanitize=address"
  ```
- Additional flags can be added per-target with `target_compile_options()`

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

## Advanced Features

### C++20 Modules (Experimental)

dmake supports C++20 modules using a **scan-at-build** three-stage pipeline:

1. **Scanner Tasks** (parallel):
   - Runs `g++ -E -fdirectives-only -fmodules-ts <source>`
   - Extracts `export module` and `import` declarations via regex
   - Outputs `.ddi` (Dynamic Dependency Info) JSON files
   - Fully parallelizable with zero initial dependencies

2. **Collator Task** (synchronization barrier):
   - Reads all `.ddi` files for a target
   - Resolves module names to BMI (Binary Module Interface) paths
   - **Dynamically injects dependencies** into the live build graph
   - Generates module mapper file (`-fmodule-mapper=modules.map`)

3. **Compile Tasks**:
   - Wait for collator + required BMIs
   - Use `-fmodules-ts -fmodule-mapper=<file>` flags

**Detection**: Module interface files automatically detected by extension (`.cppm`, `.ixx`, `.ccm`).

**Implementation**: `module_scanner.cpp`, `BuildGraph::inject_module_dependencies()`.

### Precompiled Headers

- **Automatic wrapper generation**: Creates `<binary_dir>/objs/<target>_pch.hpp`
- **Single PCH per target**: Contains all `PRECOMPILE_HEADERS` (PRIVATE + PUBLIC)
- **Compilation**: `g++ -c -o <wrapper>.gch <wrapper>.hpp`
- **Usage**: `-include <wrapper>` applied to all source files
- **Incremental**: Regenerates only if header list changes

### Find Package System

Implements both **Module Mode** and **Config Mode**:
- **Module mode**: Searches `CMAKE_MODULE_PATH` then `CMAKE_ROOT/Modules/` for `Find<Package>.cmake`
- **Config mode**: Looks for `<Package>Config.cmake` or `<package>-config.cmake`
- **Version checking**: Supports component parsing and version comparison
- **Variables set**: `<Package>_FOUND`, `<Package>_VERSION_*`, package-specific variables

### Custom Targets

`add_custom_target()` fully implemented:
- Multiple `COMMAND` entries (sequential execution)
- `DEPENDS` on files or other targets
- `ALL` flag for inclusion in default build
- `WORKING_DIRECTORY` and `COMMENT` options
- Executes commands in order, stops on first failure

### Directory Scan Caching

Optimizes `find_*()` commands:
- Caches directory listings with mtime validation
- Clock skew detection with warnings
- Invalidates cache when directory modifications detected
- O(1) filename lookups via `unordered_set`
- Significantly speeds up repeated find operations

### Toolchain Abstraction

- **Compiler interface**: Abstract `get_compile_command()`, `get_link_command()`, `get_archive_command()`
- **GnuCompiler implementation**: For gcc/g++
- **Toolchain manager**: Per-language compiler selection (C, CXX, CUDA)
- **Extensible**: Ready for clang, MSVC implementations

### CMake Tool Mode

`dmake -E <command>` provides CMake-compatible tool commands:
- `echo`, `echo_append` - Print to stdout
- `touch`, `touch_nocreate` - Update file timestamps
- `remove`, `remove_directory` - Delete files/directories
- `make_directory` - Create directories
- `copy`, `copy_if_different`, `copy_directory` - Copy operations
- `rename` - Move/rename files

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

## Centralized Cache System

dmake implements a centralized JSON-based cache infrastructure for subsystems that need persistent, cross-invocation state.

**Cache Location**: `<build_dir>/.dmake_subsystem_cache.json`

**Architecture**:
- **Format**: JSON serialization using Glaze (zero-copy parsing, minimal allocations)
- **Structure**: Subsystem-based namespacing (e.g., `try_compile_cache`, `file_listing_cache`)
- **Thread Safety**: Mutex-protected read/write operations
- **Atomicity**: Temp file + atomic rename prevents corruption
- **Lifecycle**: Loaded on interpreter construction, saved after successful build

**Cache Subsystems**:
- `CacheSubsystem::TryCompile` - try_compile() results with header dependency tracking
- `CacheSubsystem::FileListing` - Directory scan cache (future use)

**Implementation**: `dmake/cache_store.hpp/cpp`

### try_compile() Implementation

Implements CMake's `try_compile` command in SOURCE mode with aggressive caching.

**Supported Features**:
- **Source variants**: `SOURCES`, `SOURCE_FROM_CONTENT`, `SOURCE_FROM_VAR`, `SOURCE_FROM_FILE`
- **Compilation options**: `COMPILE_DEFINITIONS`, `CXX_STANDARD`, `C_STANDARD`
- **Linking**: `LINK_LIBRARIES` (with target name resolution), `LINK_OPTIONS`
- **CMAKE_FLAGS**: Additional compiler/linker settings (e.g., `-DCOMPILE_DEFINITIONS:STRING=-DFOO`)
- **Output capture**: `OUTPUT_VARIABLE` for compiler stdout/stderr
- **Result**: Boolean variable set to `TRUE`/`FALSE`

**Cache Signature** (transparent, human-readable):
```
compiler:<path>|version:<version>|lang:<C/CXX>|std:<std>|
src:<path>:<mtime>|inline:<name>:<blake2b>|
def:<def>|lib:<lib>|opt:<opt>|dep:<header>:<mtime>|...
```

**Signature Components**:
- Compiler path and version
- Language and standard
- Source file paths + mtimes (for `SOURCES`)
- Source names + content BLAKE2b hashes (for inline sources)
- All compile definitions (sorted)
- Link libraries and options (sorted)
- Discovered header dependencies + mtimes

**Cache Validation**:
- On cache lookup, validates all header mtimes match
- If any header changed → cache miss, recompile
- dmake version intentionally excluded (user preference)

**Temporary Directory**: `<bindir>/.dmake_try_compile/<hash>/`
- Unique per-signature (no conflicts)
- Preserved on failure (for debugging)
- Cleaned on success

**Examples**:
```cmake
# Basic usage
try_compile(RESULT ${CMAKE_BINARY_DIR}
    SOURCE_FROM_CONTENT test.cpp "int main() { return 0; }"
    CXX_STANDARD 17
    COMPILE_DEFINITIONS DEBUG
    OUTPUT_VARIABLE COMPILE_OUTPUT
)

# With CMAKE_FLAGS (CMake-compatible syntax)
try_compile(RESULT ${CMAKE_BINARY_DIR}
    SOURCES test.cpp
    CMAKE_FLAGS -DCOMPILE_DEFINITIONS:STRING=-DCHECK_FUNCTION_EXISTS=pthread_create
                -DLINK_LIBRARIES:STRING=pthread
)

# Real-world CheckFunctionExists pattern
try_compile(CMAKE_HAVE_PTHREAD_CREATE ${CMAKE_BINARY_DIR}
    SOURCES CheckFunctionExists.c
    CMAKE_FLAGS -DCOMPILE_DEFINITIONS:STRING=-DCHECK_FUNCTION_EXISTS=pthread_create
    LINK_LIBRARIES pthread
)
```

**Implementation**: `dmake/builtins/try_compile.cpp`

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

### Unit Tests

Tests use **Catch2 v3** and are organized by component:
- `tests/interpreter_tests.cpp` - Interpreter and builtin command tests
- `tests/language_tests.cpp` - Parser and language feature tests
- Additional test files for specific subsystems

**Test helper**: `run_script()` creates a minimal interpreter with custom `message()` builtin for output capture.

**Adding unit tests:**
1. Add a new `TEST_CASE` in appropriate test file
2. Use `run_script()` for interpreter tests
3. For parser tests, create a `Parser` directly and check `parse()` results
4. Run specific tests: `./build/dmake_tests "[test-name]"`

### Integration Tests

Location: `tests/integration/*/` - Each subdirectory is a complete test project.

**Test structure:**
- `CMakeLists.txt` - Project definition
- Source files - Test code
- `test.sh` - Shell script that takes dmake binary path as `$1`, builds project, verifies outputs

**Current integration tests (19):**
- `basic-exe`, `shared-lib`, `static-lib`, `interface-lib`, `object-lib`
- `pch` (precompiled headers)
- `incremental-rebuild` (cache validation)
- `custom-target` (add_custom_target)
- `find-package` (package discovery)
- `execute-process` (process execution)
- `modules_basic` (C++20 modules)
- `try_compile` (source compilation with caching)

**Adding integration tests:**
1. Create directory in `tests/integration/<test-name>`
2. Add `CMakeLists.txt` and source files
3. Create `test.sh` that builds with `$1` (dmake path) and verifies outputs
4. Script should exit 0 on success, non-zero on failure
5. Runner: `./tests/integration/run_tests.sh ./build/dmake`

## Self hosting

The project can self host. Once you have an initial `dmake` binary:

```bash
# In the project root
./build/dmake
```
The resulting binary will be in `build/debug/dmake`.

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
- `target.cpp`, `project.cpp`, `file.cpp`, `find_commands.cpp`, `find_package.cpp`
- `process.cpp`, `test.cpp`
- **Register in**: `Interpreter` constructor, inside `if (parent_ == nullptr)` block for root-only commands

**Target System**: `dmake/target.hpp/cpp`
- Unified `Target` class with type enum
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

**New builtin command:**
1. Create file in `dmake/builtins/` or add to existing
2. Implement as lambda: `[](Interpreter& interp, const std::vector<std::string>& args)` (void return)
3. Use `CommandParser` for argument parsing with `PARSE_OR_RETURN` macro
4. Call `interp.set_fatal_error()` and return on errors
5. Register in `Interpreter` constructor (root interpreter only) via `register_*_builtins()`

**New target property:**
1. Add to `Target` class property maps
2. Update `Target::resolve()` if transitive propagation needed
3. Update `set_target_properties` and `get_target_property` builtins
4. Modify `Target::generate_tasks()` to use property in task generation

**New if() operator:**
1. Add keyword constant in `interperter.cpp`
2. Modify `evaluate_condition()` method
3. Add to appropriate precedence level in recursive parser
4. Handle evaluation in operator switch/if blocks

**Error output format:**
1. Modify `print_error_context()` in `dmake-cli/main.cpp`
2. Error structure defined in `dmake/interperter.hpp`: `InterpreterError`

## Implementation Details

### Build Graph Execution

**Task Lifecycle:**
1. **Graph Construction**: All targets generate tasks before execution begins
2. **Cycle Detection**: DFS-based detection with full path reporting
3. **Ready Queue**: Tasks become ready when all dependencies complete
4. **Parallel Dispatch**: Worker threads pull from ready queue up to job limit
5. **Dynamic Injection**: C++20 module collator can inject new dependencies mid-build

**Thread Safety:**
- Global mutexes protect: output streams, build state, caches
- Each task owns its captured output buffer
- Stat cache and compiler version cache are thread-safe

### Property Resolution Algorithm

`Target::resolve()` implements depth-first transitive resolution:

1. Check if already resolved (cached results)
2. Mark as "visiting" (cycle detection)
3. For each dependency in `LINK_LIBRARIES`:
   - Recursively resolve dependency
   - Detect cycles via "visiting" flag
4. Merge properties:
   - Self: `PRIVATE` + `PUBLIC`
   - Propagation: `INTERFACE` + `PUBLIC` from dependencies
5. Resolve relative paths (relative to source_dir)
6. Mark as "resolved" and cache results

**Order preservation**: `LINK_LIBRARIES` maintain order for correct linking.

### File Signature Calculation

Task signatures use **Blake2b** hashing:
```
signature = hash(command_string + compiler_version + dmake_version +
                 mtime_of_input1 + mtime_of_input2 + ...)
```

**Header discovery:**
1. Check for existing `.d` file (Make-style dependencies)
2. Parse `.d` file for header list
3. Fallback: Run `g++ -H -E <source>` and parse output
4. Include all header mtimes in signature

### Modular Builtin Architecture

Builtins are organized by functionality:
- `message.cpp` - Output and diagnostics
- `variable.cpp` - set, unset, option, mark_as_advanced
- `list.cpp` - All list operations
- `string.cpp` - String manipulation
- `math.cpp` - Expression evaluation
- `target.cpp` - Target creation and configuration
- `project.cpp` - Project structure (add_subdirectory, include, etc.)
- `file.cpp` - File system operations
- `find_commands.cpp` - find_file, find_path, find_library, find_program
- `find_package.cpp` - Package discovery
- `process.cpp` - execute_process
- `test.cpp` - enable_testing, add_test

**Registration**: All builtins registered in root interpreter only (checked via `parent_ == nullptr`).

### Multi-Language Support

Currently supports C and C++ with:
- Separate compiler selection: `CMAKE_C_COMPILER`, `CMAKE_CXX_COMPILER`
- Separate standard flags: `CMAKE_C_STANDARD`, `CMAKE_CXX_STANDARD`
- Per-language compile flags: `CMAKE_C_FLAGS`, `CMAKE_CXX_FLAGS`
- Per-config flags: `CMAKE_C_FLAGS_DEBUG`, `CMAKE_CXX_FLAGS_RELEASE`, etc.
- Language-specific target properties

**Extensible**: Architecture ready for CUDA, Objective-C, etc.

### Compiler Version Caching

First build runs `g++ --version`, caches result for session:
- Avoids subprocess overhead on every build
- Invalidates signatures if compiler version changes
- Thread-safe cache with mutex protection

### Key Design Decisions

1. **No configure step**: Interprets CMakeLists.txt on every build (like Ninja with CMake's "rerun cmake" but always)
2. **Multi-config by default**: Cargo-style `build/<config>/` structure prevents accidental mixing
3. **Signature-based caching**: More reliable than timestamp-only, catches flag changes
4. **Thread-per-task model**: Simple model, bounded by job limit, good for I/O-bound compilation
5. **CMake compatibility first**: Semantic compatibility over feature parity - projects should "just work"
6. **Modern C++23**: Uses `std::expected` for error handling, ranges, concepts where appropriate
7. **Minimal dependencies**: Only CLI11, Catch2, Glaze - all lightweight or header-only
8. **Self-hosting**: dmake can build itself, ensuring real-world usability

### Performance Optimizations

- **Directory scan caching**: O(1) lookups for `find_*()` commands
- **Stat caching**: Minimizes filesystem calls during build
- **Compiler version caching**: One subprocess per build, not per task
- **Parallel everything**: Scanning, compilation, linking (where possible)
- **Incremental by default**: Signature-based skip of up-to-date tasks
- **Output buffering**: No lock contention on stdout/stderr during compilation

### Code Statistics

- **Total implementation**: ~7,762 lines (dmake/*.cpp + builtins/*.cpp)
- **Test coverage**: 15+ integration tests, comprehensive unit tests
- **Modular design**: Clear separation of concerns, easy to extend

## Some food for your own thought
* (when writing code) Do not write code before stating assumptions.
* (when writing code) Produce code you wouldn't want to debug at 3am
* Always state assumptions before implementing.
* Do not claim correctness you haven't verified.
* Do not handle only the happy path.
* Under what conditions does this work?
* Solve problems you weren't asked to solve unless instrumental
* Import complexity you don't need
