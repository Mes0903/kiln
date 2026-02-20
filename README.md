# dmake

Better C/C++ builds that just works, with CMake as an input language.

> [!IMPORTANT]
> Experimental project, Linux + GCC/G++ + non cross compiling only for now

## Why

Modern C/C++ development is editor-first:

* Language Servers rely on `compile_commands.json`
* Builds are incremental and frequent
* Developers expect fast, predictable errors

CMake is a mature and widely-supported build system generator, but the configure-then-generate model adds friction in tight development cycles and occationally causes major troubles. `dmake` keeps CMake as the input language while changing the execution model:

* **No configure step** - CMakeLists.txt is interpreted directly on every build, eliminating stale cache surprises. With aggressively invalidated cache for heavy built-ins
* **Faster interpretation** - dmake's interpreter is significantly faster than CMake's, with 10x+ speedups in some workloads
* **Better error messagess** - no more looking at cryptic errors and guessing where it originates from
* **It's a build system** - owns the entire build flow, better integration, no per-build system jank

## Building

The project has a few dependencies:

* CLI11 (https://github.com/CLIUtils/CLI11)
* Catch2 3.x (https://github.com/catchorg/Catch2)
* PCRE2 (https://github.com/PCRE2Project/pcre2)
* Glaze (https://github.com/stephenberry/glaze)
* pugixml (https://github.com/zeux/pugixml)
* libcurl
* libarchive
* linenoise
* C++23 capable compiler (GCC 13+)
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
dmake test                 # Run all tests
dmake test "RegexPattern"  # Run matching tests
```
Tests run in parallel with buffered output to keep the terminal clean.

### Installing
Install the project to a prefix:
```bash
dmake install
dmake install --prefix /usr/local
```

### Cleaning
Remove build artifacts:
```bash
dmake clean
```

### Options
Common flags:
- `-j N`: Set number of parallel jobs (defaults to CPU count)
- `-C <dir>`: Set project directory
- `-B <dir>`: Set build root directory
- `-P <script.cmake>`: Script mode (like CMake -P)
- `-DVAR=VAL`: Define a CMake variable
- `--config <debug|release|relwithdebinfo|minsizerel>`: Set build configuration
- `--profile`: Output a Chrome Trace Event Format compatible profile that can be loaded into [Perfetto](https://ui.perfetto.dev/) and others
- `--trace`: Print commands as they are executed
- `--trace-expand`: Like `--trace` but also shows variable expansion
- `--debugger`: Launch in debug mode (use `help` inside for commands, or see below)
- `--config-only`: Parse and cache only, skip the build step
- `--fresh`: Skip persistent cache (force re-evaluation of find_* and try_compile)

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

## Self Hosting

Since `dmake` uses CMake as its input language and is itself built using CMake, it can build itself!

```bash
# Assuming you have a dmake binary in build/
./build/dmake --config release
```

This will produce a release binary at `build/release/dmake`.
