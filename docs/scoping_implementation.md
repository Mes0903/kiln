# Variable Scoping Implementation

## Document Purpose

This document describes dmake's current variable scoping implementation, documents critical bugs discovered during development, and provides rationale for a future Shadow Map-based rewrite.

## Current Implementation Overview

dmake implements CMake's scoping semantics using four primary data structures in the `Interpreter` class:

1. **`call_stack_`** - Deque of `CallFrame` containing variables + metadata (script_dir, function_block pointer)
2. **`trace_stack_`** - Vector of `CallLocation` for error reporting backtraces (kept separate!)
3. **`macro_substitutions_`** - Flat map for macro parameter text substitution
4. **`cache_variables_`** - Global cache namespace (stored in root interpreter)

### Data Structures

```cpp
struct CallFrame {
    std::string script_dir;                              // Directory context
    std::unordered_map<std::string, std::string> variables;  // Variable storage (THIS is what Shadow Map replaces)
    const FunctionBlock* function_block = nullptr;       // For CMAKE_CURRENT_FUNCTION*
};

struct CallLocation {
    std::string file;
    size_t row, col, offset, length;
    std::string command;  // Command name for backtrace
};

class Interpreter {
    // Function call stack for scope management
    std::deque<CallFrame> call_stack_;

    // Error reporting stack (SEPARATE from call_stack_!)
    std::vector<CallLocation> trace_stack_;

    // Macro parameter text substitutions
    std::map<std::string, std::string> macro_substitutions_;

    // Cache variables (stored in root interpreter only)
    std::map<std::string, std::string> cache_variables_;

    // Parent interpreter for scope hierarchy
    Interpreter* parent_;
};
```

**Critical Insight**: The `call_stack_` and `trace_stack_` serve different purposes:
- **`call_stack_`**: Variable storage + metadata (script_dir, function_block)
- **`trace_stack_`**: Error backtraces (command call chain)

The Shadow Map design replaces the **`variables` field in `CallFrame`**, but must preserve the rest of the metadata.

### Variable Lookup Order (get_variable)

The current implementation searches for variables in this order:

1. **Macro substitutions** - Check `macro_substitutions_` for macro parameters
2. **Call stack** - Search function frames from top to bottom
3. **Parent scopes** - Recursively walk up `parent_` chain
4. **Cache variables** - Check `cache_variables_` in root interpreter

**Critical Issue**: This order caused a major bug where macro parameters bled into function scope.

## CMake Scoping Semantics

### Functions vs Macros

**Functions:**
- Create a new variable scope (new call stack frame)
- Parameters are function-local variables
- Local variables don't leak to caller
- Can read parent scope variables
- `PARENT_SCOPE` modifier sets variable in caller's scope

**Macros:**
- Text substitution, no new scope
- Parameters are text-replaced in macro body
- Variables set in macro leak to caller
- `${ARGC}`, `${ARGV}`, `${ARGN}` refer to macro parameters

### Cache Variables

- **Global namespace**: Accessible from all scopes
- **Not bound to local scope**: `set(VAR value CACHE ...)` only sets in cache
- **Shadowing**: Local variables can shadow cache variables

### Scope Hierarchy

```
Root Scope (project directory)
├─ Subdirectory Scope (add_subdirectory)
│  ├─ Function Call Frame 1
│  │  └─ Function Call Frame 2 (nested)
│  └─ Subdirectory Scope (nested add_subdirectory)
└─ Function Call Frame (independent function call)
```

## Critical Bugs Discovered

### Bug 1: Macro Parameters Bleed Into Function Scope

**Symptom**: Functions called from within macros see the macro's ARGV/ARGC instead of their own parameters.

**Root Cause**: In `get_variable()`, macro_substitutions_ is checked BEFORE the call stack:

```cpp
// BUGGY CODE (before fix)
std::string Interpreter::get_variable(const std::string& name) const {
    // Check macro substitutions FIRST
    auto macro_it = macro_substitutions_.find(name);
    if (macro_it != macro_substitutions_.end()) {
        return macro_it->second;  // ❌ Returns macro's ARGV even in function
    }

    // Check call stack SECOND
    for (auto it = call_stack_.rbegin(); it != call_stack_.rend(); ++it) {
        auto var_it = it->find(name);
        if (var_it != it->end()) {
            return var_it->second;
        }
    }
    // ...
}
```

**Example Failure**:
```cmake
function(my_func arg1 arg2)
    message("ARGC=${ARGC}")  # Expected: 2
    message("arg1=${arg1}")   # Expected: "func_value"
endfunction()

macro(my_macro mac_arg)
    my_func("func_value" "another")
endmacro()

my_macro("macro_value")
# BUGGY OUTPUT: ARGC=1, arg1=macro_value (WRONG!)
# CORRECT OUTPUT: ARGC=2, arg1=func_value
```

**Fix**: Save and clear `macro_substitutions_` when entering function scope, restore on exit:

```cpp
// In interpret() when handling FunctionCall
std::map<std::string, std::string> saved_macro_substitutions = macro_substitutions_;
macro_substitutions_.clear();  // ✅ Isolate function from outer macro scope

call_stack_.push_back(vars);
auto result = interpret(body);
call_stack_.pop_back();

macro_substitutions_ = saved_macro_substitutions;  // ✅ Restore for caller
```

**Lesson**: Macro parameter substitutions must NEVER be visible inside functions, regardless of calling context.

### Bug 2: Cache Variables Not Globally Accessible

