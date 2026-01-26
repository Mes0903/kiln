# Multi-Language & Toolchain Design

This document outlines the architecture for extending `dmake` to support multiple languages (C, C++, CUDA, etc.) and compilers (GCC, Clang, NVCC).

## Core Philosophy
1.  **Separation of Concerns**: `Target` knows *what* to build; `Compiler` knows *how* to build it.
2.  **Single Compiler per Language**: A build tree uses exactly one compiler for a given language (e.g., `CMAKE_CXX_COMPILER`).
3.  **Command Factory**: The `Compiler` class generates shell commands but does not execute them.

## 1. Language Detection & Filtering
Language is determined by file extension via `LanguageClassifier`.

- `.cpp`, `.cc`, `.cxx`, `.C` -> `Language::CXX`
- `.c` -> `Language::C`
- `.cu` -> `Language::CUDA`
- `.h`, `.hpp`, `.hxx`, `.hh` -> `Language::HEADER`

## 2. Compiler Abstraction

The `Compiler` interface abstracts the command-line syntax.

```cpp
struct CompileContext {
    std::string source;
    std::string output;
    std::vector<std::string> includes;    // Absolute or project-relative
    std::vector<std::string> definitions; // Without -D
    std::vector<std::string> options;     // Raw flags
    std::string standard;                 // e.g. "23"
    bool is_shared;                       // true if -fPIC needed
    std::string pch_include;              // Optional "-include wrapper.hpp"
};

struct LinkContext {
    std::string output;
    std::vector<std::string> objects;
    std::vector<std::string> lib_dirs;
    std::vector<std::string> libs;
    bool is_shared;
    std::string standard;
};

class Compiler {
public:
    virtual std::string get_compile_command(const CompileContext& ctx) const = 0;
    virtual std::string get_link_command(const LinkContext& ctx) const = 0;
    virtual std::string get_archive_command(const std::string& output, const std::vector<std::string>& objs) const = 0;
};
```

### GnuCompiler Implementation
This will be the default for GCC and Clang. It will handle the `-MMD` dependency generation and standard flag mapping.

## 3. Toolchain
The `Toolchain` class manages the active compilers for a build session.

```cpp
class Toolchain {
public:
    void set_compiler(Language lang, std::unique_ptr<Compiler> compiler);
    const Compiler* get_compiler(Language lang) const;
};
```

## 4. Integration Steps
1.  **Refactor Target**: `Target::build_compile_command` will be replaced by calls to the appropriate `Compiler` from the toolchain.
2.  **Linking Logic**: `Target` will determine the "Linker Language" (usually CXX if any CXX files are present) and use that compiler's `get_link_command`.
