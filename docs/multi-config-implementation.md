# Multi-Configuration Build Support - Implementation Plan

## Overview

Implement Cargo-style multi-configuration support where different build types live in separate subdirectories under the build root (e.g., `build/debug`, `build/release`).

## Requirements

1. **Default configuration**: `debug`
2. **Build directory structure**: `<build_root>/<config>/` (e.g., `build/debug/`, `build/release/`)
3. **Cache isolation**: Each config has its own `.dmake_cache` at `<build_root>/<config>/.dmake_cache`
4. **CMAKE_BUILD_TYPE**: Automatically set based on `--config` flag
5. **Flag combinations**: `-B foo --config release` → `foo/release`
6. **Backward compatibility**: Existing behavior should not break

## Command-Line Interface

### New Flag

```bash
-c, --config <CONFIG>     Build configuration (debug, release, relwithdebinfo, minsizerel)
                          Default: debug
```

### Examples

| Command | Build Directory | CMAKE_BUILD_TYPE |
|---------|----------------|------------------|
| `dmake .` | `./build/debug` | `Debug` |
| `dmake . --config release` | `./build/release` | `Release` |
| `dmake . -B out` | `out/debug` | `Debug` |
| `dmake . -B out --config release` | `out/release` | `Release` |
| `dmake . -DCMAKE_BUILD_TYPE=Custom` | `./build/debug` | `Custom` (overridden) |

## Implementation Details

### 1. CLI Parsing (dmake-cli/main.cpp)

**Location**: After existing CLI11 option definitions (~line 24)

```cpp
std::string config;
app.add_option("-c,--config", config,
    "Build configuration (debug, release, relwithdebinfo, minsizerel)")
   ->default_val("debug")
   ->transform(CLI::Transformer({
       {"debug", "debug"},
       {"release", "release"},
       {"relwithdebinfo", "relwithdebinfo"},
       {"minsizerel", "minsizerel"}
   }, CLI::ignore_case));
```

### 2. Build Directory Resolution (dmake-cli/main.cpp)

**Location**: Replace current build_path logic (~line 43-48)

```cpp
// Determine build root
std::filesystem::path build_root;
if (build_directory_str.empty()) {
    build_root = directory_path / "build";
} else {
    build_root = std::filesystem::absolute(build_directory_str).lexically_normal();
}

// Normalize config to lowercase for directory name
std::string config_lower = config;
std::transform(config_lower.begin(), config_lower.end(), config_lower.begin(), ::tolower);

// Append config to build root
std::filesystem::path build_path = build_root / config_lower;

// Same check: build_path cannot equal directory_path
if (build_path == directory_path) {
    std::cerr << "Error: Build directory cannot be the same as the source directory" << std::endl;
    return 1;
}
```

### 3. CMAKE_BUILD_TYPE Variable (dmake-cli/main.cpp)

**Location**: Before interpreter.interpret() call (~line 117)

**Logic**:
1. Check if user already set CMAKE_BUILD_TYPE via `-D` flag
2. If not set, set it based on `--config` with proper casing

```cpp
// Helper function to convert config to proper CMake case
auto to_cmake_case = [](const std::string& config) -> std::string {
    std::string result = config;
    if (!result.empty()) {
        result[0] = std::toupper(result[0]);
        // Handle relwithdebinfo -> RelWithDebInfo
        if (result == "Relwithdebinfo") return "RelWithDebInfo";
        if (result == "Minsizerel") return "MinSizeRel";
    }
    return result;
};

// Check if CMAKE_BUILD_TYPE was set via -D
bool cmake_build_type_set = false;
for (const auto& def : definitions) {
    if (def.starts_with("CMAKE_BUILD_TYPE=") || def == "CMAKE_BUILD_TYPE") {
        cmake_build_type_set = true;
        break;
    }
}

// Set CMAKE_BUILD_TYPE if not already set
if (!cmake_build_type_set) {
    interpreter.set_variable("CMAKE_BUILD_TYPE", to_cmake_case(config));
}
```

### 4. Cache Location Verification

**No changes needed**: The cache is already stored at `<build_dir>/.dmake_cache` where `build_dir` is passed to the Interpreter constructor. Since we're computing `build_path` to include the config subdirectory, the cache will automatically be isolated per-configuration.

**Files that reference cache**:
- `dmake/build_system.cpp`: Reads/writes `.dmake_cache` in `build_dir_`
- No changes needed

## Files to Modify

### dmake-cli/main.cpp