**Symptom**: Variables set with `CACHE` keyword were not accessible from functions.

**Root Cause**: `get_variable()` never checked `cache_variables_` after exhausting local and parent scopes.

**Example Failure**:
```cmake
set(MY_CACHE "value" CACHE STRING "Test")

function(read_cache)
    message("${MY_CACHE}")  # Expected: "value", Got: "" (empty)
endfunction()

read_cache()
```

**Fix**: Add cache variable lookup after parent scope search:

```cpp
std::string Interpreter::get_variable(const std::string& name) const {
    // ... search macro_substitutions_, call_stack_, parent_ ...

    // Check cache variables (globally accessible)
    auto cache_it = get_root()->cache_variables_.find(name);
    if (cache_it != get_root()->cache_variables_.end()) {
        return cache_it->second;  // ✅ Cache variables visible everywhere
    }

    return "";  // Undefined variable
}
```

**Lesson**: Cache variables are part of a global namespace, not bound to any scope.

### Bug 3: Cache Variables Created Local Scope Variables

**Symptom**: `set(VAR value CACHE ...)` created BOTH a cache variable AND a local scope variable.

**Root Cause**: The `set()` builtin always set a local variable after setting the cache variable.

**Example Failure**:
```cmake
function(test_cache)
    set(VAR "from_func" CACHE STRING "Test")
endfunction()

set(VAR "from_root" CACHE STRING "Test")
message("Before: ${VAR}")  # Output: "from_root"
test_cache()
message("After: ${VAR}")   # Expected: "from_func", Got: "from_root"
# Bug: VAR was set as function-local, didn't affect cache
```

**Fix**: Only set in cache namespace when `CACHE` is used:

```cpp
// In set() builtin
if (cache_type_idx != std::string::npos) {
    // Set in cache namespace only
    auto* root = interp.get_root();
    root->cache_variables_[var_name] = value;
    return;  // ✅ Don't also set as local variable
}

// Set as regular variable (local scope)
interp.set_variable(var_name, value);
```

**Lesson**: Cache variables and local variables are completely separate namespaces.

### Bug 4: file(STRINGS) REGEX Used Full String Match

**Symptom**: `file(STRINGS file.txt lines REGEX "pattern")` only matched lines where the entire line matched the regex.

**Root Cause**: Used `std::regex_match()` (full string) instead of `std::regex_search()` (substring).

**Example Failure**:
```cmake
# File contains: "This line has ERROR in it"
file(STRINGS file.txt errors REGEX "ERROR")
# Expected: ["This line has ERROR in it"]
# Got: [] (no matches because entire line doesn't match "ERROR")
```

**Fix**: Use `std::regex_search()` and set `CMAKE_MATCH_*` variables:

```cpp
if (regex_filter) {
    std::smatch match;
    if (!std::regex_search(current_string, match, *regex_filter)) {
        current_string.clear();  // Line doesn't match, skip it
        return;
    }

    // Store capture groups for CMAKE_MATCH_* variables
    last_match_groups.clear();
    for (size_t i = 0; i < match.size(); ++i) {
        last_match_groups.push_back(match[i].str());
    }
}

// After extraction, set CMAKE_MATCH_* variables
if (!last_match_groups.empty()) {
    interp.set_variable("CMAKE_MATCH_COUNT", std::to_string(last_match_groups.size() - 1));
    for (size_t i = 0; i < last_match_groups.size() && i < 10; ++i) {
        interp.set_variable("CMAKE_MATCH_" + std::to_string(i), last_match_groups[i]);
    }
}
```

**Lesson**: CMake's REGEX matching is substring-based, not full-string.

## Performance Issues with Current Design

### Problem 1: Manual Save/Restore Everywhere

Every scope boundary requires manual save/restore logic:

```cpp
// Entering function
auto saved_macro_subs = macro_substitutions_;
macro_substitutions_.clear();
call_stack_.push_back(new_frame);

// ... execute function body ...

// Exiting function
call_stack_.pop_back();
macro_substitutions_ = saved_macro_subs;
```

**Issues**:
- Error-prone (easy to forget restore)
- Not exception-safe without RAII
- Boilerplate code duplication

### Problem 2: Linear Search on Every Variable Access

`get_variable()` performs multiple linear searches:

1. Hash lookup in `macro_substitutions_` - O(1)
2. Linear search through `call_stack_` frames - O(call_depth * vars_per_frame)
3. Recursive parent traversal - O(parent_depth)
4. Hash lookup in `cache_variables_` - O(1)

**Worst case**: Deep function nesting with many variables per frame.

### Problem 3: Expensive Scope Creation

Creating a new function scope allocates a new `std::map<std::string, std::string>`:

```cpp
call_stack_.push_back({}); // Allocates new map
```

This happens on EVERY function call, even for functions with no local variables.

### Problem 4: Complex State Management

Three separate data structures (`call_stack_`, `macro_substitutions_`, `cache_variables_`) must be kept synchronized:

- Macros modify `macro_substitutions_`
- Functions modify `call_stack_`
- Cache variables modify root's `cache_variables_`
- Parent scopes require `parent_` traversal

This complexity increases cognitive load and bug surface area.

## Shadow Map Design Proposal

### Core Concept

A **Shadow Map** is a single unified data structure that stores ALL variables across ALL scopes using depth-tagged version histories. Each variable maps to a vector of `(depth, value)` pairs, where only the topmost entry is visible.

