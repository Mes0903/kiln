# dmake

Better C/C++ builds that just work with CMake as an input language.

## Building and using

The project can be built with CMake.

```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Self hosting

The project can be self-hosted by building it with itself once you build it (or has a binary from elsewhere).

```bash
build/dmake . --config Release
```

It will produce the same binary in `build/release/dmake`.
