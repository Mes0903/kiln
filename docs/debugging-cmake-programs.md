# Debugging CMake programs

Build systems are programs too And CMake projects routinely grow large enough that something will eventually misbehave: a target picks up an include path you didn't expect, a variable comes out empty, or a flag refuses to land on the command line. Kiln ships two tools for figuring out what is going on: `kiln_dump_target_info`, for inspecting a target's properties, and an interactive debugger that steps through the CMake interpreter itself.

## Inspecting a target with `kiln_dump_target_info`

`kiln_dump_target_info` is a Kiln-specific command you can drop into your `CMakeLists.txt` to print everything Kiln knows about a target - its type, source and binary directories, output path, and every list-valued property (include directories, compile options, link libraries, and so on), broken down by visibility.

The simplest form takes a target name:

```cmake
add_executable(hello hello.cpp)
target_include_directories(hello PRIVATE include)
target_compile_definitions(hello PUBLIC HELLO_VERSION=1)

kiln_dump_target_info(hello)
```

When Kiln reaches that line during configuration, it prints something long the lines of:

```plaintext
=== Target: hello ===
Type: EXECUTABLE
Source Dir: /home/me/project
Binary Dir: /home/me/project/build/debug
Output Path: /home/me/project/build/debug/hello
Imported: no

--- Unresolved Properties ---

INCLUDE_DIRECTORIES:
  PRIVATE:
    - /home/me/project/include

COMPILE_DEFINITIONS:
  PUBLIC:
    - HELLO_VERSION=1

--- Resolved Properties (after dependency resolution) ---
...
```

Two things are worth pointing out about this output:

- **Unresolved properties** are exactly what you wrote - what `target_include_directories` and friends recorded on the target itself, with no propagation from dependencies.
- **Resolved properties** are what Kiln will actually use when it builds the target, after walking through every linked dependency and pulling in its `PUBLIC`/`INTERFACE` properties. This is the section to look at when you ask "is `-DSOMETHING` on my command line?".

### `AT_BUILD`: dumping after resolution

If you call `kiln_dump_target_info` while the `CMakeLists.txt` is still being interpreted, Kiln has not yet resolved dependencies. The "Resolved Properties" section may be empty or incomplete. To see the final picture, pass `AT_BUILD`:

```cmake
kiln_dump_target_info(hello AT_BUILD)
```

Kiln defers the dump until after target resolution, just before the build starts. Use the `AT_BUILD` form when:

- You want to see what `INTERFACE` properties a dependency contributed.
- A target uses generator expressions that only get evaluated at build time.
- You're chasing a property that ought to be there but the immediate dump shows it empty.

For day-to-day "did I spell this right?" questions, the immediate form is faster. Kiln prints and keeps configuring, no build step needed.

### Stopping after configure with `--config-only`

When you're debugging the build system itself, compiling and linking the actual project is just noise — and on a large project, slow noise. Pass `--config-only` to make Kiln interpret `CMakeLists.txt`, save its cache, and exit before any compilation happens:

```bash
kiln --config-only
```

This pairs naturally with `kiln_dump_target_info`: drop the dump call into your `CMakeLists.txt`, run with `--config-only`, read the output, iterate. No need to wait for a real build when the question is about properties or variables.

## The interactive debugger

When the problem isn't a single target but the flow of the CMake script itself — a variable being overwritten, an `if()` taking the wrong branch, a function call that does not produce the expected effect — a print-style approach gets tedious. Kiln includes an interactive debugger modelled on `gdb` that lets you step through CMake commands one at a time.

Start it with `--debugger`:

```bash
kiln --debugger
```

Kiln stops on the first command in the top-level `CMakeLists.txt` and gives you a prompt:

```plaintext
CMakeLists.txt(1): cmake_minimum_required(VERSION 3.20)
(kiln-dbg)
```

From here it behaves much like `gdb`. The most useful commands:

| Command | Short | What it does |
| --- | --- | --- |
| `continue` | `c` | Run until the next breakpoint. |
| `step` | `s` | Execute one command, descending into function calls. |
| `next` | `n` | Execute one command, staying at the current depth. |
| `print <var>` | `p` | Print the value of a CMake variable. |
| `backtrace` | `bt` | Show the call stack. |
| `list` | `l` | Show source around the current line. |
| `frame [N]` | `f` | Select stack frame N. |
| `up` / `down` | | Move up or down the call stack. |
| `info variables` | | List every visible variable. |
| `info breakpoints` | | List active breakpoints. |
| `quit` | `q` | Exit Kiln. |

### Setting breakpoints

`break` (or `b`) takes three forms:

```plaintext
(kiln-dbg) break 42                     # line 42 of the current file
(kiln-dbg) break src/CMakeLists.txt:17  # specific file and line
(kiln-dbg) break add_executable         # break whenever this command runs
```

The third form is particularly handy: if you want to see every `target_link_libraries` call as it happens, set a breakpoint on the command name and `continue` between hits.

To break when a `message()` call matches a pattern, use `break-on-message` (or `bm`). This is also exposed on the command line:

```bash
kiln --break-on-message "looking for foo"
```

Kiln stops the moment a `message()` call produces text matching that pattern, which is a handy way to land in the debugger right when something interesting happens — without editing `CMakeLists.txt` to insert a marker.

### Watching a variable

If a variable is being changed somewhere and you can't tell where, use `watch`:

```plaintext
(kiln) watch CMAKE_CXX_FLAGS
```

Kiln will stop the next time something assigns to `CMAKE_CXX_FLAGS`, with the call stack pointing at the offending line.

### Inspecting state

`print` and `info variables` are your main tools for looking at the current state. `print` accepts any CMake variable name, and `info variables` dumps everything visible in the current scope — useful when you're not sure what's been set.

To navigate the call stack, use `backtrace` to see the frames, then `frame N` (or `up` / `down`) to switch. `print` and `list` operate on whichever frame you have selected, so you can walk back up through an `include()` chain and inspect locals at each level.

### A typical session

A common debugging flow looks like this:

```plaintext
❯ kiln --debugger
CMakeLists.txt(1): cmake_minimum_required(VERSION 3.20)
(kiln) break target_link_libraries
Breakpoint 1 on command "target_link_libraries"
(kiln) continue
src/CMakeLists.txt(23): target_link_libraries(hello PRIVATE foo)
(kiln) print FOO_LIBS
FOO_LIBS = (unset)
(kiln) backtrace
#0 src/CMakeLists.txt:23 in target_link_libraries
#1 CMakeLists.txt:7 in add_subdirectory
(kiln) up
#1 CMakeLists.txt:7 in add_subdirectory
(kiln) print FOO_LIBS
FOO_LIBS = foo;bar
```
