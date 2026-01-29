# External Command Caching

## Overview

dmake implements a generic caching system for external commands in `execute_process`. This allows caching of commands like `pkg-config`, `llvm-config`, and other configuration tools that:
- Are expensive to run (4-20ms+ each)
- Are called repeatedly with the same arguments
- Have deterministic outputs based on filesystem state

## Architecture

### Cache Structure

```cpp
struct ExternalCommandCacheEntry {
    std::string stdout_output;      // Captured stdout
    std::string stderr_output;      // Captured stderr
    int exit_code;                  // Exit code
    std::map<std::string, std::optional<int64_t>> tracked_dir_mtimes;
};
```

The cache is stored in `.dmake_subsystem_cache.json` under `external_command_cache`.

### Cache Key (Signature)

The signature includes:
- Full command with all arguments
- Working directory (if specified)
- Relevant environment variables (command-specific)

### Cache Validation

Cache entries are invalidated when:
- Any tracked directory mtime changes
- A tracked directory appears/disappears
- Command arguments or environment variables change

## Currently Cached Commands

### pkg-config / pkgconf

**Tracked directories:**
- `/usr/lib/pkgconfig`
- `/usr/lib64/pkgconfig`
- `/usr/share/pkgconfig`
- `/usr/local/lib/pkgconfig`
- `/usr/local/lib64/pkgconfig`
- `/usr/local/share/pkgconfig`
- Paths from `PKG_CONFIG_PATH` environment variable
- Paths from `PKG_CONFIG_LIBDIR` environment variable

**Tracked environment variables:**
- `PKG_CONFIG_PATH`
- `PKG_CONFIG_SYSROOT_DIR`
- `PKG_CONFIG_LIBDIR`

**Performance:** Reduces ~14 pkg-config calls per package from 4-8ms each to essentially instant on cache hit.

## Adding New Command Types

To cache a new type of external command:

### 1. Add Command Detection

In `dmake/builtins/process.cpp`, add a detection function:

```cpp
// Helper: Check if command is your-tool
static bool is_your_tool_command(const std::string& cmd) {
    std::filesystem::path cmd_path(cmd);
    std::string filename = cmd_path.filename().string();
    return filename == "your-tool" || filename == "your-tool-bin";
}
```

### 2. Add Directory Tracking

Update `get_tracked_dir_mtimes()` to track relevant directories:

```cpp
static std::map<std::string, std::optional<int64_t>> get_tracked_dir_mtimes(const std::string& cmd) {
    std::map<std::string, std::optional<int64_t>> mtimes;

    if (is_pkgconfig_command(cmd)) {
        // ... existing pkg-config tracking
    } else if (is_your_tool_command(cmd)) {
        // Add directories that affect your tool's output
        mtimes["/usr/lib/your-tool"] = get_dir_mtime("/usr/lib/your-tool");
        mtimes["/etc/your-tool"] = get_dir_mtime("/etc/your-tool");

        // Track from environment variables if needed
        if (const char* custom_path = std::getenv("YOUR_TOOL_PATH")) {
            mtimes[custom_path] = get_dir_mtime(custom_path);
        }
    }

    return mtimes;
}
```

### 3. Add Environment Variable Tracking

Update `compute_command_signature()` to include relevant environment variables:

```cpp
static std::string compute_command_signature(
    const std::vector<std::vector<std::string>>& commands,
    const ProcessOptions& options
) {
    std::ostringstream oss;

    // ... existing command and args tracking

    // Existing pkg-config env vars
    if (const char* pkg_path = std::getenv("PKG_CONFIG_PATH")) {
        oss << "PKG_CONFIG_PATH:" << pkg_path << "|";
    }

    // Add your tool's env vars
    if (const char* your_var = std::getenv("YOUR_TOOL_CONFIG")) {
        oss << "YOUR_TOOL_CONFIG:" << your_var << "|";
    }

    return oss.str();
}
```

### 4. Update Cache Condition (Optional)

If your command has specific caching requirements, update the `can_cache` condition:

```cpp
bool is_your_tool = !commands.empty() && !commands[0].empty() && is_your_tool_command(commands[0][0]);
bool can_cache = (is_pkgconfig || is_your_tool) &&
                output_file.empty() &&
                error_file.empty() &&
                input_file.empty() &&
                (!output_variable.empty() || !result_variable.empty());
```

## Example: Adding llvm-config Caching

```cpp
// 1. Detection
static bool is_llvm_config_command(const std::string& cmd) {
    std::filesystem::path cmd_path(cmd);
    std::string filename = cmd_path.filename().string();
    return filename.find("llvm-config") != std::string::npos;
}

// 2. Directory tracking
static std::map<std::string, std::optional<int64_t>> get_tracked_dir_mtimes(const std::string& cmd) {
    std::map<std::string, std::optional<int64_t>> mtimes;

    if (is_pkgconfig_command(cmd)) {
        // ... existing
    } else if (is_llvm_config_command(cmd)) {
        // Track LLVM installation directories
        mtimes["/usr/lib/llvm"] = get_dir_mtime("/usr/lib/llvm");
        mtimes["/usr/lib/llvm-18"] = get_dir_mtime("/usr/lib/llvm-18");
        mtimes["/usr/local/lib/llvm"] = get_dir_mtime("/usr/local/lib/llvm");
    }

    return mtimes;
}

// 3. Update cache condition
bool is_llvm_config = !commands.empty() && !commands[0].empty() && is_llvm_config_command(commands[0][0]);
bool can_cache = (is_pkgconfig || is_llvm_config) &&
                output_file.empty() &&
                // ...
```

## Cache Inspection

View cached commands:

```bash
# Count cached entries
jq '.external_command_cache | length' build/debug/.dmake_subsystem_cache.json

# View all cached command signatures
jq -r '.external_command_cache | keys[]' build/debug/.dmake_subsystem_cache.json

# View a specific entry
jq '.external_command_cache | to_entries | .[0]' build/debug/.dmake_subsystem_cache.json
```

## Safety and Correctness

### Why This Is Safe

1. **Input Tracking**: Cache key includes full command, args, and relevant env vars
2. **Filesystem Tracking**: Tracks all directories that affect command output
3. **Mtime Validation**: Invalidates cache when any tracked directory changes
4. **Atomic Storage**: Cache uses temp file + atomic rename
5. **Graceful Degradation**: Corrupted cache results in clean rebuild

### What NOT to Cache

- Commands with side effects (writes, network calls)
- Commands with non-deterministic output
- Commands that depend on current time/date
- Commands with file redirects (already handled by `can_cache` check)

## Performance

**Lagrange project example:**
- Before caching: ~221 pkg-config calls at 4-8ms each = ~1.5 seconds
- After caching (second run): ~221 cache hits at ~0ms = essentially instant
- Speedup: Configuration phase ~1.5s faster on incremental builds

## Implementation Details

**Location:** `dmake/builtins/process.cpp`

**Key functions:**
- `is_pkgconfig_command()` - Command detection
- `get_tracked_dir_mtimes()` - Directory tracking (extensible)
- `compute_command_signature()` - Cache key generation
- `validate_command_cache()` - Cache validation via mtime checks
- Main logic in `execute_process` builtin around line 230

**Cache storage:** `dmake/cache_store.hpp`
- `CacheSubsystem::ExternalCommand`
- `ExternalCommandCacheEntry` structure
- Serialized to JSON via Glaze library
