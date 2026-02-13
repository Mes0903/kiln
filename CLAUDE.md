# CLAUDE.md

This file provides guidance to Claude Code when working with this repository.

## Project Overview

dmake is a fast C++23 build system that interprets CMake's language directly without a configure step. Features:
- **Cargo-style multi-config builds** - separate `build/<config>/` directories
- **Parallel, incremental builds** - signature-based caching
- **No configure step** - interprets CMakeLists.txt on every build
- **Experimental C++20 modules support**

## Quick Reference

```bash
# Bootstrap
mkdir -p build && cd build && cmake .. && make

# Self-host (once bootstrapped)
./build/dmake                           # Output: build/debug/dmake

# Run tests
./build/dmake_tests                     # Unit tests
./build/dmake_tests "[test-name]"       # Specific test
./tests/integration/run_tests.sh ./build/dmake  # Integration tests

# CLI usage
dmake [targets...] [-j N] [-DVAR=VAL] [--config CONFIG]
dmake build [targets...]      # Build targets
dmake run <target> [-- args]  # Build and run
dmake test [pattern]          # Run tests
dmake clean                   # Clean build
dmake install [--prefix PATH] # Install project
dmake -P <script.cmake>       # Script mode
dmake -E <cmd> [args]         # Tool mode (echo, touch, copy, etc.)
dmake --debug                 # Interactive debugger
dmake --trace                 # Trace execution
```

## Architecture

Four main layers:

1. **Parser** (`cmake-language.hpp/cpp`): Recursive descent CMake parser
2. **Interpreter** (`interperter.hpp/cpp`): AST execution with builtin registry
3. **Target System** (`target.hpp/cpp`): Unified targets with transitive resolution
4. **Build System** (`build_system.hpp/cpp`): Parallel task execution with caching

### Key Subsystems

| System | Files | Purpose |
|--------|-------|---------|
| Generator Expressions | `genex_parser.hpp/cpp`, `genex_evaluator.hpp/cpp` | `$<...>` expressions |
| Install | `install_executor.hpp/cpp`, `builtins/install.cpp` | Installation support |
| FetchContent | `intercept/fetch_content.cpp` | Dependency downloading |
| ExternalProject | `intercept/external_project.cpp` | External builds |
| Debugger | `debugger.hpp/cpp` | Interactive debugging |
| Condition Evaluator | `condition_evaluator.hpp/cpp` | if() parsing |
| Cache Store | `cache_store.hpp/cpp` | Persistent JSON cache |

## Supported Commands

**Targets**: `add_executable`, `add_library`, `add_custom_target`, `add_custom_command`, `add_dependencies`

**Target Properties**: `target_sources` (FILE_SET support), `target_include_directories`, `target_compile_definitions`, `target_compile_options`, `target_compile_features`, `target_link_libraries`, `target_link_directories`, `target_precompile_headers`, `set_target_properties`, `get_target_property`

**Variables**: `set`, `unset`, `option`, `list`, `string`, `math`

**Files**: `file` (READ/WRITE/GLOB/COPY/...), `configure_file`, `cmake_path`

**Find**: `find_file`, `find_path`, `find_library`, `find_program`, `find_package`

**Properties**: `set_property`, `get_property`, `define_property`, `set_source_files_properties`, `get_source_file_property`

**Install/Export**: `install` (TARGETS/FILES/PROGRAMS/DIRECTORY/SCRIPT/CODE/EXPORT), `export`

**Testing**: `enable_testing`, `add_test`, `set_tests_properties`, `try_compile`, `try_run`

**Process**: `execute_process`

**Dependencies**: `FetchContent_Declare`, `FetchContent_MakeAvailable`, `FetchContent_Populate`, `ExternalProject_Add`

**Control Flow**: `if`/`elseif`/`else`/`endif`, `foreach`/`endforeach`, `while`/`endwhile`, `function`/`endfunction`, `macro`/`endmacro`, `return`, `break`, `continue`, `include`, `add_subdirectory`, `cmake_language(CALL/DEFER)`

**System**: `message`, `cmake_minimum_required`, `project`, `cmake_host_system_information`

