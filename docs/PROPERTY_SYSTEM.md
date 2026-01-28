# Property System Implementation

This document describes the comprehensive property system implementation in dmake, following CMake's property API specifications.

## Implemented Commands

### 1. `define_property()`

Defines custom properties with metadata and inheritance rules.

**Syntax:**
```cmake
define_property(<GLOBAL | DIRECTORY | TARGET | SOURCE | TEST | VARIABLE | CACHED_VARIABLE>
                PROPERTY <name> [INHERITED]
                [BRIEF_DOCS <brief-doc> [docs...]]
                [FULL_DOCS <full-doc> [docs...]]
                [INITIALIZE_FROM_VARIABLE <variable>])
```

**Features:**
- All 7 scope types supported
- `INHERITED` flag enables property inheritance through scope chains
- Documentation strings (BRIEF_DOCS, FULL_DOCS)
- Silently ignores redefinition attempts (CMake behavior)
- INITIALIZE_FROM_VARIABLE for target properties (metadata stored, not yet enforced)

### 2. `set_property()`

Sets property values for various scopes with append operations.

**Syntax:**
```cmake
set_property(<GLOBAL |
              DIRECTORY [<dir>] |
              TARGET [<target1> ...] |
              SOURCE [<src1> ...] [DIRECTORY <dirs>] [TARGET_DIRECTORY <targets>] |
              TEST [<test1> ...] |
              CACHE [<entry1> ...] >
             [APPEND] [APPEND_STRING]
             PROPERTY <name> [<value1> ...])
```

**Supported Scopes:**
- **GLOBAL**: Sets properties in global namespace (no item names)
- **DIRECTORY**: Sets properties for current directory (item names optional)
- **TARGET**: Sets properties on targets (handles INTERFACE_* properties)
- **SOURCE**: Sets properties on source files with DIRECTORY/TARGET_DIRECTORY sub-options
- **TEST**: Sets properties on tests
- **CACHE**: Sets properties on cache entries (note: uses "CACHE" not "CACHED_VARIABLE")

**Operations:**
- **APPEND**: Appends to existing value with semicolon separator
- **APPEND_STRING**: Appends as string without separator

### 3. `get_property()`

Retrieves property values with inheritance support and query modes.

**Syntax:**
```cmake
get_property(<variable>
             <GLOBAL | DIRECTORY [<dir>] | TARGET <target> |
              SOURCE <source> [DIRECTORY <dir> | TARGET_DIRECTORY <target>] |
              TEST <test> | CACHE <entry> | VARIABLE>
             PROPERTY <name>
             [SET | DEFINED | BRIEF_DOCS | FULL_DOCS])
```

**Query Modes:**
- **(default)**: Returns property value (or empty if not set)
- **SET**: Returns "1" if property is set, "0" otherwise
- **DEFINED**: Returns "1" if property is defined via `define_property()`, "0" otherwise
- **BRIEF_DOCS**: Returns brief documentation string (or "<name>-NOTFOUND")
- **FULL_DOCS**: Returns full documentation string (or "<name>-NOTFOUND")

**Inheritance:**
Properties marked with `INHERITED` follow these chains:
- **DIRECTORY** → parent directories → GLOBAL
- **TARGET** → DIRECTORY (if inherited)
- **SOURCE** → DIRECTORY (if inherited)
- **TEST** → DIRECTORY (if inherited)

## Architecture

### Property Storage

**Target Properties**: Live in `Target` class using existing methods:
- `Target::set_property()` - scalar properties
- `Target::get_property()` - retrieves scalar properties
- `Target::append_property()` - list properties with PUBLIC/PRIVATE/INTERFACE visibility

**Other Scopes**: Stored in Interpreter:
- `Interpreter::global_properties_` - Global properties (root only)
- `Interpreter::directory_properties_` - Directory properties (per scope)
- `Interpreter::source_properties_` - Source file properties (root only, keyed by absolute path)
- `TestDefinition::properties` - Test properties (per test)
- `Interpreter::cache_variables_` - Cache properties (using naming convention)
- `Interpreter::property_definitions_` - Property metadata (root only)

