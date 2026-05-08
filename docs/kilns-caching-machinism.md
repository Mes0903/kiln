# Kiln's caching mechanism

Kiln reads and interprets `CMakeLists.txt` on every build. That is the big difference from CMake: there is no generated Ninja file or Makefile sitting between the configure step and the build step. Every invocation starts by running the CMake program again, collecting targets again, and producing a build graph again.

That sounds like it should be expensive. In practice Kiln makes it work by caching the slow parts underneath the language, not by keeping a long-lived CMake variable cache. The rule of thumb is:

- CMake remembers configuration state.
- Kiln replays configuration, but remembers expensive observations made while replaying it.

This is why Kiln can feel fresh on every run without paying the full cost of every `find_*`, `try_compile`, directory scan, or generated-file rewrite.

## What Kiln does not cache

Kiln does not treat `set(... CACHE ...)` as a persistent user-options database in the same way CMake does. Cache variables exist while the interpreter is running, and commands like `find_library()` still set cache variables as the CMake language expect that. But a `-D` value from a previous Kiln invocation is not remembered as the default for the next one.

For example:

```bash
kiln -DMY_OPTION=ON
kiln
```

The second command behaves as if `MY_OPTION` was not passed. If you want an option on every build, pass it every time, put it in a preset, or put it in the project logic. This is intentional, it avoids the classic CMake problem where an old `CMakeCache.txt` silently keeps a decision alive after the machine, toolchain, or dependency layout changed.

## The subsystem cache

The main persistent configure-time cache is:

```plaintext
build/<config>/.kiln_subsystem_cache.json
```

It is a JSON file with separate sections for different subsystems. Each section has its own key and invalidation rule. Kiln loads it when the interpreter starts and saves it again when the build finishes. External projects get their own subsystem cache in their own binary directory, because their configure state belongs to the child project, not the parent.

The cache is also best-effort. If the file is missing, Kiln starts empty. If it is corrupted, Kiln ignores it and recomputes. You should not need to edit it by hand.

`--fresh` skips loading this persistent cache. Use it when you suspect a stale result, or after changing something outside the project that Kiln cannot reasonably observe.

## `try_compile()` and `try_run()`

`try_compile()` is one of the most important caches. CMake projects use it for feature tests, ABI checks, compiler behavior checks, and package-specific probes. Running all of those from scratch on every Kiln build would make the "interpret every time" model painful.

Kiln caches the result of a test compile using a signature made from the important inputs:

- compiler path and compiler version
- language and standard
- sysroot, target, and global language flags
- source files or inline source contents
- compile definitions
- linked libraries and link options
- discovered header dependencies

The first lookup uses source file mtimes as it is the cheapest option. If the mtime changed, Kiln falls back to a content-hash signature. That fallback matters for CMake code that rewrites the same probe source on every configure run with `file(WRITE)`. The file timestamp changes while the contents may not. Kiln can still reuse the previous `try_compile()` result.

`try_run()` uses the same compile-side inputs and also includes the run arguments and working directory. It caches the compile result, exit code, and captured output. Cross-compiling still follows the usual `try_run()` constraints: if the test cannot be run, Kiln cannot invent a host result.

Compared with CMake, the practical difference is that Kiln re-executes the configure program but often skips the actual compiler probes. If a header used by a probe changes, the cached result is invalidated through the recorded header dependency state.

## `find_file()`, `find_path()`, `find_library()`, and `find_program()`

The `find_*` commands are cached as search results, not as permanent user choices. Kiln records:

- the command kind
- the searched names
- the search paths and suffixes
- relevant root-path and sysroot settings
- whether `NAMES_PER_DIR` was used
- the directories that were inspected
- each inspected directory's mtime, including missing directories

On the next run, Kiln can reuse the result if the directories it searched still have the same state. This caches both positive and negative results. If a package appears later in a directory that was previously searched, that directory's mtime should change and the lookup is retried.

This is the main difference from CMake's `CMakeCache.txt` behavior. In CMake, a found path often stays fixed until the cache variable is removed or the build directory is recreated. In Kiln, the result is still cached, but it is tied to the filesystem state that made the result true.

`NO_CACHE` is effectivly no-op and Kiln may still use its lower-level subsystem cache to avoid repeated directory scans for the same observable search.

## Directory listings and globs

Kiln does a lot of path checks while interpreting CMake: `if(EXISTS ...)`, `if(IS_DIRECTORY ...)`, `include()`, `find_package()`, `file(GLOB)`, and many find operations all touch the filesystem.

To avoid statting the same paths over and over, Kiln caches directory listings. A directory listing records the directory mtime, regular entries, and subdirectories. During one interpreter run this is kept in memory. Across runs it is stored in the subsystem cache and reused when the directory mtime still matches.

`file(GLOB)` and `file(GLOB_RECURSE)` have an additional cache above the directory listing cache. Kiln records the matched result and the mtimes of all directories that were scanned for that glob pattern. If any scanned directory changes, the glob is recomputed.

This is not CMake's `CONFIGURE_DEPENDS` model. Kiln is interpreting every build anyway, so the question is not "should a generated build system re-run configure?". The question is only "can this glob result be reused safely during this configure run?". Directory mtimes are the invalidation boundary.

