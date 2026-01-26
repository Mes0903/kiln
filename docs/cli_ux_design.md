# Sane CLI Design

`dmake` aims to provide a modern, developer-friendly interface that reduces friction for common tasks while maintaining full compatibility with CMake projects.

## 1. Core Philosophy

- **Zero-Config Defaults**: `dmake` assumes the current directory is the project root if it contains a `CMakeLists.txt`.
- **Verbs over Flags**: Common actions like testing and running are top-level commands.
- **Context-Aware Positional Arguments**: The system distinguishes between project paths and build targets automatically.

## 2. Command Syntax

```bash
dmake [verb] [project_path] [targets...] [-- [run_args...]]
```

### Verbs
| Verb | Description |
| :--- | :--- |
| `build` | (Default) Builds the specified targets. |
| `test`  | Builds and runs tests (optionally matching a pattern). |
| `run`   | Builds and executes a specific target. |
| `clean` | Removes the build artifacts for the current configuration. |

## 3. Context Resolution Rules

To resolve the ambiguity between a **Project Path** and a **Target Name**, `dmake` applies the following logic to the first positional argument:

1. **Explicit Project**: If the argument is a path to a directory containing `CMakeLists.txt`, it is treated as the project source directory.
2. **Implicit Project**: If the first argument is NOT a project directory, but the current directory (`.`) contains a `CMakeLists.txt`, the current directory is the project root. The argument is then treated as the first **Target**.
3. **Ambiguity Resolution**: If a folder exists with the same name as a target (and contains a `CMakeLists.txt`), the folder takes precedence. Use `dmake . <target>` to force target resolution in the current directory.

## 4. Examples

| Goal | Command |
| :--- | :--- |
| Build everything in current dir | `dmake` |
| Build specific target | `dmake my_lib` |
| Build a different project | `dmake ../other_project` |
| Build specific target in other project | `dmake ../other_project my_app` |
| Run all tests | `dmake test` |
| Run specific tests | `dmake test "Network.*"` |
| Build and execute a binary | `dmake run my_app` |
| Build and execute with arguments | `dmake run my_app -- --port 8080` |

## 5. Subcommand Details

### `dmake run <target> [-- <args...>]`
- Identifies the `executable` target.
- Performs an incremental build of that target and its dependencies.
- Locates the resulting binary in the build directory.
- Executes it, passing any arguments following the `--` separator.

### `dmake test [pattern]`
- Integrates with `enable_testing()` and `add_test()`.
- Identifies all targets required by the tests.
- Builds those targets.
- Executes tests in parallel, capturing output and providing a clean summary.

### `dmake clean`
- Wipes the `<project>/build/<config>` directory.
- Does not affect other configurations (e.g., `clean` in `debug` leaves `release` intact).
