# Using Kiln

This document walks through the basics of using Kiln on a small project. It assumes you've used CMake before. If you haven't, the [official CMake tutorial](https://cmake.org/cmake/help/latest/guide/tutorial/index.html) is a good place to start. Kiln consumes `CMakeLists.txt` files directly, so the CMake you already know carries over.

## A first build

Suppose you have a project that looks like this:

```plaintext
.
├── CMakeLists.txt
└── hello.cpp
```

To build it, `cd` into the directory and run `kiln`:

```plaintext
❯ kiln
[STATUS] Configuring done (0.0s)
[STATUS] Generating build graph...
[STATUS] Generating done (0.0s)
[STATUS] Starting debug build...
   Compiling [hello] hello.cpp
     Linking [hello] hello
    Finished build in 0.17s (critical path: 0.17s)
[STATUS] Build finished.
```

Kiln configured the project, generated a build graph, and built it - all in a single command. It outputs `build/debug/` as Kiln defaults to a debug build; Please refer to the [Kiln for CMake veterans](kiln-for-cmake-veterans.md) documentation for more details.

## Building specific targets

By default Kiln builds everything (the `all` target). To build a particular target, pass its name:

```bash
kiln hello
```

You can list more than one:

```bash
kiln hello hello_tests
```

## Running an executable

Once `hello` is built, you can ask Kiln to run it. This is convenient because Kiln will rebuild it first if anything changed:

```bash
kiln run hello
```

If the program takes arguments, put them after `--`:

```bash
kiln run hello -- --name world
```

Anything after `--` is forwarded to the program untouched.

## Picking a configuration

Kiln supports the four standard CMake build types: `debug`, `release`, `relwithdebinfo`, and `minsizerel`. The default is `debug`. To pick another, use `--config` (or `-c`):

```bash
kiln --config release
kiln -c relwithdebinfo
```

`-DCMAKE_BUILD_TYPE=Release` works the same way and is equivalent to `--config release`.

Each configuration gets its own subdirectory under `build/`, so a release build does not overwrite your debug build:

```plaintext
build/
├── debug/
└── release/
```

Switching between them is just a matter of passing a different `--config`.

## Windows support

Kiln has v1 Windows CI smoke coverage for basic MSVC projects that build executables, static libraries, shared libraries, custom commands, `execute_process`, `find_program`, and `find_library`. On Windows the default debug outputs are under `build/debug/`, with executables ending in `.exe`, static libraries and import libraries ending in `.lib`, and shared library runtimes ending in `.dll`.

The Linux integration suite remains broader. Windows support for install/export workflows and `try_compile` migration is still outside the v1 smoke surface.

## Setting variables

CMake variables are set the same way as in CMake, with `-D`:

```bash
kiln -DMY_OPTION=ON -DMY_PATH=/opt/foo
```

One thing worth knowing: Kiln does not remember `-D` flags between runs. If you want `MY_OPTION=ON` on every build, pass it every time. The [Kiln for CMake veterans](kiln-for-cmake-veterans.md) doc covers the reasoning.

## Running tests

If your project defines tests, build and run them with:

```bash
kiln test
```

Kiln sets `BUILD_TESTING=ON` for you, so tests are built even if your `CMakeLists.txt` would normally gate them behind that variable. To run only some tests, pass a regex:

```bash
kiln test "parser.*"
```

## Cleaning

To remove build artifacts for the current configuration:

```bash
kiln clean
```

This only touches the current config - `kiln clean` after a debug build does not remove `build/release/`. Pass `-c` to clean a different config:

```bash
kiln clean -c release
```

## Installing

```bash
kiln install
kiln install --prefix /usr/local
kiln install --component runtime
```

Kiln builds the project first, then installs. Depending on the prefix, you may need elevated privileges.

## Working from another directory

You don't have to `cd` into the project. `-C` points Kiln at a project directory:

```bash
kiln -C ~/code/my_project
kiln -C ~/code/my_project run hello
```

If you'd like the build output to live somewhere other than `build/`, use `-B`:

```bash
kiln -B /tmp/scratch-build
```

## Parallelism

Kiln uses all available cores by default. To dial it back, use `-j`:

```bash
kiln -j4
```

## Presets

If the project ships a `CMakePresets.json`, you can use the same preset machinery you would with CMake:

```bash
kiln --list-presets
kiln --preset dev
```

## Running a CMake script

Sometimes you want to run a CMake script without building anything. `-P` does that, the same as `cmake -P`:

```bash
kiln -P script.cmake -DFOO=bar
```

## When something goes wrong

A few flags are useful when a build does not behave the way you expect:

- `--trace` prints each CMake command as it runs.
- `--trace-expand` does the same, but with variables expanded — handy when you suspect a variable isn't what you think it is.
- `--log-level VERBOSE` (or `DEBUG`, `TRACE`) turns up the output from `message()` calls and Kiln itself.
- `--fresh` ignores the persistent cache and reconfigures from scratch, useful if you suspect a stale value is hanging around.
- `--debugger` drops you into Kiln's interactive CMake debugger. See [debugging CMake programs](debugging-cmake-programs.md) for a walkthrough.

## CMake-compatible tools

Kiln implements the same `-E` tool commands as CMake, so build scripts that shell out to `cmake -E ...` continue to work when invoked through Kiln:

```bash
kiln -E copy file.txt dest/
kiln -E echo "hello"
```

`kiln tool` is a synonym for `-E` if you prefer a word over a flag.