```cpp
class ShadowMap {
    // Single map for all variables: name -> [(depth, value), ...]
    // The vector acts as a version stack - only the last entry is visible
    std::unordered_map<std::string, std::vector<std::pair<int, std::string>>> variables_;

    // Current scope depth (incremented on scope entry)
    int current_depth_ = 0;

    // Track which variables were pushed at each depth (for cleanup on scope exit)
    // modified_per_depth_[depth] = set of variable names that pushed at that depth
    std::vector<std::unordered_set<std::string>> modified_per_depth_;

    // O(1) lookup - just return the topmost entry
    std::string get(const std::string& name) const {
        auto it = variables_.find(name);
        if (it == variables_.end() || it->second.empty()) {
            return "";  // Variable not defined
        }
        return it->second.back().second;  // Return most recent value
    }

    // O(1) set - push if new depth, modify if same depth
    void set(const std::string& name, const std::string& value) {
        auto& versions = variables_[name];

        if (versions.empty() || versions.back().first < current_depth_) {
            // New depth level - PUSH new version
            versions.emplace_back(current_depth_, value);
            modified_per_depth_[current_depth_].insert(name);
        } else {
            // Same depth - MODIFY existing version
            versions.back().second = value;
        }
    }

    // Enter new scope
    void push_scope() {
        current_depth_++;
        modified_per_depth_.emplace_back();  // New tracking set for this depth
    }

    // Exit scope - pop all variables modified at current depth
    void pop_scope() {
        for (const auto& var_name : modified_per_depth_[current_depth_]) {
            auto& versions = variables_[var_name];
            versions.pop_back();  // Remove this depth's version
        }
        modified_per_depth_.pop_back();
        current_depth_--;
    }
};
```

### Key Benefits

#### 1. True O(1) Variable Access

No parent traversal, no linear search - just hash lookup + vector back:

```cpp
// Current implementation: O(call_depth * vars_per_frame)
for (auto it = call_stack_.rbegin(); it != call_stack_.rend(); ++it) {
    auto var_it = it->find(name);  // Search each frame
    if (var_it != it->end()) return var_it->second;
}

// Shadow Map: O(1)
return variables_[name].back().second;  // Single hash lookup
```

#### 2. No Manual Save/Restore

Current implementation requires manual backup:

```cpp
// BEFORE: Error-prone manual management
auto saved_macro_subs = macro_substitutions_;
macro_substitutions_.clear();
call_stack_.push_back(new_frame);
// ... execute ...
call_stack_.pop_back();
macro_substitutions_ = saved_macro_subs;  // Easy to forget!
```

Shadow Map is automatic:

```cpp
// AFTER: Automatic cleanup
shadow_map_.push_scope();
// ... execute ...
shadow_map_.pop_scope();  // Automatically pops all variables at this depth
```

#### 3. Automatic Shadowing

Variables naturally shadow outer scopes because `.back()` always returns the most recent version:

```cmake
set(VAR "depth_0")  # variables_["VAR"] = [(0, "depth_0")]

function(test)
    message("${VAR}")      # Output: "depth_0" (from depth 0)
    set(VAR "depth_1")     # PUSH: [(0, "depth_0"), (1, "depth_1")]
    message("${VAR}")      # Output: "depth_1" (from depth 1)
    set(VAR "modified")    # MODIFY: [(0, "depth_0"), (1, "modified")]
    message("${VAR}")      # Output: "modified" (still depth 1)
endfunction()

test()                     # On exit: POP depth 1 -> [(0, "depth_0")]
message("${VAR}")          # Output: "depth_0" (back to root)
```

#### 4. Unified Data Structure

All variables (regular, cache, macro params) use the same map - no need to synchronize separate data structures.

```cpp
// Current: Three separate structures to manage
std::vector<std::map<std::string, std::string>> call_stack_;
std::map<std::string, std::string> macro_substitutions_;
std::map<std::string, std::string> cache_variables_;

// Shadow Map: One structure
ShadowMap shadow_map_;
```

#### 5. Minimal Allocations

Only allocates when a variable is first set at a new depth:

```cpp
// Current: Allocates a new map on EVERY function call
call_stack_.push_back({});  // New std::map

// Shadow Map: Only allocates if variable is actually modified
// If function doesn't set any variables, zero allocation overhead
```

### Proposed Architecture

The key insight is to **separate concerns**:
- **Variable storage**: Shadow Map (O(1) lookup, automatic cleanup)
- **Metadata storage**: Lightweight frame stack (script_dir, function_block)
- **Error reporting**: Keep `trace_stack_` unchanged

```cpp
// Lightweight metadata frame (no variables!)
struct FrameMetadata {
    std::string script_dir;
    const FunctionBlock* function_block = nullptr;
};

class Interpreter {
    // Variable storage - O(1) lookup across all scopes
    ShadowMap shadow_map_;

    // Metadata stack - only stores context info (NOT variables)
    std::deque<FrameMetadata> frame_stack_;

    // Error reporting stack - UNCHANGED, keep separate for backtraces
    std::vector<CallLocation> trace_stack_;

    // Separate map for cache variables (global namespace)
    ShadowMap cache_map_;

    // Macro parameter substitution (keep separate for clarity)
    std::map<std::string, std::string> macro_substitutions_;

    // Parent interpreter (for add_subdirectory)
    Interpreter* parent_;
};
```

**Why keep frame_stack_ separate from Shadow Map?**