## Generator Expressions

Supports common genex patterns:

- **Context**: `$<BUILD_INTERFACE:...>`, `$<INSTALL_INTERFACE:...>`, `$<CONFIG:cfg>`
- **Logic**: `$<BOOL:...>`, `$<IF:cond,true,false>`, `$<AND:...>`, `$<OR:...>`, `$<NOT:...>`
- **Comparison**: `$<STREQUAL:a,b>`, `$<VERSION_LESS:...>`, etc.
- **Target**: `$<TARGET_EXISTS:...>`, `$<TARGET_FILE:...>`, `$<TARGET_OBJECTS:...>`, `$<TARGET_PROPERTY:...>`
- **Compiler**: `$<COMPILE_LANGUAGE:...>`, `$<CXX_COMPILER_ID:...>`, `$<C_COMPILER_ID:...>`
- **Special**: `$<LINK_ONLY:...>`, `$<INSTALL_PREFIX>`

## Code Locations

### Core Files
- **Parser**: `dmake/cmake-language.hpp/cpp`
- **Interpreter**: `dmake/interperter.hpp/cpp`
- **Target**: `dmake/target.hpp/cpp`
- **Build System**: `dmake/build_system.hpp/cpp`
- **CLI**: `dmake-cli/main.cpp`

### Builtins (`dmake/builtins/`)
Each file registers commands via `register_*_builtins()`:
- `message.cpp`, `variable.cpp`, `list.cpp`, `string.cpp`, `math.cpp`
- `target.cpp`, `project.cpp`, `file.cpp`, `path.cpp`
- `find_commands.cpp`, `find_package.cpp`
- `process.cpp`, `try_compile.cpp`
- `property.cpp`, `source_properties.cpp`
- `install.cpp`, `export_generator.cpp`
- `system_info.cpp`

### Utilities
- **CommandParser**: `dmake/command_parser.hpp` - Builder API for parsing CMake args
- **CMakeList**: `dmake/cmake_list.hpp` - Semicolon-separated list ops
- **Module Scanner**: `dmake/module_scanner.hpp/cpp` - C++20 module deps

## Adding New Features

**New builtin**: Create in `dmake/builtins/`, use `CommandParser`, add `register_*_builtins()` declaration to `registry.hpp`, call from `Interpreter` constructor

**New target property**: Add to `Target` class, update `resolve()` if transitive, modify `generate_tasks()` for build usage

**New if() operator**: Add keyword in `condition_evaluator.cpp`

## Key Patterns

- **Glaze headers are expensive** - keep `#include <glaze/glaze.hpp>` in .cpp files only
- **C++23 codebase** - uses `std::expected`, `std::string_view`, `inline` variables
- **Signature-based caching** - Blake2b hash of command + compiler version + input mtimes
- **Header deps** - parsed from `.d` files or `g++ -H -E` fallback
- **Multi-config** - `build/{debug,release,relwithdebinfo,minsizerel}/`

## Testing

**Unit tests**: `tests/*.cpp` (Catch2 v3). Run with `./build/dmake_tests "[tag]"`.

**Integration tests**: `tests/integration/*/`. Each has `CMakeLists.txt` + `test.sh` script taking dmake path as `$1`.

## Debugging

```bash
dmake --debug              # Interactive debugger (step, breakpoints, backtrace)
dmake --trace              # Trace all command execution
dmake --trace-expand       # Trace with variable expansion shown
dmake --break-on-message="pattern"  # Break on matching message
```

Debugger commands: `s`tep, `n`ext, `c`ontinue, `b`reak, `d`elete, `p`rint, `bt` (backtrace), `l`ist, `q`uit

## Design Principles

- No configure step - interpret CMakeLists.txt every build
- Signature-based caching catches flag changes (more reliable than timestamp-only)
- CMake compatibility first - semantic compatibility over feature parity
- Multi-config by default with Cargo-style `build/<config>/` structure

## Coding Guidelines

- State assumptions before implementing
- Produce code you wouldn't want to debug at 3am
- Don't handle only the happy path
- Don't solve problems you weren't asked to solve
- Don't import complexity you don't need