### Built-in Cache Properties

The following built-in properties are automatically handled for CACHE scope:
- **TYPE**: Returns the cache variable type (currently always "STRING")
- **VALUE**: Returns the cache variable's value
- **HELPSTRING**: Returns help text (currently empty)
- **ADVANCED**: Returns "1" if advanced, "0" otherwise (currently always "0")

### CMake Compatibility Notes

**Scope Keyword Inconsistency**: CMake uses different keywords:
- `define_property(CACHED_VARIABLE ...)` - uses CACHED_VARIABLE
- `set_property(CACHE ...)` - uses CACHE
- `get_property(... CACHE ... PROPERTY ...)` - uses CACHE

Our implementation accepts both "CACHE" and "CACHED_VARIABLE" as aliases.

## Testing

Comprehensive integration test: `tests/integration/property-system/`

**Test Coverage:**
- Global property operations (set, get, append, DEFINED, SET, BRIEF_DOCS, FULL_DOCS)
- Directory property operations
- Target property operations with APPEND
- Source property operations
- Test property operations
- Property inheritance
- Query modes
- Not-found behavior
- CACHE scope with TYPE query
- GNUInstallDirs module compatibility

**Test Results:**
- All 18 integration tests pass
- All 405 unit test assertions pass (176 test cases)

## Example Usage

```cmake
# Define a custom target property with inheritance
define_property(TARGET
    PROPERTY MY_CUSTOM_FLAG INHERITED
    BRIEF_DOCS "Custom flag for build configuration"
    FULL_DOCS "This property controls special build behavior"
)

# Set the property on a target
set_property(TARGET myapp PROPERTY MY_CUSTOM_FLAG "enabled")

# Append to the property
set_property(TARGET myapp APPEND PROPERTY MY_CUSTOM_FLAG "optimized")

# Get the property value
get_property(flag_value TARGET myapp PROPERTY MY_CUSTOM_FLAG)
message(STATUS "Flag: ${flag_value}")  # Output: Flag: enabled;optimized

# Query if property is defined
get_property(is_defined TARGET myapp PROPERTY MY_CUSTOM_FLAG DEFINED)
message(STATUS "Defined: ${is_defined}")  # Output: Defined: 1

# Get documentation
get_property(docs TARGET myapp PROPERTY MY_CUSTOM_FLAG BRIEF_DOCS)
message(STATUS "Docs: ${docs}")  # Output: Docs: Custom flag for build configuration

# Set source property
set_property(SOURCE main.cpp PROPERTY COMPILE_FLAGS "-DDEBUG")
get_property(src_flags SOURCE main.cpp PROPERTY COMPILE_FLAGS)

# Set global property
set_property(GLOBAL PROPERTY MY_PROJECT_VERSION "1.0.0")
get_property(version GLOBAL PROPERTY MY_PROJECT_VERSION)

# Cache property (for compatibility with GNUInstallDirs and similar modules)
get_property(cache_type CACHE CMAKE_INSTALL_PREFIX PROPERTY TYPE)
```

## Implementation Files

- **dmake/builtins/property.cpp**: Complete property system implementation
- **dmake/interperter.hpp**: Property storage structures and accessor methods
- **dmake/builtins/registry.hpp**: Property builtin registration
- **tests/integration/property-system/**: Integration test suite

## Known Limitations

1. **INITIALIZE_FROM_VARIABLE**: Metadata is stored but not enforced during target creation
2. **Cache property types**: All cache variables are currently reported as type "STRING"
3. **DIRECTORY scope with explicit paths**: Only current directory fully supported
4. **INSTALL scope**: Not yet implemented (rarely used)

These limitations can be addressed in future updates as needed.