1. **Metadata doesn't need versioning**: script_dir and function_block are context, not variables
2. **Different lifecycle**: Frames need to persist for CMAKE_CURRENT_FUNCTION lookups
3. **Clarity**: Separates "what directory am I in" from "what variables exist"

### Detailed Example Walkthrough

Consider this CMake script:

```cmake
set(VAR "root")        # Depth 0

function(outer)
    set(VAR "outer")   # Depth 1
    set(LOCAL "temp")  # Depth 1

    function(inner)
        message("${VAR}")     # Reads from depth 1
        set(VAR "inner")      # Depth 2
        message("${LOCAL}")   # Reads from depth 1
    endfunction()

    inner()
    message("${VAR}")  # Reads from depth 1 (inner's change is gone)
endfunction()

outer()
message("${VAR}")      # Reads from depth 0 (back to root)
```

**Shadow Map state transitions:**

```
Initial (depth=0):
  variables_ = {}
  current_depth_ = 0
  modified_per_depth_ = [{}]

After "set(VAR "root")":
  variables_ = {
    "VAR": [(0, "root")]
  }
  modified_per_depth_[0] = {"VAR"}

Enter outer() (depth=1):
  current_depth_ = 1
  modified_per_depth_ = [{...}, {}]

After "set(VAR "outer")":
  variables_ = {
    "VAR": [(0, "root"), (1, "outer")]  # PUSH (depth changed)
  }
  modified_per_depth_[1] = {"VAR"}

After "set(LOCAL "temp")":
  variables_ = {
    "VAR": [(0, "root"), (1, "outer")],
    "LOCAL": [(1, "temp")]  # New variable
  }
  modified_per_depth_[1] = {"VAR", "LOCAL"}

Enter inner() (depth=2):
  current_depth_ = 2
  modified_per_depth_ = [{...}, {...}, {}]

After "set(VAR "inner")":
  variables_ = {
    "VAR": [(0, "root"), (1, "outer"), (2, "inner")],  # PUSH
    "LOCAL": [(1, "temp")]
  }
  modified_per_depth_[2] = {"VAR"}

Exit inner() (depth=1):
  # Pop all variables in modified_per_depth_[2]
  variables_["VAR"].pop_back()  # Remove (2, "inner")
  variables_ = {
    "VAR": [(0, "root"), (1, "outer")],  # Back to outer's version
    "LOCAL": [(1, "temp")]
  }
  current_depth_ = 1

Exit outer() (depth=0):
  # Pop all variables in modified_per_depth_[1]
  variables_["VAR"].pop_back()   # Remove (1, "outer")
  variables_["LOCAL"].pop_back()  # Remove (1, "temp")
  variables_ = {
    "VAR": [(0, "root")],     # Back to root
    "LOCAL": []               # Empty (essentially deleted)
  }
  current_depth_ = 0
```

### Scope Lifecycle Examples

**Function Call**:
```cpp
// Before (current implementation)
auto saved_macro_subs = macro_substitutions_;
macro_substitutions_.clear();
call_stack_.push_back(new_frame);
// ... execute ...
call_stack_.pop_back();
macro_substitutions_ = saved_macro_subs;

// After (Shadow Map)
shadow_map_.push_scope();  // Increment depth, add tracking set
// ... execute ...
shadow_map_.pop_scope();   // Automatically pops all modified variables
```

**Macro Call**:
```cpp
// Macros DON'T increment depth - they share the caller's depth
// This is why macro variables "leak" to the caller

// Before (current implementation)
for (auto& [name, value] : macro_params) {
    macro_substitutions_[name] = value;
}
// ... execute ...
for (auto& [name, _] : macro_params) {
    macro_substitutions_.erase(name);
}

// After (Shadow Map)
// Set macro parameters at current depth
for (auto& [name, value] : macro_params) {
    shadow_map_.set(name, value);  // Modifies at current_depth_
}
// ... execute (variables set in macro stay at current depth) ...
// On function return, caller's depth pops everything
```

### Error Reporting and Stack Traces

**Critical requirement**: Error messages must show the full call chain for debugging.

**Current implementation** uses `trace_stack_`:

```cpp
void Interpreter::set_fatal_error(const std::string& message) {
    Interpreter* root = get_root();
    std::vector<CallLocation> backtrace;

    // Build backtrace from trace_stack_ (NOT call_stack_!)
    for (size_t i = 0; i < root->trace_stack_.size() - 1; ++i) {
        backtrace.push_back(root->trace_stack_[i]);
    }

    const auto& current = root->trace_stack_.back();
    set_fatal_error(InterpreterError{
        current.file, current.row, current.col,
        current.offset, current.length,
        message, backtrace
    });
}
```

**With Shadow Map**: `trace_stack_` is **completely unchanged**.

Example error output (preserved):
```
error: undefined variable 'FOO'
  --> /path/to/CMakeLists.txt:42:5
     |
  42 |     message("${FOO}")
     |             ^^^^^^^

Backtrace:
  1. my_function() called from CMakeLists.txt:10
  2. outer_function() called from CMakeLists.txt:5
  3. if block at CMakeLists.txt:3
```

**Shadow Map has ZERO impact on error reporting** - it only affects variable storage, not the call trace.

### Implementation Considerations

#### Handling PARENT_SCOPE

When `set(VAR value PARENT_SCOPE)` is used, we need to modify the variable at `current_depth_ - 1`:

