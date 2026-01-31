#pragma once
#include "compiler.hpp"
#include "language.hpp"
#include <sstream>
#include <array>
#include <cstdio>
#include <regex>

#ifdef __unix__
#include <sys/utsname.h>
#endif

namespace dmake {

namespace detail {

// Execute a command and capture stdout
inline std::string run_command(const std::string& command) {
    std::array<char, 128> buffer;
    std::string result;

    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
    if (!pipe) {
        return "";
    }

    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }

    return result;
}

} // namespace detail

class GnuCompiler : public Compiler {
public:
    explicit GnuCompiler(std::string binary, Language lang) 
        : binary_(std::move(binary)), lang_(lang) {}

    std::vector<std::string> get_compile_command(const CompileContext& ctx) const override {
        std::vector<std::string> cmd;
        cmd.push_back(binary_);

        if (!ctx.standard.empty()) {
            std::string std_prefix = (lang_ == Language::C ? "c" : "c++");
            // Use "gnu" prefix if extensions are enabled (gnu11 vs c11)
            if (ctx.extensions_enabled) {
                std_prefix = "gnu" + std_prefix.substr(1);  // "c" -> "gnu", "c++" -> "gnu++"
            }
            cmd.push_back("-std=" + std_prefix + ctx.standard);
        }

        if (ctx.color_diagnostics) {
            cmd.push_back("-fdiagnostics-color=always");
        }

        // C++20 modules support
        if (ctx.is_module_source) {
            cmd.push_back("-fmodules-ts");

            // Module mapper file for resolving import declarations
            if (!ctx.module_mapper_file.empty()) {
                cmd.push_back("-fmodule-mapper=" + ctx.module_mapper_file);
            }

            // Explicit module file mappings
            for (const auto& mf : ctx.module_files) {
                cmd.push_back("-fmodule-file=" + mf);
            }
        }

        for (const auto& opt : ctx.options) cmd.push_back(opt);
        for (const auto& def : ctx.definitions) {
            // Strip -D prefix if present (some CMakeLists.txt files include it)
            std::string clean_def = def;
            if (clean_def.starts_with("-D")) {
                clean_def = clean_def.substr(2);
            }
            cmd.push_back("-D" + clean_def);
        }

        cmd.push_back("-MMD");
        cmd.push_back("-MF");
        cmd.push_back(ctx.output + ".d");

        if (ctx.is_shared) cmd.push_back("-fPIC");

        cmd.push_back("-c");
        cmd.push_back("-o");
        cmd.push_back(ctx.output);

        for (const auto& dir : ctx.includes) {
            cmd.push_back("-I" + dir);
        }
        for (const auto& dir : ctx.system_includes) {
            cmd.push_back("-isystem" + dir);
        }

        if (!ctx.pch_include.empty()) {
            // Split pch_include if it contains multiple arguments (e.g., "-include wrapper.hpp")
            std::stringstream ss(ctx.pch_include);
            std::string arg;
            while (ss >> arg) cmd.push_back(arg);
        }

        cmd.push_back(ctx.source);

        return cmd;
    }

    std::vector<std::string> get_link_command(const LinkContext& ctx) const override {
        std::vector<std::string> cmd;
        cmd.push_back(binary_);

        if (!ctx.standard.empty()) {
            std::string std_prefix = (lang_ == Language::C ? "c" : "c++");
            // Use "gnu" prefix if extensions are enabled (gnu11 vs c11)
            if (ctx.extensions_enabled) {
                std_prefix = "gnu" + std_prefix.substr(1);  // "c" -> "gnu", "c++" -> "gnu++"
            }
            cmd.push_back("-std=" + std_prefix + ctx.standard);
        }

        if (ctx.color_diagnostics) {
            cmd.push_back("-fdiagnostics-color=always");
        }

        for (const auto& flag : ctx.linker_flags) cmd.push_back(flag);

        cmd.push_back("-Wl,-rpath,'$ORIGIN'");
        if (ctx.is_shared) cmd.push_back("-shared");
        cmd.push_back("-o");
        cmd.push_back(ctx.output);

        for (const auto& obj : ctx.objects) cmd.push_back(obj);

        for (const auto& dir : ctx.lib_dirs) cmd.push_back("-L" + dir);
        for (const auto& lib : ctx.libs) {
            // Strip -l prefix if present (some CMakeLists.txt files include it)
            std::string clean_lib = lib;
            if (clean_lib.starts_with("-l")) {
                clean_lib = clean_lib.substr(2);
            }
            cmd.push_back("-l" + clean_lib);
        }

        return cmd;
    }

