# Argument Expansion Refactoring

## Problem
Currently, `dmake` maps each AST `Argument` node directly to one runtime argument string. This causes incorrect behavior when variable references expand to empty strings or lists.

Example:
```cmake
set(U "")
message(${U})
```
- **Expected (CMake)**: `message()` is called with 0 arguments. `message` errors because it requires arguments.
- **Current (dmake)**: `message` is called with 1 argument (`""`). It prints an empty line.

Also applies to lists:
```cmake
set(L "a;b")
message(${L})
```
- **Expected**: `message` called with "a", "b".
- **Current**: `message` called with "a;b".

## Solution
We need an intermediate "Expansion" step between AST processing and command execution.

### Algorithm
`std::vector<std::string> Interpreter::expand_arguments(const std::vector<Argument>& args)`

Iterate over input `args`:
1. **Evaluate**: Resolve variable references in the argument to a string `val`.
2. **If Quoted (`arg.quoted`)**:
   - Push `val` to result (preserving empty strings).
3. **If Unquoted (`!arg.quoted`)**:
   - If `val` is empty: **Discard** (contributes 0 arguments).
   - If `val` contains `;`: **Split** by `;` and push all parts.
   - Else: Push `val`.

### Implementation Changes

1.  **`dmake/interperter.hpp`**:
    -   Update `BuiltinFunction` signature:
        ```cpp
        using BuiltinFunction = std::function<void(Interpreter&, const std::vector<std::string>&)>;
        ```
    -   Update `invoke_user_function` and `invoke_user_macro` to accept `std::vector<std::string>`.
    -   Add `expand_arguments` helper.

2.  **`dmake/interperter.cpp`**:
    -   Implement `expand_arguments`.
    -   Update `execute_command` to call `expand_arguments` before dispatching.
    -   Update `execute_foreach_block` to usage (optional but recommended for consistency).
    -   Update `invoke_*` implementations.

3.  **Builtins**:
    -   Update all builtin implementations in `dmake/builtins/*.cpp` and `dmake/interperter.cpp` to match the new signature.
    -   **`message` builtin**: Update to return an error if `args` is empty (matching CMake behavior).

4.  **`CMakeList`**:
    -   Update `Interpreter::from_arguments` to work with the new system or replace it with `CMakeList` construction from `vector<string>`.