```cpp
void set_parent_scope(const std::string& name, const std::string& value) {
    if (current_depth_ == 0) {
        // No parent scope - error or ignore
        return;
    }

    auto& versions = variables_[name];
    int target_depth = current_depth_ - 1;

    // Find or create entry at parent depth
    if (!versions.empty() && versions.back().first == target_depth) {
        // Parent depth entry exists - modify it
        versions.back().second = value;
    } else {
        // Need to insert at parent depth
        versions.emplace_back(target_depth, value);
        modified_per_depth_[target_depth].insert(name);
    }
}
```

#### Macro Parameter Handling

Macros don't create new scopes - they operate at the caller's depth. To isolate macro parameters from the caller's actual variables:

**Option 1: Separate macro parameter map**
```cpp
// Keep macro_substitutions_ as a separate map
// Check it BEFORE shadow_map_ in get_variable()
std::map<std::string, std::string> macro_substitutions_;
```

**Option 2: Use negative depth for macro params**
```cpp
// Macro parameters at depth = current_depth_ + 0.5 (use float depth)
// Or use a separate "layer" flag in the pair
std::pair<int, bool> depth_and_layer;  // (depth, is_macro_param)
```

**Recommended**: Keep macro_substitutions_ separate for clarity - macro text substitution is semantically different from variable scoping.

#### Cache Variables

Cache variables are global and should never be popped. Options:

**Option 1: Separate cache map**
```cpp
ShadowMap cache_map_;  // Never push/pop scope, always at depth 0
```

**Option 2: Special depth marker**
```cpp
// Use depth = -1 for cache variables
variables_["CACHE_VAR"] = [(-1, "value")];
// Never pop entries with depth == -1
```

**Recommended**: Separate map for clarity and to avoid special-case logic in hot paths.

#### add_subdirectory() Scopes

Subdirectories create parent-child interpreter relationships. Variables should be inherited but not modified:

```cpp
class Interpreter {
    Interpreter* parent_;
    ShadowMap shadow_map_;

    std::string get_variable(const std::string& name) {
        // Check local shadow map first
        if (auto val = shadow_map_.get(name); !val.empty()) {
            return val;
        }

        // Check parent interpreter
        if (parent_) {
            return parent_->get_variable(name);
        }

        // Check cache (global)
        return cache_map_.get(name);
    }
};
```

### Migration Strategy

1. **Phase 1**: Implement `ShadowMap` class with comprehensive unit tests
   - Test push/pop/get/set operations
   - Test depth tracking and cleanup
   - Test edge cases (empty scopes, deep nesting, etc.)

2. **Phase 2**: Add compatibility layer in `Interpreter`
   - Keep existing data structures
   - Add `ShadowMap shadow_map_` alongside them
   - Dual-write to both old and new structures
   - Assert they stay in sync

3. **Phase 3**: Switch reads to use `ShadowMap`
   - Modify `get_variable()` to read from `shadow_map_`
   - Keep writing to both structures
   - Run full test suite to verify correctness

4. **Phase 4**: Switch writes to use only `ShadowMap`
   - Modify `set_variable()` to only write to `shadow_map_`
   - Remove dual-write logic from Phase 2

5. **Phase 5**: Remove old data structures
   - Delete `call_stack_`, `macro_substitutions_` (keep if separating concerns)
   - Simplify `get_variable()` / `set_variable()` logic
   - Run full test suite + integration tests

6. **Phase 6**: Performance validation
   - Benchmark variable access patterns
   - Verify zero regressions in correctness
   - Measure memory usage improvements

### Compatibility Guarantee

The Shadow Map refactor is **purely internal** - no user-facing behavior changes:

- All existing tests must pass
- CMake semantics remain identical
- No changes to builtin command APIs

### Expected Performance Improvements

- **Scope creation**: O(1) - just increment depth counter and add empty tracking set (vs allocating new std::map)
- **Variable lookup**: O(1) - single hash lookup + vector `.back()` (vs linear search through call_stack_ frames)
- **Variable write**: O(1) - hash lookup, check depth, push or modify (vs creating new map entries)
- **Scope destruction**: O(modified_vars) - only pop variables that were actually modified at this depth
- **Memory usage**: Minimal overhead - only stores versions when variables are actually shadowed
- **Code complexity**: ~70% reduction - no manual save/restore logic anywhere

### Performance Analysis

**Current Implementation:**
- Function call: Allocate `std::map`, copy parent variables if needed - **O(parent_vars)**
- Variable lookup: Linear search through call stack - **O(call_depth)**
- Scope exit: Pop map, restore macro_substitutions - **O(1)** but requires manual tracking

**Shadow Map:**
- Function call: Increment counter, add tracking set - **O(1)**
- Variable lookup: Hash lookup, return `.back()` - **O(1)**
- Scope exit: Pop tracked variables - **O(modified_vars_at_depth)**

