# Architecture Refactor: Task-Based Build System

This document outlines the plan for transitioning dmake to a generalized, task-based dependency graph execution model.

## Core Concepts

### 1. Artifact (formerly Target)
High-level logical entity (Executable, Shared Library, Static Library).
- **Role**: Serves as a "Task Factory".
- **Attributes**: Name, type, sources, include paths, dependencies.
- **UI**: Provides context for "Cargo-style" progress reporting (e.g., `Building [my_lib]`).

### 2. BuildTask
The atomic unit of work.
- **Inputs**: Files required (sources, headers, or outputs of other tasks).
- **Outputs**: Files produced (object files, binaries).
- **Command**: The shell command to execute.
- **Ownership**: Linked to a parent Artifact for UI tracking.

## The Build Pipeline

### Phase 1: Generation
The Interpreter populates Artifacts. Each Artifact generates its granular `BuildTask`s:
- **Compile Tasks**: One per source file (parallelizable across artifacts).
- **Link Task**: Depends on its artifact's Compile Tasks AND Link Tasks of dependency Artifacts.

### Phase 2: Graph Construction & Validation
A `BuildGraph` collects all tasks and discovers edges via input/output matching.
- **Cycle Detection**: DFS with Three-Color Marking (White/Gray/Black).
- **Error Reporting**: If a Gray -> Gray transition occurs, backtrace the stack to print the cycle path (e.g., `libA -> libB -> libA`).

### Phase 3: Incremental Check (Signature Analysis)
Each task generates a **Signature** to determine if it needs to be re-run. A task is skipped only if its signature matches the value stored in `.dmake_cache` AND its output files exist.

**Signature Components**:
- **Command String**: Any change in flags (e.g., `-O2` to `-O3`) invalidates the task.
- **Input Files**: Contents/timestamps of all source files.
- **Header Dependencies**:
    - During compilation, we will use compiler flags (e.g., `-MMD`) to generate dependency files (`.d`).
    - The `Executor` will parse these files to track which headers were actually included.
    - If any included header changes, the corresponding `Compile Task` is invalidated.
- **Environment Context**:
    - **Compiler Version**: The version string of `g++` (or the active compiler).
    - **dmake Version**: The version of the build tool itself (using a dummy string for now).
    - If the compiler or dmake is updated, a full rebuild may be triggered to ensure consistency.

### Phase 4: Parallel Execution & UI
A multi-threaded `Executor` processes the DAG:
- **Topological Sort**: Tasks run as soon as dependencies are zero.
- **UI Logic**: Print `Building [Name]` when the first task for an Artifact starts; `Finished [Name]` when the Link Task completes.

## Benefits
- **Maximized Parallelism**: Compiles from different libraries run simultaneously.
- **Incremental Builds**: Only re-runs tasks whose inputs or commands have changed.
- **Extensibility**: Naturally supports generated files (GeneratorTask -> Output -> CompileTask).
- **Correctness**: Guarantees circular dependency detection before execution.
