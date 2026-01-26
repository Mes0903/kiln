# Nested Variable Expansion Design

## Overview

Support for nested variable references like `${${VAR}}` and mixed namespace expansions like `$ENV{${VAR}}` for future extensibility.

## Motivation

CMake supports nested variable expansion:
```cmake
set(VAR1 "A")
set(VAR2 "VAR1")
message("${${VAR2}}")  # Outputs: A
```

Additionally, CMake has multiple variable namespaces:
- `${VAR}` - regular variables
- `$ENV{VAR}` - environment variables
- `$CACHE{VAR}` - cache variables

We need a design that supports both nesting and future namespace expansion.

## Design

### Data Structure

```cpp
struct VariableReference {
    std::string namespace_prefix;  // "", "ENV", "CACHE"
    std::vector<ArgumentPart> name_parts;  // Recursively contains strings and VariableReferences
};
```

**Key insight**: Variable names are themselves argument expressions that can contain literals and variable references.

### Examples

#### Simple variable: `${VAR}`
```
VariableReference {
  namespace_prefix: ""
  name_parts: [string("VAR")]
}
```

#### Nested variable: `${${VAR2}}`
```
VariableReference {
  namespace_prefix: ""
  name_parts: [
    VariableReference {
      namespace_prefix: ""
      name_parts: [string("VAR2")]
    }
  ]
}
```

#### Mixed expansion: `${prefix${VAR}suffix}`
```
VariableReference {
  namespace_prefix: ""
  name_parts: [
    string("prefix"),
    VariableReference {
      namespace_prefix: ""
      name_parts: [string("VAR")]
    },
    string("suffix")
  ]
}
```

#### Future ENV syntax: `$ENV{${VAR}}`
```
VariableReference {
  namespace_prefix: "ENV"
  name_parts: [
    VariableReference {
      namespace_prefix: ""
      name_parts: [string("VAR")]
    }
  ]
}
```

## Implementation

### Parser Changes (cmake-language.cpp)

**Goal**: When encountering `${`, recursively parse the content inside braces as argument parts.

1. **Detect namespace prefix**: Check if `$` is followed by identifier then `{` (e.g., `$ENV{`)
2. **Track brace depth**: Find matching closing `}`
3. **Recursive parsing**: Parse content between braces as argument parts (which may contain more variable references)

**Parsing algorithm**:
```
When encountering '$':
  if next is '{':
    namespace_prefix = ""
  else if next is alpha:
    namespace_prefix = parse_identifier()
    expect '{'

  Parse argument parts until matching '}':
    - Literal strings
    - Nested ${...} → recursively create VariableReference

  Return VariableReference(namespace_prefix, parts)
```

### Evaluator Changes (interperter.cpp)

**Goal**: Recursively evaluate variable name, then lookup based on namespace.

```cpp
std::string evaluate_variable_reference(const VariableReference& ref) {
    // Step 1: Recursively evaluate name_parts to build final name
    std::string name;
    for (const auto& part : ref.name_parts) {
        if (std::holds_alternative<std::string>(part)) {
            name += std::get<std::string>(part);
        } else {
            name += evaluate_variable_reference(std::get<VariableReference>(part));
        }
    }

    // Step 2: Lookup based on namespace
    if (ref.namespace_prefix.empty()) {
        return get_variable(name);
    } else if (ref.namespace_prefix == "ENV") {
        return get_env_variable(name);  // Future implementation
    } else if (ref.namespace_prefix == "CACHE") {
        return get_cache_variable(name);  // Future implementation
    }
    return "";
}
```

Update `evaluate_argument()`:
```cpp
std::string Interpreter::evaluate_argument(const Argument& arg) {
    std::string res;
    for (const auto& p : arg.parts) {
        if (std::holds_alternative<std::string>(p)) {
            res += std::get<std::string>(p);
        } else {
            res += evaluate_variable_reference(std::get<VariableReference>(p));
        }
    }
    return res;
}
```

## Edge Cases

| Case | Behavior |
|------|----------|
| `${${UNDEF}}` | UNDEF→"", lookup var with empty name → "" |
| `${pre${VAR}post}` | Expand VAR, concatenate with literals |
| `${}` | Variable with empty name |
| `${${${VAR}}}` | Arbitrary nesting depth |
| `$ENV{HOME}` | (Future) Environment variable lookup |
| `$CACHE{VAR}` | (Future) Cache namespace lookup |

## Migration Path

**Phase 1 (Current PR)**:
- Implement recursive structure
- Support only `namespace_prefix = ""`
- All existing tests pass

**Phase 2 (Future)**:
- Add `$ENV{}` parser recognition
- Implement `get_env_variable()`

**Phase 3 (Future)**:
- Add `$CACHE{}` parser recognition
- Implement cache namespace (just another variable map, not persistent)

## Advantages

1. **Parse once, evaluate many times**: No re-parsing at evaluation time
2. **Type safety**: Invalid syntax caught at parse time with proper error locations
3. **Extensibility**: Adding new namespaces requires minimal changes
4. **Clean separation**: Parser handles syntax, evaluator handles semantics
5. **Natural recursion**: Both parser and evaluator use straightforward recursive algorithms

## Testing

Test cases in `tests/interpreter_tests.cpp`:

```cmake
# Simple nesting
set(VAR1 "A")
set(VAR2 "VAR1")
message("${${VAR2}}")  # → "A"

# Triple nesting
set(VAR1 "A")
set(VAR2 "VAR1")
set(VAR3 "VAR2")
message("${${${VAR3}}}")  # → "A"

# Partial expansion
set(VAR1 "A")
set(VAR2 "VAR1")
set(VAR3 "VAR")
message("${${${VAR3}2}}")  # → "A"
```