**Best case (function reads variables but doesn't modify)**:
- Current: Still allocates map - O(1) allocation
- Shadow Map: Zero allocations - O(1) counter increment

**Typical case (function modifies 3-5 variables)**:
- Current: O(call_depth) per variable read
- Shadow Map: O(1) per variable read

**Worst case (function modifies many variables at deep nesting)**:
- Both: O(modified_vars) on scope exit
- Shadow Map advantage: No redundant parent variable storage

## Testing Strategy

### Regression Tests Added

All discovered bugs have corresponding test cases in `tests/interpreter_tests.cpp`:

1. `"Function called from macro has correct ARGV/ARGC"` - Bug 1
2. `"Nested macro/function calls preserve correct scope"` - Bug 1 (deep nesting)
3. `"Cache variables are globally accessible"` - Bug 2
4. `"Cache variables don't create local scope variables"` - Bug 3
5. `"file(STRINGS) REGEX uses substring matching"` - Bug 4
6. `"file(STRINGS) REGEX sets CMAKE_MATCH_* variables"` - Bug 4

### Pre-Rewrite Test Coverage

Before starting the Shadow Map rewrite, ensure:

- All 21 integration tests pass
- All unit tests pass (4997 lines of test code)
- No memory leaks (valgrind clean)
- No undefined behavior (UBSAN clean)

### Post-Rewrite Validation

After Shadow Map implementation:

- All existing tests must still pass (bit-for-bit compatibility)
- New micro-benchmarks show performance improvements
- Code review confirms reduced complexity

## References

- **CMake Documentation**: https://cmake.org/cmake/help/latest/manual/cmake-language.7.html
- **Shadow Map Pattern**: Common in language runtimes (V8, SpiderMonkey)
- **Related Bugs**: See `git log --grep="ARGV\|cache\|scope"` for full history

## Future Work

### Potential Enhancements After Shadow Map

1. **Lazy Variable Expansion**: Delay `${}` expansion until variable is actually used
2. **Copy-on-Write Scopes**: Share parent scope data until modification
3. **Variable Type Tracking**: Store types alongside values for better error messages
4. **Scope Introspection**: Add builtins for debugging scope chains

### Known Limitations

- Shadow Map doesn't solve **macro variable leakage** (that's by design in CMake)
- Won't improve performance of regex matching or file I/O
- Still requires parent traversal for undefined variable checks

## Complete Implementation Pseudocode

Here's a complete reference implementation of the Shadow Map design:

```cpp
class ShadowMap {
private:
    // Core data structure: variable name -> [(depth, value), ...]
    std::unordered_map<std::string, std::vector<std::pair<int, std::string>>> variables_;

    // Current scope depth (0 = root)
    int current_depth_ = 0;

    // Track which variables were pushed at each depth for cleanup
    std::vector<std::unordered_set<std::string>> modified_per_depth_;

public:
    ShadowMap() {
        modified_per_depth_.emplace_back();  // Depth 0 tracking set
    }

    // Get variable - O(1)
    std::string get(const std::string& name) const {
        auto it = variables_.find(name);
        if (it == variables_.end() || it->second.empty()) {
            return "";  // Undefined
        }
        return it->second.back().second;  // Most recent value
    }

    // Set variable - O(1)
    void set(const std::string& name, const std::string& value) {
        auto& versions = variables_[name];

        if (versions.empty() || versions.back().first < current_depth_) {
            // New depth - push new version
            versions.emplace_back(current_depth_, value);
            modified_per_depth_[current_depth_].insert(name);
        } else {
            // Same depth - modify existing version
            assert(versions.back().first == current_depth_);
            versions.back().second = value;
            // Already in modified set, no need to insert again
        }
    }

    // Set in parent scope (for PARENT_SCOPE keyword)
    void set_parent_scope(const std::string& name, const std::string& value) {
        if (current_depth_ == 0) {
            // No parent - either error or treat as regular set
            set(name, value);
            return;
        }

        int target_depth = current_depth_ - 1;
        auto& versions = variables_[name];

        // Search for existing entry at parent depth
        bool found = false;
        for (auto& [depth, val] : versions) {
            if (depth == target_depth) {
                val = value;  // Modify existing
                found = true;
                break;
            }
        }

        if (!found) {
            // Insert at parent depth
            versions.emplace_back(target_depth, value);
            modified_per_depth_[target_depth].insert(name);
        }
    }

    // Unset variable (remove at current depth)
    void unset(const std::string& name) {
        auto& versions = variables_[name];
        if (!versions.empty() && versions.back().first == current_depth_) {
            versions.pop_back();
            // Note: we still keep it in modified_per_depth_ for safety
        }
    }

    // Check if variable is defined
    bool is_defined(const std::string& name) const {
        auto it = variables_.find(name);
        return it != variables_.end() && !it->second.empty();
    }

    // Enter new scope (function call, block(), etc.)
    void push_scope() {
        current_depth_++;
        modified_per_depth_.emplace_back();  // New tracking set
    }

    // Exit scope - automatically clean up all modifications at this depth
    void pop_scope() {
        assert(current_depth_ > 0);

        // Pop all variables that were pushed at current depth
        for (const auto& var_name : modified_per_depth_[current_depth_]) {
            auto& versions = variables_[var_name];
            if (!versions.empty() && versions.back().first == current_depth_) {
                versions.pop_back();
            }
        }

        modified_per_depth_.pop_back();
        current_depth_--;
    }

    // Get current depth (for debugging)
    int depth() const { return current_depth_; }

    // Debug print
    void dump() const {
        std::cout << "ShadowMap (depth=" << current_depth_ << "):\n";
        for (const auto& [name, versions] : variables_) {
            std::cout << "  " << name << ": ";
            for (const auto& [depth, value] : versions) {
                std::cout << "(" << depth << ":" << value << ") ";
            }
            std::cout << "\n";
        }
    }
};

// Lightweight metadata (no variables!)
struct FrameMetadata {
    std::string script_dir;
    const FunctionBlock* function_block = nullptr;
};

// Interpreter integration
class Interpreter {
private:
    ShadowMap shadow_map_;              // Regular variables
    ShadowMap cache_map_;               // Cache variables (global, never push/pop)
    std::map<std::string, std::string> macro_substitutions_;  // Macro params (separate)

    std::deque<FrameMetadata> frame_stack_;       // Metadata only (script_dir, function_block)
    std::vector<CallLocation> trace_stack_;       // Error backtraces (UNCHANGED)

    Interpreter* parent_;               // Parent interpreter (add_subdirectory)

public:
    std::string get_variable(const std::string& name) const {
        // Special variables from frame metadata
        if (name == "CMAKE_CURRENT_SOURCE_DIR" || name == "CMAKE_CURRENT_LIST_DIR") {
            return frame_stack_.front().script_dir;
        }
        if (name == "CMAKE_CURRENT_FUNCTION") {
            const auto* fb = frame_stack_.front().function_block;
            return fb ? fb->name : "";
        }
        if (name == "CMAKE_CURRENT_FUNCTION_LIST_FILE") {
            const auto* fb = frame_stack_.front().function_block;
            return fb ? fb->definition_file : "";
        }

        // 1. Check macro substitutions (if in macro context)
        if (!macro_substitutions_.empty()) {
            auto it = macro_substitutions_.find(name);
            if (it != macro_substitutions_.end()) {
                return it->second;
            }
        }

        // 2. Check local shadow map
        std::string value = shadow_map_.get(name);
        if (!value.empty()) {
            return value;
        }

        // 3. Check parent interpreter (add_subdirectory scope)
        if (parent_) {
            value = parent_->get_variable(name);
            if (!value.empty()) {
                return value;
            }
        }

        // 4. Check cache (global)
        return cache_map_.get(name);
    }

    void set_variable(const std::string& name, const std::string& value) {
        shadow_map_.set(name, value);
    }

    void set_cache_variable(const std::string& name, const std::string& value) {
        cache_map_.set(name, value);  // Never popped
    }

    // Function call
    void enter_function(const FunctionBlock* func, const std::string& script_dir) {
        // Push metadata frame
        frame_stack_.push_front({script_dir, func});

        // Push trace for error reporting (UNCHANGED)
        trace_stack_.push_back({current_file_, func->row, func->col, func->offset, func->length, func->name});

        // Clear macro substitutions so function doesn't see outer macro params
        saved_macro_substitutions_ = macro_substitutions_;
        macro_substitutions_.clear();

        // Push variable scope
        shadow_map_.push_scope();
    }

    void exit_function() {
        // Pop all three stacks in reverse order
        shadow_map_.pop_scope();
        macro_substitutions_ = saved_macro_substitutions_;
        trace_stack_.pop_back();
        frame_stack_.pop_front();
    }

    // Macro call - DON'T push scope (variables leak to caller)
    void enter_macro(const std::map<std::string, std::string>& params, const MacroBlock* macro) {
        // Push trace for error reporting (macros appear in backtrace)
        trace_stack_.push_back({current_file_, macro->row, macro->col, macro->offset, macro->length, macro->name});

        // Set macro parameters (no scope push!)
        macro_substitutions_ = params;
    }

    void exit_macro() {
        macro_substitutions_.clear();
        trace_stack_.pop_back();
    }

    // Error reporting (COMPLETELY UNCHANGED)
    void set_fatal_error(const std::string& message) {
        std::vector<CallLocation> backtrace;
        for (size_t i = 0; i < trace_stack_.size() - 1; ++i) {
            backtrace.push_back(trace_stack_[i]);
        }
        const auto& current = trace_stack_.back();
        set_fatal_error(InterpreterError{
            current.file, current.row, current.col,
            current.offset, current.length,
            message, backtrace
        });
    }
};
```

