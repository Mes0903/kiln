# dmake

Better C/C++ builds that just works, with CMake as an input language.

> ![NOTE]
> Experimental project, GCC/G++ only for now

## Why

Modern C/C++ development is editor-first:

* LSPs rely on `compile_commands.json`
* Builds are incremental and frequent
* Developers expect fast, predictable errors

CMake’s generator model was designed for a different era. This project keeps CMake compatibility while fixing the parts that routinely cause confusion:

* stale caches
* generator-specific behavior
* delayed or misleading errors

## Building and using

The project has very little dependency:

* CLI11 (https://github.com/CLIUtils/CLI11)
* Catch2 3.x (https://github.com/catchorg/Catch2)
* C++23 capable compiler

The project can be built with CMake like almost any other C++ project.

```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Self hosting

Of course, being a build system that consumes CMake. You can also self-host by building it with itself once you build it (or has a binary from elsewhere).

```bash
build/dmake . --config Release
```

It will produce the same binary in `build/release/dmake`.