**Changes**:
1. Add `--config` CLI option (~line 24)
2. Replace build directory logic (~line 43-53)
3. Add CMAKE_BUILD_TYPE setup (~line 117-135)

**Estimated lines**: ~40 new/modified lines

### Tests to Add

**File**: `tests/cli_tests.cpp` (create if doesn't exist)

Test cases:
1. Default config creates `build/debug`
2. `--config release` creates `build/release`
3. `-B custom` creates `custom/debug`
4. `-B custom --config release` creates `custom/release`
5. CMAKE_BUILD_TYPE is set correctly
6. `-D CMAKE_BUILD_TYPE=Custom` overrides default

## Verification Steps

1. **Build and test**:
   ```bash
   mkdir -p build && cd build && cmake .. && make
   ./dmake_tests
   ```

2. **Manual testing**:
   ```bash
   # Test default
   ./dmake /path/to/project
   ls -la /path/to/project/build/debug/

   # Test release
   ./dmake /path/to/project --config release
   ls -la /path/to/project/build/release/

   # Test custom build dir
   ./dmake /path/to/project -B out --config release
   ls -la /path/to/project/out/release/
   ```

3. **Verify isolation**:
   ```bash
   # Build debug and release, check for separate caches
   ./dmake /path/to/project --config debug
   ./dmake /path/to/project --config release

   ls -la /path/to/project/build/debug/.dmake_cache
   ls -la /path/to/project/build/release/.dmake_cache

   # Verify different content (should have different signatures)
   diff /path/to/project/build/debug/.dmake_cache \
        /path/to/project/build/release/.dmake_cache
   ```

## Edge Cases

### 1. Backward Compatibility

**Concern**: Users with existing `build/` directories (no subdirectories)

**Solution**: New default is `build/debug`. Old builds won't be used. This is acceptable since:
- Build directories are typically in .gitignore
- Cache invalidation is expected when upgrading build tools
- Clean rebuild is harmless

### 2. Case Sensitivity

**Directory names**: Always lowercase (`debug`, `release`)
**CMAKE_BUILD_TYPE**: Proper case (`Debug`, `Release`, `RelWithDebInfo`, `MinSizeRel`)

### 3. Custom Build Types

Users can use custom build types (e.g., `--config profile`):
- Directory: `build/profile`
- CMAKE_BUILD_TYPE: `Profile`
- No automatic compiler flags (user must configure via `target_compile_options`)

### 4. Path Conflicts

**Scenario**: `-B build/debug --config debug` → Would create `build/debug/debug`

**Solution**: Allow it. User explicitly chose this structure.

## Future Enhancements

### Global Compiler Flags

Currently, compiler flags must be set per-target using `target_compile_options()`. Future enhancement could add:

1. **CMAKE_CXX_FLAGS support**:
   ```cmake
   if(CMAKE_BUILD_TYPE STREQUAL "Debug")
       set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0")
   endif()
   ```

2. **Automatic flag injection** (like real CMake):
   - Set CMAKE_CXX_FLAGS_DEBUG, CMAKE_CXX_FLAGS_RELEASE, etc.
   - Apply them automatically in build_compile_command()

3. **Global compile options builtin**:
   ```cmake
   add_compile_options(-Wall -Wextra)
   ```

These are out of scope for the initial multi-config implementation.

## Testing Strategy

### Unit Tests

Add to `tests/interpreter_tests.cpp`:
- Verify CMAKE_BUILD_TYPE variable is set correctly
- Test conditional compilation based on build type

### Integration Tests

Manual testing checklist:
- [ ] Default build creates `build/debug`
- [ ] Release build creates `build/release`
- [ ] Custom build dir works
- [ ] Caches are isolated
- [ ] Switching configs doesn't cause unnecessary rebuilds within same config
- [ ] CMAKE_BUILD_TYPE is accessible in CMakeLists.txt
- [ ] Can override CMAKE_BUILD_TYPE with -D flag

## Timeline Estimate

- CLI changes: 1 hour
- Testing: 1 hour
- Documentation: 30 minutes
- **Total**: ~2.5 hours

## Questions & Decisions

### Q: Should we validate config names?

**Decision**: No. Allow arbitrary config names for flexibility. Users might want custom configs like "profile", "sanitize", etc.

### Q: What if user passes `-B build`?

**Decision**: Append config: `build/debug`, `build/release`. The `-B` flag sets the *root*, not the final directory.

### Q: Should we support `CMAKE_CONFIGURATION_TYPES` (multi-config generator)?

**Decision**: No. dmake is a single-config builder. Users switch configs by passing different `--config` flags, similar to Cargo.
