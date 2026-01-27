# C++ Modules Support Design: Scan-at-Build Architecture

This document outlines the architectural design for implementing high-performance C++ Modules support in `dmake`. The design prioritizes build speed and parallelism by adopting a **Scan-at-Build** (Dynamic Dependency) strategy, similar to Ninja (`dyndep`) and CMake.

## The Problem: Serialization vs. Dynamic Dependencies

C++ Modules introduce a dependency order that cannot be known from the file names or `CMakeLists.txt` alone.
*   `main.cpp` imports `math`.
*   `math.cpp` provides `math`.
*   Therefore, `math.cpp` must be compiled before `main.cpp`.

### Naive Approach (Scan-at-Config)
Scanning all source files during the configuration (Interpreter) phase creates a bottleneck. `dmake` would have to serially check timestamps and run preprocessors for every source file before the build can even start. This destroys the "instant" feel of the tool.

### Solution: Scan-at-Build (Dyndep)
Scanning is treated as a parallel build task. The Build Graph starts "blind" and patches itself dynamically during execution as dependency information becomes available.

## Architecture: The Three-Stage Pipeline

For every target involving C++ modules, the build pipeline for sources is split into three distinct stages:

### 1. The Scanner Task (Parallel)
*   **Input:** Source file (`.cpp`, `.ixx`).
*   **Action:** Runs the compiler in preprocessor mode (e.g., `g++ -E -fdirectives-only -fmodules-ts`).
*   **Output:** A `.ddi` (Dynamic Dependency Info) file (JSON format).
    *   Contains `provides: [module_name]`
    *   Contains `requires: [module_list]`
*   **Characteristics:** Extremely fast, fully parallelizable, zero dependencies (initially).

### 2. The Collator Task (The Barrier)
*   **Input:** All `.ddi` files from the target's Scanner Tasks.
*   **Action:**
    1.  Reads all dependency info.
    2.  Resolves module names to specific Object tasks (e.g., `module Math` -> `math.o`).
    3.  **Graph Mutation:** Injects new dependency edges into the live `BuildGraph`.
    4.  Generates a "Mapper File" (e.g., `modules.map`) mapping module names to BMI paths.
*   **Output:** `modules.map` (or similar).
*   **Characteristics:** Acts as a synchronization point for the target's modules.

### 3. The Compile Task (The Heavy Lifter)
*   **Input:** Source file + `modules.map`.
*   **Action:** Runs the actual compilation.
    *   Uses `-fmodule-mapper=modules.map` (or explicit `-fmodule-file` flags).
*   **Dependencies:** Now correctly waits for the BMIs of imported modules to be ready (thanks to the Collator).

## Implementation Details

### 1. `BuildTask` Upgrades
The `BuildTask` struct needs to support dynamic behaviors.

```cpp
struct BuildTask {
    // ... existing fields ...

    // Dyndep Support
    bool is_dyndep_generator = false;         // If true, output triggers graph patching
    std::string dyndep_file;                  // The file to parse (e.g., "scan_results.json")
    
    // Command Injection
    // The Collator needs to inject flags into waiting Compile tasks
    std::vector<std::string>* target_command_args_ptr = nullptr; 
};
```

### 2. `BuildGraph::execute` Upgrades
The execution engine must become thread-safe for mutation.

**Current:**
1.  Check dependencies.
2.  Run tasks.

**New:**
1.  Start Scanner tasks (ready immediately).
2.  **On Task Completion:**
    *   If `is_dyndep_generator` is true:
        1.  **Lock Graph Mutex.**
        2.  Parse the generated `.ddi` file.
        3.  Find the consumer tasks (the Compile tasks).
        4.  **Inject Dependencies:** `tasks_[consumer].dependencies.insert(provider)`.
        5.  **Inject Flags:** Add `-fmodule-file=...` to the consumer's command.
        6.  **Unlock.**
3.  Continue execution. The newly dependent Compile tasks will now naturally wait.

### 3. The `Dyndep` File Format (Internal)
`dmake` will use a simplified JSON format for internal communication between Scanner and Collator.

```json
{
  "source": "src/math.ixx",
  "provides": ["Math"],
  "requires": ["Core"],
  "timestamp": 123456789
}
```

### 4. Compiler Abstraction (GCC Example)

*   **Scan:** `g++ -std=c++20 -fmodules-ts -E -fdirectives-only math.ixx`
*   **Map:** `math ./build/math.gcm`
*   **Compile:** `g++ -std=c++20 -fmodules-ts -fmodule-mapper=mapper.txt -c math.ixx`

## Advantages

1.  **Zero Startup Latency:** The build starts immediately. Scanning happens in parallel on all cores.
2.  **Correctness:** Handles generated source files (which don't exist at config time).
3.  **Scalability:** Performance remains $O(N/Cores)$ even for massive projects.

## Roadmap

1.  **Refactor `BuildGraph`:** Add mutex protection for dependency sets and command lists.
2.  **Implement `Scanner` class:** Wrapper for compiler-specific scan commands.
3.  **Implement `Collator` logic:** The algorithm to resolve `provides` vs `requires` and patch the graph.
4.  **Update `Target::generate_tasks`:** To emit the 3-stage pipeline instead of single compile tasks.
