# Testing and Selective Building Design

This document outlines the design for integrated testing and selective target building in `dmake`.

## 1. Selective Building

To support large projects, `dmake` needs to build specific targets and their dependencies rather than the entire project.

### CLI UX
Selective building is triggered via the `--target` (or `-T`) flag.

```bash
# Build only specific targets
dmake . --target my_exe
dmake . -T lib1 -T lib2
```

### Implementation
1. **Target Selection**: The `Interpreter::run_build` method will accept a list of requested target names.
2. **Build Graph Filtering**: 
   - If targets are specified, `dmake` will only generate `BuildTask`s for those targets and their transitive dependencies.
   - If no targets are specified, it defaults to building all targets (current behavior).
3. **Dependency Resolution**: The system must ensure that requesting a target automatically pulls in all required libraries and object files.

## 2. Integrated Testing

`dmake` aims for a "batteries-included" testing experience similar to `cargo test`, while maintaining compatibility with CMake's testing commands.

### CLI UX
Testing is integrated into the main `dmake` command via the `--test` flag.

```bash
# Build and run all tests
dmake . --test

# Build and run tests matching a pattern
dmake . --test "Unit.*"

# Build only what is needed for a specific test and run it
dmake . --target my_test_exe --test
```

### CMake Compatibility Builtins
*   `enable_testing()`: Enables test registration for the current directory and below.
*   `add_test(NAME <name> COMMAND <cmd> [ARGS <args>...] [WORKING_DIRECTORY <dir>])`: Registers a test.
    - If `<cmd>` is a target name created by `add_executable`, `dmake` resolves it to the build output path.

### Two-Phase Execution
1. **Build Phase**: 
   - Tests are integrated into the build graph. 
   - If `--test` is active, all targets referenced in `add_test` commands are implicitly added to the requested targets list.
   - This ensures test binaries are up-to-date before execution.
2. **Test Phase**:
   - Executes after a successful build.
   - Runs tests in parallel using a thread pool.
   - Captures and buffers output, displaying it only on failure or in verbose mode.
   - Provides a clean summary of passed/failed tests.

## 3. Data Structures

### Test Definition
```cpp
struct TestDefinition {
    std::string name;
    std::string command; // Target name or path
    std::vector<std::string> args;
    std::string working_dir;
};
```

### Interpreter Additions
- `std::vector<TestDefinition> tests_`: Registry of all defined tests.
- `bool testing_enabled_`: Global toggle controlled by `enable_testing()`.

## 4. Implementation Steps

1. **Selective Building**:
   - Update `dmake-cli` to capture targets.
   - Update `Interpreter::run_build` and `Target::generate_tasks` to support filtering.
2. **Builtins**:
   - Implement `enable_testing` and `add_test`.
3. **Test Runner**:
   - Implement the post-build execution logic.
   - Add output buffering and summary reporting.