## Three-Stack Architecture Summary

The Shadow Map design maintains **three separate stacks** for different concerns:

### 1. Shadow Map (Variable Storage)
**Purpose**: Store and retrieve variable values with automatic shadowing

**Structure**: Single flat map with depth-tagged version history
```cpp
unordered_map<string, vector<pair<int, string>>> variables_;
```

**Operations**:
- `push_scope()` - Increment depth (O(1))
- `get()` - Return `.back()` value (O(1))
- `set()` - Push if new depth, modify if same depth (O(1))
- `pop_scope()` - Pop all variables modified at current depth (O(modified_vars))

**Benefit**: O(1) variable access, no parent traversal, automatic cleanup

### 2. Frame Stack (Metadata Storage)
**Purpose**: Track context information (script_dir, function_block pointer)

**Structure**: Lightweight deque of metadata
```cpp
struct FrameMetadata {
    std::string script_dir;
    const FunctionBlock* function_block;
};
std::deque<FrameMetadata> frame_stack_;
```

**Used for**:
- `CMAKE_CURRENT_SOURCE_DIR` / `CMAKE_CURRENT_LIST_DIR` lookups
- `CMAKE_CURRENT_FUNCTION` / `CMAKE_CURRENT_FUNCTION_LIST_FILE` lookups
- Directory context for relative path resolution

**Benefit**: No variables stored here, just pointers and strings

### 3. Trace Stack (Error Reporting)
**Purpose**: Build error backtraces showing call chain

**Structure**: Vector of call locations
```cpp
struct CallLocation {
    std::string file, command;
    size_t row, col, offset, length;
};
std::vector<CallLocation> trace_stack_;
```

**Used for**:
- Fatal error backtraces
- Warning context
- Debugging information

**Benefit**: COMPLETELY UNCHANGED by Shadow Map refactor

