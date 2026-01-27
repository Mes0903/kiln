# C++ Modules Support Implementation Plan for dmake

## Executive Summary

This plan implements C++20 modules support in dmake using a **Scan-at-Build (Dynamic Dependency)** architecture. The approach maximizes parallelism and preserves dmake's instant startup by treating module scanning as parallel build tasks rather than configuration-time overhead.

**Core Strategy**: Three-stage pipeline per target: Scanner Tasks (parallel) → Collator Task (barrier) → Compile Tasks (with injected dependencies)

## Design Decisions

### 1. Module Scanning Strategy

**When to Scan**: At build time, as parallel tasks (not configuration time)

**Rationale**:
- dmake runs configuration + build in single phase
- Scanning at config time would serialize the startup (destroy "instant" feel)
- Build-time scanning leverages all CPU cores immediately
- Handles generated source files (don't exist at config time)

**How to Scan**: Use compiler preprocessor mode

```bash
g++ -std=c++20 -fmodules-ts -E -fdirectives-only <source> 2>&1
```

- Parse output for `import` and `export module` directives
- Fast (preprocessor only, no actual compilation)
- Compiler-accurate (respects macros, conditional includes)

**Alternative Rejected**: Custom C++ parser
- Would require maintaining C++ grammar knowledge
- Macros/conditionals would make it unreliable
- Compiler is the source of truth

**Parallelism**: All scanner tasks run immediately with no dependencies
- Scanners for all source files in a target can run in parallel
- Bottleneck is the collator (waits for all scanners)
- Collator is fast (just JSON parsing + graph mutation)

### 2. Build Graph Extension

**New Task Types**:

```cpp
// Scanner: Source → DDI file
BuildTask {
  id: "build/debug/ddi/src/math.ixx.ddi"
  commands: [["g++", "-E", "-fdirectives-only", ...]]
  inputs: ["src/math.ixx"]
  outputs: ["build/debug/ddi/src/math.ixx.ddi"]
  is_module_scanner: true  // NEW FLAG
}

// Collator: All DDI files → Module map
BuildTask {
  id: "build/debug/target_myapp.modules.map"
  commands: [[dmake internal function]]  // No subprocess
  inputs: ["*.ddi"]  // All DDI files from target
  outputs: ["target_myapp.modules.map"]
  is_module_collator: true  // NEW FLAG
  target_ptr: &myapp_target  // For graph mutation
}

// Compile: Source + Map → Object + BMI
BuildTask {
  id: "build/debug/objs/src/math.ixx.o"
  commands: [["g++", "-fmodule-mapper=...", ...]]
  inputs: ["src/math.ixx", "target_myapp.modules.map"]
  outputs: ["build/debug/objs/src/math.ixx.o", "build/debug/bmis/Math.gcm"]
  dependencies: {}  // Populated by collator at runtime
}
```

**BMI Storage**: `build/<config>/bmis/<ModuleName>.gcm`
- Separate directory from objects (cleaner)
- Named by module name (not source file)
- Module mapper file maps names to paths

**DDI File Format** (JSON):
```json
{
  "source": "src/math.ixx",
  "provides": "Math",          // Empty string if no export
  "requires": ["Core", "std"],
  "timestamp": 1234567890
}
```

### 3. Parallelism Approach

**Execution Flow**:

```
[Config Phase - Instant]
  ↓
[Generate Tasks]
  ├─ Scanner 1 ──┐
  ├─ Scanner 2 ──┤
  ├─ Scanner N ──┼──→ [Collator] ──→ Compile 1
  └─ Non-module ──┘                  Compile 2
      compiles                       Compile N
      (parallel)
```

**Phase 1**: All scanners + non-module compiles run in parallel
**Phase 2**: Collator runs when all scanners complete (barrier)
**Phase 3**: Module compiles run with correct dependencies

**Critical Path Optimization**:
- Scanner tasks are high priority (fast, unlock collator)
- Collator is high priority (fast, unlocks module compiles)
- Non-module code compiles freely during scanning phase

**No Starvation**:
- Non-module code doesn't wait for scanners
- Standard library code (no modules) builds immediately
- Only module-using code waits for collator

### 4. Caching Strategy

**Scanner Task Signatures**:
```
hash(source_file_timestamp + compiler_version + dmake_version + std_flag)
```

- Cache DDI files in `.dmake_cache` like object files
- Skip scanning if signature matches cached DDI
- DDI timestamp used by collator for incremental logic

**Compile Task Signatures**:
```
hash(source_timestamp + all_flags + included_headers + imported_bmi_timestamps)
```

- BMI timestamps tracked like headers
- Changing module interface invalidates importers
- Module mapper file is an input (timestamp tracked)

**Collator Task Signature**:
```
hash(all_ddi_timestamps)
```

- Regenerate mapper only if any DDI changed
- Cheap to regenerate (just JSON parsing)

**Invalidation Rules**:
1. Modify module interface → Recompile interface + all importers
2. Modify module implementation (no interface change) → Recompile only that file
3. Add new source file → Run scanner, update collator, recompile importers if new module

### 5. Graph Mutation Implementation

**Thread Safety**: Add mutex to BuildGraph

```cpp
class BuildGraph {
  std::mutex graph_mutation_mutex_;  // NEW

  // Called by collator task after completion
  void inject_module_dependencies(
      const std::map<std::string, std::string>& module_to_bmi,
      const std::map<std::string, std::vector<std::string>>& source_imports);
};
```

**Collator Logic** (runs inside task execution):

1. Parse all DDI files
2. Build map: `module_name → task_id` (providers)
3. Build map: `source_file → [module_names]` (importers)
4. Lock graph mutex
5. For each compile task:
   - Check which modules it imports
   - Add dependencies to provider tasks
   - Inject `-fmodule-file=<ModuleName>=<bmi_path>` flags
6. Update task signatures
7. Unlock mutex
8. Notify condition variable (wake waiting compile tasks)

**Key Insight**: Collator doesn't run subprocess, it's an in-process function that mutates the graph.

### 6. CMakeLists.txt API

**Auto-Detection** (Phase 1):
- Files with extensions: `.cppm`, `.ixx`, `.ccm`, `.cxxm`
- Automatically treated as potential module sources
- Scanner runs on all C++ sources (cheap if no modules found)

**Explicit Declaration** (Future):
```cmake
target_sources(myapp PRIVATE
  src/main.cpp
  MODULE src/math.ixx  # Explicit module interface
)
```

**Standard Library Modules** (Phase 2):
```cmake
target_link_libraries(myapp PRIVATE std.modules)
# Adds pre-built std BMI to mapper
```

**Philosophy**:
- Start simple (auto-detection)
- Avoid new builtins unless necessary
- Optimize for common case (modules just work)

## Implementation Roadmap

### Phase 1: Core Infrastructure (Files to Modify)

**1. Add DDI file format** (new file)
- Location: `dmake/module_scanner.hpp`
- Struct: `ModuleDependencyInfo` with `provides`, `requires`
- Function: `parse_ddi_file(path) → ModuleDependencyInfo`
- Function: `write_ddi_file(path, info)`

**2. Extend BuildTask** (`dmake/build_system.hpp`)
- Add: `bool is_module_scanner = false`
- Add: `bool is_module_collator = false`
- Add: `Target* parent_target = nullptr` (already exists, document usage)

**3. Add Graph Mutation** (`dmake/build_system.hpp`, `dmake/build_system.cpp`)
- Add: `std::mutex graph_mutation_mutex_` member
- Add method: `inject_module_dependencies(...)`
- Modify: `execute()` to check task flags after completion
- Call injection logic for collator tasks

**4. Scanner Command Generation** (`dmake/gnu_compiler.hpp`)
- Add method: `get_scan_command(source, output_ddi)`
- Returns: `["g++", "-std=c++20", "-fmodules-ts", "-E", "-fdirectives-only", ...]`
- Include all `-I` and `-D` flags (needed for accurate scanning)

**5. Module Detection** (`dmake/language.hpp`)
- Extend: `LanguageInfo` with `bool is_potential_module`
- Update: `LanguageClassifier::from_path()` to detect `.ixx`, `.cppm` extensions
- Note: Eventually scan `export module` in file, but start with extension

### Phase 2: Task Generation (Files to Modify)

**6. Extend Target** (`dmake/target.hpp`, `dmake/target.cpp`)
- Add method: `generate_module_scanner_tasks(BuildGraph&)`
- Add method: `generate_module_collator_task(BuildGraph&)`
- Modify: `generate_object_tasks()` to skip module files initially
- Modify: `generate_tasks()` to call new methods

**7. Scanner Task Creation** (`dmake/target.cpp`)
```cpp
void Target::generate_module_scanner_tasks(BuildGraph& graph) {
  for (const auto& src : get_property_list("SOURCES")) {
    if (!is_potential_module_source(src)) continue;

    std::string ddi_path = binary_dir_ + "/ddi/" + src + ".ddi";
    BuildTask scanner;
    scanner.id = ddi_path;
    scanner.commands = {compiler.get_scan_command(src, ddi_path)};
    scanner.inputs = {src};
    scanner.outputs = {ddi_path};
    scanner.is_module_scanner = true;
    scanner.parent_target = this;
    graph.add_task(scanner);
  }
}
```

**8. Collator Task Creation** (`dmake/target.cpp`)
```cpp
void Target::generate_module_collator_task(BuildGraph& graph) {
  std::vector<std::string> ddi_inputs;
  // Collect all DDI files from scanner tasks

  std::string mapper_path = binary_dir_ + "/" + name_ + ".modules.map";
  BuildTask collator;
  collator.id = mapper_path;
  collator.inputs = ddi_inputs;
  collator.outputs = {mapper_path};
  collator.is_module_collator = true;
  collator.parent_target = this;
  collator.always_run = false;  // Can be cached
  graph.add_task(collator);
}
```

**9. Compile Task Updates** (`dmake/target.cpp`)
- Modify: `generate_object_tasks()` to add mapper file as input
- Add: BMI files to outputs list (parsed from DDI later)
- Note: Dependencies injected by collator, not at task creation

### Phase 3: Collator Implementation (Files to Modify)

**10. Collator Execution Logic** (`dmake/build_system.cpp`)
```cpp
void BuildGraph::execute_collator_task(const BuildTask& task) {
  // 1. Parse all DDI files
  std::map<std::string, std::string> module_to_source;
  std::map<std::string, std::vector<std::string>> source_requires;

  for (const auto& ddi_path : task.inputs) {
    auto info = parse_ddi_file(ddi_path);
    if (!info.provides.empty()) {
      module_to_source[info.provides] = info.source;
    }
    if (!info.requires.empty()) {
      source_requires[info.source] = info.requires;
    }
  }

  // 2. Generate module mapper file
  write_module_mapper(task.outputs[0], module_to_source);

  // 3. Inject dependencies into compile tasks
  inject_module_dependencies(module_to_source, source_requires);
}
```

**11. Dependency Injection** (`dmake/build_system.cpp`)
```cpp
void BuildGraph::inject_module_dependencies(
    const std::map<std::string, std::string>& module_to_source,
    const std::map<std::string, std::vector<std::string>>& source_requires) {

  std::lock_guard<std::mutex> lock(graph_mutation_mutex_);

  for (auto& [task_id, task] : tasks_) {
    if (!task.is_compilation) continue;

    auto it = source_requires.find(task.source_file);
    if (it == source_requires.end()) continue;

    // Add dependencies to provider tasks
    for (const auto& required_module : it->second) {
      auto provider_it = module_to_source.find(required_module);
      if (provider_it == module_to_source.end()) {
        throw std::runtime_error("Module not found: " + required_module);
      }

      std::string provider_obj = get_obj_path(provider_it->second);
      task.dependencies.insert(provider_obj);

      // Inject module-file flag
      std::string bmi_path = get_bmi_path(required_module);
      task.commands[0].push_back("-fmodule-file=" + required_module + "=" + bmi_path);
    }
  }
}
```

### Phase 4: Incremental Build & Caching (Files to Modify)

**12. Signature Calculation** (`dmake/build_system.cpp`)
- Extend: `calculate_signature()` to include DDI timestamps for compile tasks
- Extend: Signature for scanner tasks (source + compiler version)
- Add: BMI timestamp tracking (like headers)

**13. Cache Integration** (`dmake/build_system.cpp`)
- Scanner DDI outputs cached like `.d` files
- Collator mapper file cached
- Skip scanner if source unchanged and DDI exists

### Phase 5: Error Handling & Diagnostics

**14. Error Cases** (`dmake/build_system.cpp`, error messages)
- Circular module imports: Detect in collator (cycle in requires graph)
- Missing module: Throw at injection time with helpful message
- Scanner failure: Treat like compile failure (stop build)

**15. Stall Detection** (already exists in `execute()`)
- Should naturally handle module dependencies
- Test with complex module graphs

## File Modification Summary

### New Files
- `dmake/module_scanner.hpp` - DDI format, parsing, module scanning logic
- `dmake/module_scanner.cpp` - Implementation

### Modified Files
- `dmake/build_system.hpp` - Add mutex, flags to BuildTask
- `dmake/build_system.cpp` - Graph mutation, collator execution, injection logic
- `dmake/target.hpp` - New task generation methods
- `dmake/target.cpp` - Scanner/collator/compile task generation
- `dmake/gnu_compiler.hpp` - Scanner command generation
- `dmake/language.hpp` - Module file detection

### Test Files
- New: `tests/integration/modules_basic/` - Simple module test
- New: `tests/integration/modules_chain/` - Transitive module deps
- New: `tests/interpreter_tests.cpp` - Module scanning unit tests

## Verification Plan

### Unit Tests
1. DDI file parsing (valid/invalid JSON)
2. Module name extraction from scan output
3. Dependency graph construction
4. Circular dependency detection

### Integration Tests

**Test 1**: Basic module export/import
```
tests/integration/modules_basic/
  CMakeLists.txt
  main.cpp          // import Math
  math.ixx          // export module Math
  test.sh           // Verify builds, runs correctly
```

**Test 2**: Module chain
```
tests/integration/modules_chain/
  main.cpp          // import Top
  top.ixx           // export module Top, import Middle
  middle.ixx        // export module Middle, import Bottom
  bottom.ixx        // export module Bottom
```

**Test 3**: Incremental rebuild
```
1. Build project with modules
2. Touch math.ixx (interface unchanged)
3. Verify only math.ixx recompiles
4. Modify module interface
5. Verify importers recompile
```

**Test 4**: Parallel build
```
1. Large project with modules
2. Build with -j8
3. Verify correct execution order (scanners → collator → compiles)
4. Verify no races or deadlocks
```

### Performance Tests
1. Compare build time with/without modules
2. Verify scanning overhead is minimal (<5% for non-module projects)
3. Verify parallelism scales (8 cores → ~8x scanner throughput)

## Risks & Mitigations

### Risk 1: Compiler Support Variability
**Issue**: g++ module support is evolving, flags may change
**Mitigation**: Abstract compiler interface, version detection, clear error messages

### Risk 2: Graph Mutation Races
**Issue**: Thread-unsafe mutation causes corruption
**Mitigation**: Single mutex for all graph mutations, thorough testing

### Risk 3: Standard Library Modules
**Issue**: `import std;` requires pre-built BMI
**Mitigation**: Phase 2 feature, document workaround (build std first)

### Risk 4: Build Time Regression
**Issue**: Scanning adds overhead for non-module code
**Mitigation**: Scanner only runs if module extensions detected, cache aggressively

### Risk 5: Complex Module Partitions
**Issue**: Module partitions (`module Math:Impl`) are complex
**Mitigation**: Phase 3 feature, focus on basic modules first

## Assumptions

1. **g++ is the target compiler**: Other compilers (clang++, MSVC) deferred to future
2. **C++20 modules syntax**: No legacy TS modules support
3. **Per-target module isolation**: Modules don't cross target boundaries initially
4. **File extensions indicate modules**: `.ixx`, `.cppm` reliably identify module files
5. **No module partitions in Phase 1**: Internal partitions are advanced feature
6. **Scanner output is stable**: g++ preprocessor output format won't change drastically

## Header Units: `import <vector>` Support

**Question**: What would it take to support `import <vector>;` (C++20 header units)?

**Answer**: Header units are an extension of the module system that allows importing headers as modules. This is technically feasible within the scan-at-build architecture but adds significant complexity.

### What Are Header Units?

Header units compile traditional headers into BMIs that can be imported:

```cpp
// Traditional
#include <vector>

// Header unit
import <vector>;  // System header unit
import "myheader.h";  // User header unit
```

### Technical Requirements

**1. Header Unit Compilation** (new task type)

```bash
# System header
g++ -std=c++20 -fmodules-ts -fmodule-header=system -c -x c++-header \
    /usr/include/c++/13/vector -o build/debug/bmis/__system__vector.gcm

# User header
g++ -std=c++20 -fmodules-ts -fmodule-header=user -c -x c++-header \
    include/myheader.h -o build/debug/bmis/__user__myheader.h.gcm
```

**2. Scanner Updates**

Detect `import <...>` and `import "..."` in addition to `import ModuleName`:

```json
{
  "source": "main.cpp",
  "provides": "",
  "requires": ["Math"],
  "header_imports_system": ["vector", "iostream"],
  "header_imports_user": ["myheader.h"]
}
```

**3. Collator Updates**

Generate tasks for header units on-demand:

```cpp
void BuildGraph::generate_header_unit_tasks(
    const std::set<std::string>& system_headers,
    const std::set<std::string>& user_headers) {

  for (const auto& hdr : system_headers) {
    // Find header path (use compiler's search paths)
    // Create BMI compilation task
    // Add to module mapper
  }
}
```

**4. Module Mapper Updates**

Include header → BMI mappings:

```
Math build/debug/bmis/Math.gcm
<vector> build/debug/bmis/__system__vector.gcm
"myheader.h" build/debug/bmis/__user__myheader.h.gcm
```

### Challenges & Complexity

**Challenge 1: Header Path Resolution**
- System headers: Need to query compiler for search paths (`g++ -E -x c++ -v`)
- User headers: Need to resolve relative to source file or include directories
- Solution: Run `g++ -H -E` to discover actual header paths

**Challenge 2: Transitive Header Dependencies**
- `<vector>` includes `<memory>`, `<iterator>`, etc.
- Must compile all transitively included headers as units
- Solution: Scanner detects all headers (like current header discovery)

**Challenge 3: Build Time**
- Pre-compiling all system headers is slow (100+ headers)
- But only needed once per configuration
- Solution: Build header units on first use, cache aggressively

**Challenge 4: Mixing `#include` and `import`**
- Can't `import <vector>` and `#include <vector>` in same translation unit
- Must be consistent across project
- Solution: Document as user responsibility, detect and warn

**Challenge 5: Standard Library Pre-built Modules**
- Some systems ship pre-built `std` module
- Header units are separate from `import std;`
- Solution: Support both, detect which is available

### Implementation Approach

**Phase 1: System Header Units Only**

```cmake
# User enables header units per-target
target_compile_options(myapp PRIVATE -fmodule-header=system)
```

1. Scanner detects `import <...>` syntax
2. Collator generates header unit tasks (on-demand)
3. Header units built before first consumer
4. Cached like regular modules

**Phase 2: User Header Units**

```cmake
# Mark specific headers for unit compilation
target_compile_options(myapp PRIVATE -fmodule-header=user)
```

1. Scanner detects `import "..."` syntax
2. Resolve header paths relative to includes
3. Generate user header unit tasks

**Phase 3: Automatic Detection**

1. Scanner checks for both `#include` and `import` of same header
2. Warn if mixing (undefined behavior)
3. Auto-generate header units if only `import` used

### File Modifications (Additional)

**Extend Module Scanner** (`dmake/module_scanner.hpp`):
- Parse `import <...>` and `import "..."` from scan output
- Distinguish from named module imports

**Extend Collator** (`dmake/build_system.cpp`):
- Method: `generate_header_unit_task(header_path, is_system)`
- Add header units to module mapper
- Inject dependencies to compile tasks

**Add Header Path Resolver** (`dmake/compiler.hpp`):
- Method: `resolve_system_header(header_name) → path`
- Method: `resolve_user_header(header_name, source_dir, includes) → path`
- Cache header paths (expensive lookup)

**Extend BuildTask** (`dmake/build_system.hpp`):
- Flag: `bool is_header_unit = false`
- Field: `std::string header_path`

### Complexity Assessment

| Component | Complexity | Reason |
|-----------|------------|--------|
| Scanner updates | Low | Just parse different import syntax |
| Header path resolution | Medium | Need compiler integration |
| Task generation | Medium | Similar to module tasks |
| Transitive headers | High | Full dependency tree |
| Mixed include/import | High | Hard to detect/prevent |

### Recommendation

**Not in initial implementation** because:

1. **Limited benefit**: Most code uses `import std;` or named modules, not header units
2. **High complexity**: Header path resolution is fragile
3. **Compiler support**: Header units are less stable than modules in g++
4. **Maintenance burden**: Another mode to test and debug

**When to add**:
- After basic modules working reliably
- If users specifically request it
- When compiler support stabilizes
- As Phase 3/4 feature (not core functionality)

**Workaround until then**:
Users can manually compile header units and add to module mapper via custom commands.

## Non-Goals (Out of Scope for Initial Implementation)

1. **clang/MSVC support**: Focus on g++ first
2. **Module partitions**: Internal module structure
3. **Header units**: `import <vector>` translation of headers (see above for future path)
4. **Cross-target modules**: Exporting modules from one target to another
5. **Module precompilation caching**: Beyond basic incremental builds
6. **CMake compatibility**: Not matching CMake's exact module API

## Success Criteria

1. ✓ Basic module export/import compiles correctly
2. ✓ Transitive module dependencies resolve correctly
3. ✓ Incremental builds work (changing interface recompiles importers)
4. ✓ Parallel scanning uses all CPU cores
5. ✓ Build time overhead <10% for non-module code
6. ✓ Clear error messages for missing/circular modules
7. ✓ Integration tests pass consistently
8. ✓ Self-hosting: dmake can build itself with modules (if converted)

## Timeline Estimate

**Phase 1** (Infrastructure): ~2-3 days
**Phase 2** (Task Generation): ~2-3 days
**Phase 3** (Collator Logic): ~2-3 days
**Phase 4** (Caching): ~1-2 days
**Phase 5** (Testing/Polish): ~2-3 days

**Total**: ~9-14 days for working implementation

## Open Questions for User

None - proceeding with scan-at-build architecture as the clear optimal approach for dmake's design.