    std::vector<std::string> get_archive_command(const std::string& output, const std::vector<std::string>& objs) const override {
        std::vector<std::string> cmd;
        cmd.push_back("ar");
        cmd.push_back("rcs");
        cmd.push_back(output);
        for (const auto& obj : objs) cmd.push_back(obj);
        return cmd;
    }

    // C++20 modules: generate scan command for extracting module dependencies
    // Uses preprocessor-only mode to quickly extract import/export declarations
    std::vector<std::string> get_module_scan_command(const ModuleScanContext& ctx) const override {
        std::vector<std::string> cmd;
        cmd.push_back(binary_);

        // Set C++ standard (must be C++20 or later for modules)
        if (!ctx.standard.empty()) {
            cmd.push_back("-std=c++" + ctx.standard);
        } else {
            cmd.push_back("-std=c++20");  // Default to C++20 for modules
        }

        // Enable modules TS
        cmd.push_back("-fmodules-ts");

        // Preprocessor-only mode with directives
        cmd.push_back("-E");
        cmd.push_back("-fdirectives-only");

        // Force C++ mode for module interface files
        cmd.push_back("-x");
        cmd.push_back("c++");

        if (ctx.color_diagnostics) {
            cmd.push_back("-fdiagnostics-color=always");
        }

        // Include directories
        for (const auto& dir : ctx.includes) {
            cmd.push_back("-I" + dir);
        }
        for (const auto& dir : ctx.system_includes) {
            cmd.push_back("-isystem" + dir);
        }

        // Definitions
        for (const auto& def : ctx.definitions) {
            // Strip -D prefix if present (some CMakeLists.txt files include it)
            std::string clean_def = def;
            if (clean_def.starts_with("-D")) {
                clean_def = clean_def.substr(2);
            }
            cmd.push_back("-D" + clean_def);
        }

        // Source file
        cmd.push_back(ctx.source);

        return cmd;
    }

    // Platform detection for GCC
    PlatformInfo detect_platform() const override {
        PlatformInfo info;
        info.compiler_id = "GNU";

        // Get compiler version from g++ --version
        std::string version_output = detail::run_command(binary_ + " --version 2>&1");
        // Extract version number (pattern: X.Y.Z)
        std::regex version_regex(R"((\d+\.\d+\.\d+))");
        std::smatch match;
        if (std::regex_search(version_output, match, version_regex)) {
            info.compiler_version = match[1].str();
        }

        // System info from uname
#ifdef __unix__
        struct utsname uname_info;
        if (uname(&uname_info) == 0) {
            info.system_name = uname_info.sysname;
            info.system_processor = uname_info.machine;
        }
#endif

        // Pointer size (compile-time for native builds)
        info.sizeof_void_p = std::to_string(sizeof(void*));

        // Get implicit include directories from g++ -E -v
        std::string lang_flag = (lang_ == Language::C) ? "c" : "c++";
        std::string verbose_output = detail::run_command(
            "echo | " + binary_ + " -E -v -x " + lang_flag + " - 2>&1");

        // Parse "#include <...> search starts here:" section
        bool in_include_section = false;
        std::istringstream iss(verbose_output);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.find("#include <...> search starts here:") != std::string::npos) {
                in_include_section = true;
                continue;
            }
            if (line.find("End of search list.") != std::string::npos) {
                in_include_section = false;
                continue;
            }
            if (in_include_section && !line.empty() && line[0] == ' ') {
                // Trim leading space
                std::string dir = line.substr(1);
                // Remove any trailing " (framework directory)" annotation
                auto paren = dir.find(" (");
                if (paren != std::string::npos) {
                    dir = dir.substr(0, paren);
                }
                if (!dir.empty()) {
                    info.implicit_includes.push_back(dir);
                }
            }
        }

        // Get implicit link directories from g++ -print-search-dirs
        std::string search_dirs = detail::run_command(binary_ + " -print-search-dirs 2>&1");
        std::istringstream search_iss(search_dirs);
        while (std::getline(search_iss, line)) {
            if (line.rfind("libraries: =", 0) == 0) {
                std::string paths = line.substr(12);  // Skip "libraries: ="
                std::istringstream path_stream(paths);
                std::string path;
                while (std::getline(path_stream, path, ':')) {
                    if (!path.empty()) {
                        info.implicit_link_dirs.push_back(path);
                    }
                }
                break;
            }
        }

        // Implicit link libraries - common for GCC
        info.implicit_link_libs = {"stdc++", "m", "gcc_s", "c"};

        return info;
    }

private:
    std::string binary_;
    Language lang_;
};

} // namespace dmake