### Why Three Stacks?

Each stack has a different purpose and lifecycle:

| Stack | Purpose | Push/Pop Timing | Contents |
|-------|---------|----------------|----------|
| Shadow Map | Variable storage | Function calls, block() | All variable values across all scopes |
| Frame Stack | Metadata | Function calls | script_dir, function_block pointer |
| Trace Stack | Error reporting | Every command execution | file, row, col, command name |

**Independence**: Shadow Map only affects variable lookup - error reporting and metadata are untouched.

### Example: Function Call with Error

Consider this CMake script that triggers an error:

```cmake
set(ROOT_VAR "root")

function(inner)
    message("${UNDEFINED_VAR}")  # Error!
endfunction()

function(outer)
    set(OUTER_VAR "outer")
    inner()
endfunction()

outer()
```

**State transitions:**

```
Initial state (depth=0):
  Shadow Map:
    variables_ = {"ROOT_VAR": [(0, "root")]}
    current_depth_ = 0
  Frame Stack:
    [{"<root>", nullptr}]
  Trace Stack:
    []

Enter outer() (depth=1):
  Shadow Map:
    variables_ = {"ROOT_VAR": [(0, "root")]}  # Unchanged yet
    current_depth_ = 1
  Frame Stack:
    [{"<root>", &outer_func}, ...]  # Push metadata
  Trace Stack:
    [{file, row, col, "outer"}]  # Push for backtrace

Set OUTER_VAR:
  Shadow Map:
    variables_ = {
      "ROOT_VAR": [(0, "root")],
      "OUTER_VAR": [(1, "outer")]  # New variable at depth 1
    }
    modified_per_depth_[1] = {"OUTER_VAR"}

Enter inner() (depth=2):
  Shadow Map:
    current_depth_ = 2
  Frame Stack:
    [{"<root>", &inner_func}, {"<root>", &outer_func}, ...]
  Trace Stack:
    [{file, row, col, "outer"}, {file, row, col, "inner"}]

Error on message("${UNDEFINED_VAR}"):
  Trace Stack used to build backtrace:
    Backtrace: ["outer() at line 8", "inner() at line 4"]
    Current: "message at line 4"

  Error output:
    error: undefined variable 'UNDEFINED_VAR'
      --> CMakeLists.txt:4:17
         |
       4 |     message("${UNDEFINED_VAR}")
         |                 ^^^^^^^^^^^^^^^

    Backtrace:
      1. outer() called from CMakeLists.txt:8
      2. inner() called from CMakeLists.txt:9
```

**Key observation**: All three stacks work together, but **only Shadow Map affects variable lookup performance**.

## Conclusion

The current scoping implementation works but has significant complexity and performance issues. The Shadow Map design offers:

- **Correctness**: Eliminates entire classes of scope-related bugs through automatic cleanup
- **Performance**: O(1) variable access vs O(call_depth), minimal allocations
- **Simplicity**: Single unified data structure, no manual save/restore logic
- **Memory efficiency**: Only stores versions when variables are actually shadowed
- **Safety**: Automatic cleanup on scope exit, no risk of forgetting to restore state

### Key Insight

The Shadow Map treats variable history like a **stack**, but stores all stacks in a single flat map. The depth counter acts as a "version number", and `.back()` always gives you the current version. This is more efficient than traditional scope chains because:

1. No pointer chasing (no linked list of scopes)
2. No redundant storage (only store when shadowed)
3. No parent traversal (everything in one map)

This refactor is **high priority** and should be completed before adding new interpreter features. The performance gains and reduced complexity will pay dividends in maintainability and future feature development.

### What Gets Simplified

**Before Shadow Map** (current implementation):
```cpp
// Function call - manual save/restore, linear search
auto saved_macro_subs = macro_substitutions_;
macro_substitutions_.clear();
CallFrame frame{call_stack_.front().script_dir, {}, &func};
// ... set up ARGC/ARGV in frame.variables ...
call_stack_.push_front(frame);
// ... execute ...
call_stack_.pop_front();
macro_substitutions_ = saved_macro_subs;

// Variable lookup - O(call_depth)
for (auto it = call_stack_.rbegin(); it != call_stack_.rend(); ++it) {
    auto var_it = it->variables.find(name);
    if (var_it != it->variables.end()) return var_it->second;
}
```

**After Shadow Map**:
```cpp
// Function call - automatic cleanup, O(1) operations
frame_stack_.push_front({script_dir, &func});
shadow_map_.push_scope();
// ... set up ARGC/ARGV via shadow_map_.set() ...
// ... execute ...
shadow_map_.pop_scope();  // Automatically pops all modified variables
frame_stack_.pop_front();

// Variable lookup - O(1)
return shadow_map_.get(name);  // Just variables_[name].back()
```

**What stays the same**:
- Error reporting (`trace_stack_` unchanged)
- Metadata access (frame_stack_ is simpler, not slower)
- All CMake semantics (functions, macros, cache, PARENT_SCOPE)

**What improves**:
- Variable lookup: O(call_depth) → O(1)
- Scope creation: allocate map → increment counter
- Code complexity: ~70% less scope management code
- Memory: only store variables when shadowed

---

**Document Status**: Living document, update as new scoping issues are discovered

**Last Updated**: 2026-01-29

**Author**: Development team (synthesized from bug investigation and design discussions)

**Next Steps**: Implement Shadow Map with full test coverage, then migrate incrementally per Phase 1-6 strategy above.