## Compiler detection

Kiln needs the same sort of compiler facts that CMake normally discovers during configure: compiler id, version, implicit include directories, implicit link directories, implicit libraries, and default standards.

Those probes are cached by compiler path, resolved path, compiler mtime, sysroot, and target. A cache hit still validates the compiler with `--version`, which catches common wrapper or shim changes where the visible path stayed the same but the compiler underneath changed.

This is another place where Kiln differs from CMake. CMake usually discovers compiler metadata once for a build directory and keeps it in `CMakeCache.txt`. Kiln discovers the information as needed, but stores the expensive probe result in the subsystem cache.

## Cacheable `execute_process()`

Most `execute_process()` calls are not cached. Arbitrary commands can read the network, inspect the current time, mutate files, or depend on hidden environment state. Kiln only caches a narrow set of commands where the implementation knows how to key and validate the result:

- `pkg-config` style queries with captured output
- simple Python commands that are recognized as safe
- Python import checks, with `sys.path` directory mtimes tracked
- Python version queries, with a small in-session shortcut

Commands with input, output, or error file redirects are not cached by this path. Commands that do not capture output or a result variable are also not cached, because there is nothing useful to replay back into the CMake program.

For `pkg-config`, Kiln tracks the mtimes of package-config search directories. For Python import checks, it tracks import directories. For simple Python commands, the key includes the binary, script arguments, and effective working directory.

## Build-task signatures

After configuration, Kiln builds from its own task graph. The build cache for those tasks lives in:

```plaintext
build/<config>/.kiln_cache
```

This cache is separate from `.kiln_subsystem_cache.json`. It answers a different question: "does this build task need to run?"

Kiln signs a task using:

- the command line, with cosmetic compiler flags stripped where possible
- the version output of each invoked tool
- the Kiln version
- primary input mtimes
- header dependencies parsed from `.d` files
- generated dependencies when no `.d` file exists and a compile needs a slower header scan

If the signature is unchanged, the task is clean. If the build directory moved, Kiln invalidates this cache using `.kiln_build_path`, because absolute paths embedded in command lines and dependency files may no longer mean the same thing.

ExternalProject build tasks are routed to the external project's own build cache, so a child project's build state does not pollute the parent's `.kiln_cache`.

## AST cache

Kiln also has a small in-memory AST cache for a few CMake modules that are included repeatedly, such as `FindPackageHandleStandardArgs.cmake`, `FindPackageMessage.cmake`, `FindPkgConfig.cmake`, and `CMakeParseArguments.cmake`.

This cache is not persistent and is not user-visible. It avoids reparsing the same shared helper modules many times during a single run, especially across repeated `find_package()` calls.

## Write-if-difference

Because Kiln interprets CMake every time, generated files are especially important. If a CMake script writes the same generated header on every build and updates its mtime every time, downstream compile tasks would rebuild for no real reason.

Kiln avoids that in several places:

- `configure_file()` compares the would-be output with the existing file and only rewrites when the content changes. It also updates permissions only when needed.
- `file(GENERATE)` evaluates generator expressions, then hashes existing and new content and only writes when the content changed.
- `file(WRITE)` returns early when the existing file already has exactly the same contents. `file(APPEND)` still appends.
- Qt autogen's `mocs_compilation.cpp` is only rewritten when the generated include list changes.
- `kiln -E copy_if_different` compares file contents before copying, matching the expectation of CMake's tool mode.

This is not just an optimization. It is part of making Kiln's execution model usable. In CMake, generated files are usually produced during configure and then a generated build system decides what to rebuild. In Kiln, configure and build happen together on every invocation. Preserving mtimes for unchanged generated files keeps no-op builds quiet.

There is one behavioral detail to remember: if your project intentionally relies on `file(WRITE)` updating a timestamp even when the content is identical, Kiln will not do that. Use `file(TOUCH)` or `file(TOUCH_NOCREATE)` when the timestamp update itself is the intended effect.

The opposite pattern is also worth avoiding: generated files that embed the current time, a random value, or any other per-configure changing content. For example, a `configure_file()` template that writes "configured at ${NOW}" will produce different output every time Kiln interprets the project. Kiln cannot cache that away, and it should not try. The file really did change, so anything downstream of it will rebuild every time.

If you need build provenance, prefer a value that changes only when the provenance changes, such as a Git commit, version string, or explicit release timestamp. If you need the wall-clock time of the build, keep it out of headers and generated sources that are inputs to normal compilation, or expect the rebuild.

## When to clear the cache

Most changes should invalidate naturally: source changes, header changes, compiler changes, package directory changes, and generated-file content changes are all part of the relevant signatures.

Use `--fresh` when the changed input is outside those observable boundaries. Common examples are:

- a package manager changed symlinks without changing the searched directory mtimes
- a tool's behavior changed but its path and version output did not
- an environment-dependent command was recognized as cacheable but depended on something Kiln does not track
- you are debugging a configure-time problem and want to remove cache reuse from the picture

If that fixes the build, the right long-term fix is usually to make the changing input visible: pass it through a CMake variable, make it part of the search path, touch the relevant directory, or avoid hiding it behind an untracked command.
