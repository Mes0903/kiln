# Intercepted CMake commands

CMake ships with a set of standard modules that projects pull in via `include(...)`. Most of them - `CMakeParseArguments`, `GNUInstallDirs`, `CheckIncludeFile`, and so on - are just CMake code, and Kiln runs them the same way CMake does: read the file, interpret it.

A few are different. Some modules in upstream CMake rely on behaviour that only makes sense for a generator (writing a script for Ninja or Make to run later), or they shell out to a second `cmake` invocation to do work that a one-shot interpreter can do directly. For those, Kiln intercepts the `include(...)` call and substitutes its own implementation.

This document lists which modules Kiln intercepts and why.

## How interception works

When you write:

```cmake
include(FetchContent)
```

Kiln notices the module name before it tries to read the file from disk. If it has a native implementation registered for that name, it skips the file entirely and registers the corresponding commands (`FetchContent_Declare`, `FetchContent_MakeAvailable`, ...) directly into the interpreter. From your `CMakeLists.txt`'s point of view nothing looks different: the same commands exist, just a different implementation.

This means an intercepted module won't pick up local modifications. If you have a copy of `FetchContent.cmake` on your `CMAKE_MODULE_PATH`, Kiln still uses its own implementation. In return you get behaviour that fits Kiln's execution model.

## Intercepted modules

### `FetchContent`

Upstream `FetchContent` controls downloads, extraction, and patching by writing helper scripts and re-invoking CMake. Kiln's interpreter can do all of that in-process: download the archive, unpack it, apply patches, then `add_subdirectory` the result - with better UX, control and granularity.

The replacment implements `FetchContent_Declare`, `FetchContent_Populate`, `FetchContent_MakeAvailable`, and `FetchContent_GetProperties`,with the same `URL`, `GIT_REPOSITORY`, `GIT_TAG`, and `PATCH_COMMAND` arguments you would pass in CMake. Source and binary directories are exposed through the same `_FetchContent_<name>_sourceDir` / `_binaryDir` properties downstream code expects.

### `ExternalProject`

`ExternalProject` in CMake builds its targets at build time by writing scripts that re-invoke `cmake` and the chosen generator. Kiln has no generator step. Kiln replaces it with a pass that configure/build/install of the external project as part of the same build graph.

`ExternalProject_Add` and the related `ExternalProject_Add_Step` / `ExternalProject_Get_Property` commands are reimplemented. URL/Git sources, configure/build/install steps, and `INSTALL_DIR` propagation all behave the way projects expect.

Some projects directly invokes CMake with `CONFIGURE_COMMAND ${CMAKE_COMMAND} -G "Unix Makefiles" -S . -B build` or `BUILD_COMMAND ${CMAKE_COMMAND} --build . --target foo` directly. Under CMake `${CMAKE_COMMAND}` resolves to `cmake`; under Kiln it resolves to `kiln`, which does not have a generation phase. To keep these projects working, Kiln detects when an `execute_process`-style invocation is calling its own binary with cmake-style flags and rewrites the arguments to the equivalent Kiln command (`-G` is dropped, `--build <dir>` becomes `-C <dir>`, `--target foo` becomes the positional target `foo`, and so on). It is transparent in practice, but worth knowing about.

For cases where Kiln knows the external project is going into non CMake based builds, Kiln shells out to the external build system directly at build time.

### `CPack`

Packaging is not implemented yet. You get a warning about CPack not avaliable and packaging is skipped.
