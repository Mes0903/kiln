#pragma once
#include <string>
#include <vector>

namespace kiln {

// A command emitted by a Compiler driver, in two views:
//   - argv: the actual argv to spawn (includes cosmetic flags like
//     -fdiagnostics-color=always).
//   - signature_argv: the same command with cosmetic-only flags omitted, safe
//     to feed into the build cache signature so toggling color/presentation
//     doesn't bust cache hits. The driver itself decides which flags it
//     emitted are cosmetic — we never parse argv strings to find out.
//
// User-supplied options (CompileContext::options et al) pass through
// unchanged in both views. Anything smuggled into a user CFLAGS string is
// the user's problem, same as in CMake/ninja.
struct CompilerCommand {
    std::vector<std::string> argv;
    std::vector<std::string> signature_argv;
};

struct CompileContext {
    std::string source;
    std::string output;
    std::vector<std::string> includes;
    std::vector<std::string> system_includes;  // Directories to include with -isystem (no warnings)
    std::vector<std::string> definitions;
    std::vector<std::string> options;
    std::string standard;
    bool extensions_enabled = true;  // GNU extensions (gnu11 vs c11)
    bool is_shared = false;
    bool is_pie = false;                        // -fPIE for executables with POSITION_INDEPENDENT_CODE
    std::string visibility_preset;          // e.g. "hidden", "default", "protected", "internal"
    bool visibility_inlines_hidden = false; // -fvisibility-inlines-hidden (GCC/Clang)
    std::string pch_include;
    bool color_diagnostics = false;

    // C++20 modules support
    bool is_module_source = false;         // True if source uses modules
    std::string module_mapper_file;        // Path to module mapper file (for -fmodule-mapper=)
    std::string bmi_output;                // Path for BMI output (if this provides a module)
    std::vector<std::string> module_files; // Explicit -fmodule-file= arguments
};

struct ModuleScanContext {
    std::string source;
    std::string output;                    // DDI output file path
    std::vector<std::string> includes;
    std::vector<std::string> system_includes;  // Directories to include with -isystem (no warnings)
    std::vector<std::string> definitions;
    std::string standard;
    bool extensions_enabled = true;  // GNU extensions (gnu11 vs c11)
    bool color_diagnostics = false;
};

struct LinkContext {
    std::string output;
    std::vector<std::string> objects;
    std::vector<std::string> lib_dirs;
    std::vector<std::string> libs;
    std::vector<std::string> linker_flags;
    // Compiler/system implicit link search dirs (CMAKE_<LANG>_IMPLICIT_LINK_DIRECTORIES).
    // Used to skip system paths when emitting RUNPATH so we don't pollute it.
    std::vector<std::string> implicit_link_dirs;
    // CMAKE_SKIP_BUILD_RPATH / SKIP_BUILD_RPATH target property.
    // When true, do not embed any link-path-derived RUNPATH entries.
    bool skip_build_rpath = false;
    bool is_shared = false;
    bool is_pie = false;  // -pie for executables with POSITION_INDEPENDENT_CODE
    std::string standard;
    bool extensions_enabled = true;  // GNU extensions (gnu11 vs c11)
    bool color_diagnostics = false;
};

// Platform information detected from the compiler and system
struct PlatformInfo {
    std::string compiler_id;                    // "GNU", "Clang", etc.
    std::string compiler_version;               // "11.3.0"
    std::string system_name;                    // "Linux", "Darwin", "Windows"
    std::string system_processor;               // "x86_64", "aarch64", etc.
    std::string sizeof_void_p;                  // "8" or "4"
    std::vector<std::string> implicit_includes; // Implicit include directories
    std::vector<std::string> implicit_link_dirs;// Implicit link directories
    std::vector<std::string> implicit_link_libs;// Implicit link libraries
    int default_cxx_standard = 0;               // Compiler's default C++ standard (e.g. 17)
    int default_c_standard = 0;                 // Compiler's default C standard (e.g. 17)
};

class Compiler {
public:
    virtual ~Compiler() = default;

    // Identity: the binary path / sysroot / compiler-target this Compiler
    // instance was configured with. Used by Toolchain to dedupe registry
    // entries — two captures with the same identity tuple resolve to the
    // same Compiler*. Default-empty for compilers that don't carry these
    // (only the GNU/Clang family does today).
    virtual const std::string& binary() const { static const std::string e; return e; }
    virtual const std::string& sysroot() const { static const std::string e; return e; }
    virtual const std::string& compiler_target() const { static const std::string e; return e; }

    virtual CompilerCommand get_compile_command(const CompileContext& ctx) const = 0;
    virtual CompilerCommand get_link_command(const LinkContext& ctx) const = 0;
    virtual std::vector<std::string> get_archive_command(const std::string& output, const std::vector<std::string>& objs) const = 0;

    // Files the linker writes as a side effect of producing the primary
    // output, parsed out of the final argv. CMake doesn't track these
    // explicitly, but custom_target DEPENDS frequently reference them
    // (e.g. -Map= map files, --out-implib= MinGW import libraries) and
    // kiln's stricter dependency graph stalls if no producer is registered.
    //
    // Returned paths are as they appear in argv (unnormalized). The caller
    // resolves them against the link task's working directory. Drivers that
    // emit no side-effect outputs (or aren't implemented yet, e.g. MSVC
    // /MAP, /IMPLIB, /PDB) return an empty vector.
    virtual std::vector<std::string> get_link_side_effect_outputs(
        const std::vector<std::string>& argv) const {
        (void)argv;
        return {};
    }

    // C++20 modules support
    virtual CompilerCommand get_module_scan_command(const ModuleScanContext& ctx) const {
        (void)ctx;
        return {}; // Default: no module support
    }

    // Platform detection - detects compiler info and system platform
    virtual PlatformInfo detect_platform() const {
        return {}; // Default: empty platform info
    }
};

} // namespace kiln
