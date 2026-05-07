# Kiln for CMake veterans

Kiln is designed to work like CMake. It is, after all, doing it's best to interpert and execute the build process. However there are some cititical differences that needs to be highlighted.

First, Kiln is not a _build system generator_. Despite heavy caching and a fast interperter, Kiln parses and interperts CMakeLists.txt every run to collect build information. This avoids stale caches. However, it also means Kiln does not remember your previosuly specified build options.

In `cmake` you can specify build options and they apply to all subsequent `cmake` invocations.

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake . -DCMAKE_CXX_STANDARD=20 # Build type is still Release
```

Kiln acts **as if every invocation is fresh** and forgets about previously specified build options.

```bash
kiln -C . -DCMAKE_BUILD_TYPE=Release
kiln -C . -DCMAKE_CXX_STANDARD=20 # Build type is now the default (i.e. Debug)
```

That gets us to the 2nd major difference. CMake defautls to `""` (null string) as the build type. Kiln wants you to be explicit and defaults to `Debug`. You can use the usual `Release`, `RelWithDebInfo` and `MinSizeRel` build types with Kiln. Kiln acts as if it's a multi-config build system and **outputs to `<build-dir>/<config>`** (e.g. `build/debug`, `build/release`).

For example, in a test project:

```plaintext
❯ tree
.
├── CMakeLists.txt
└── hello.cpp
```

Running `kiln` generates a debug build under `build/debug`

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
❯ tree -L 3
.
├── build
│   └── debug
│       ├── compile_commands.json
│       ├── hello
│       └── objs
├── CMakeLists.txt
└── hello.cpp

4 directories, 4 files
```

## Meta-data exposure to CMake scripts

Projects sometimes branch on the active generator. For Kiln's purposes:

- `CMAKE_GENERATOR` is set to the literal string `"kiln"`. There is no generator in the CMake sense (no Ninja or  Makefile is written), but the variable is populated so projects that read it for diagnostics or logging see something sensible. Conditionals like `if(CMAKE_GENERATOR STREQUAL "Ninja")` will simply be false.
- `GENERATOR_IS_MULTI_CONFIG` is set to `FALSE`.

The second one is worth elaborating on. As mentioned above, Kiln writes its output into `build/<config>/`, which behaves like a multi-config layout from the outside. The property is set to `FALSE` because most CMake code that checks `GENERATOR_IS_MULTI_CONFIG` is asking a more specific question: "do I read `CMAKE_BUILD_TYPE` (single-config) or `CMAKE_CONFIGURATION_TYPES` (multi-config)?". Kiln runs one configuration per invocation, with `CMAKE_BUILD_TYPE` set, so the single-config answer is the right one. Reporting `TRUE` would push projects down code paths that expect to emit per-config rules in a single configure step. Something Kiln does not do.

Likewise, Kiln pretends to be CMake 3.21 and sets `CMAKE_VERSION` and `CMAKE_VERSION_*` accordingly.
