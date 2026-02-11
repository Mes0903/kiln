# dmake

Better C/C++ builds that just works, with CMake as an input language.

> [!NOTE]
> Experimental project, Linux + GCC/G++ only for now

## Why

Modern C/C++ development is editor-first:

* LSPs rely on `compile_commands.json`
* Builds are incremental and frequent
* Developers expect fast, predictable errors

CMake’s generator model was designed for a different era. This project keeps CMake compatibility while fixing the parts that routinely cause confusion:

* stale caches
* generator-specific behavior
* delayed or misleading errors

## Building

The project has very few dependencies:

* CLI11 (https://github.com/CLIUtils/CLI11)
* Catch2 3.x (https://github.com/catchorg/Catch2)
* PCRE2 (https://github.com/PCRE2Project/pcre2)
* Glaze (https://github.com/stephenberry/glaze)
* libcurl
* C++23 apable compiler (GCC 13+)
* CMake (dmake is an execution engine, you still need the CMake shipped modules)

To build `dmake` for the first time using CMake:

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## Usage

`dmake` provides a modern, verb-based CLI inspired by tools like `cargo` or `go`.

### Basic Build
Build the current project (the `all` virtual target):
```bash
dmake
```

Build specific targets:
```bash
dmake my_lib my_app
```

### Running Targets
Build and execute an executable target in one step:
```bash
dmake run my_app -- --arg1 --arg2
```

### Testing
Run tests defined with `add_test()`:
```bash
dmake test                # Run all tests
dmake test "RegexPattern"  # Run matching tests
```
Tests run in parallel with buffered output to keep the terminal clean.

### Cleaning
Remove build artifacts:
```bash
dmake clean
```

### Debugging CMake
Launch in debug mode (GDB-like commands with breaking and other features). The program is then immediately broken on the first command. Setup breakpoints here and use `c` or `continue` to start execution. Debug mode also automatically breaks on fatal errors.

```plaintext
❯ ./dmake .. --debugger
(dmake) /home/marty/Documents/dmake/CMakeLists.txt:1  cmake_minimum_required(VERSION 3.15)
(dmake) > help
Commands:
  break <line>         (b) Set breakpoint at line in current file
  break <file>:<line>  (b) Set location breakpoint
  break <command>      (b) Break on command name
  continue             (c) Run until next breakpoint
  step                 (s) Step to next command
  next                 (n) Step over (stay at current call depth)
  print <var>          (p) Print variable value
  backtrace            (bt) Show call stack
  list                 (l) Show source around current line/frame
  frame [N]            (f) Select stack frame N (show current if no arg)
  up                       Move up one stack frame (toward caller)
  down                     Move down one stack frame (toward callee)
  info variables           List all visible variables
  info breakpoints         List all breakpoints
  break-on-message <p> (bm) Break when message matches pattern
  watch <var>          (w) Break when variable changes
  delete <n>           (d) Delete breakpoint by ID
  quit                 (q) Exit dmake
  help                 (h) Show this help
(dmake) > 
```

### Options
Common flags:
- `-j N`: Set number of parallel jobs (defaults to CPU count)
- `--config <debug|release|relwithdebinfo>`: Set build configuration
- `-DVAR=VAL`: Define a CMake variable
- `-B <dir>`: Set build root directory
- `--profile`: Output a Chrome Trace Event Format compatiable profile that can be loaded into [Perfetto](https://ui.perfetto.dev/) and others
- `--debugger`: Launch in debug mode (GDB-like commands with breaking and other features. Use `help` in the debugger for more information)

## Self Hosting

Since `dmake` uses CMake as its input language and itself built using CMake, it can build itself!

```bash
# Assuming you have a dmake binary in build/
./build/dmake --config release
```

This will produce a release binary at `build/release/dmake`.
