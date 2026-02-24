#pragma once
#include "compiler.hpp"
#include "genex_parser.hpp"
#include "language.hpp"
#include "parse_number.hpp"
#include <sstream>
#include <array>
#include <cstdio>
#include <future>
#include "regex.hpp"

#ifdef __unix__
#include <sys/utsname.h>
#endif

namespace kiln {

namespace detail {

// Execute a command and capture stdout
inline std::string run_command(const std::string& command) {
    std::array<char, 128> buffer;
    std::string result;

    std::unique_ptr<FILE, int(*)(FILE*)> pipe(popen(command.c_str(), "r"), pclose);
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

        if (!ctx.standard.empty() && lang_ != Language::ASM) {
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

        for (const auto& opt : ctx.options) {
            // Handle CMake's SHELL: prefix - strips prefix and passes arguments as-is
            if (opt.starts_with("SHELL:")) {
                // Split the rest by whitespace and add as separate arguments
                std::string rest = opt.substr(6);  // Skip "SHELL:"
                std::stringstream ss(rest);
                std::string arg;
                while (ss >> arg) {
                    cmd.push_back(arg);
                }
            } else {
                cmd.push_back(opt);
            }
        }
        for (const auto& def : ctx.definitions) {
            // Strip -D prefix if present (some CMakeLists.txt files include it)
            std::string clean_def = def;
            if (clean_def.starts_with("-D")) {
                clean_def = clean_def.substr(2);
            }
            // No shell escaping needed: commands are executed via execvp,
            // so each argv element is passed directly to the compiler.
            cmd.push_back("-D" + clean_def);
        }

        cmd.push_back("-MMD");
        cmd.push_back("-MF");
        cmd.push_back(ctx.output + ".d");

        if (ctx.is_shared) cmd.push_back("-fPIC");
        else if (ctx.is_pie) cmd.push_back("-fPIE");
        if (!ctx.visibility_preset.empty()) cmd.push_back("-fvisibility=" + ctx.visibility_preset);
        if (ctx.visibility_inlines_hidden) cmd.push_back("-fvisibility-inlines-hidden");

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

        for (const auto& arg : cmd) assert_no_genex(arg, "compile command");
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

        for (const auto& flag : ctx.linker_flags) {
            // Handle CMake's SHELL: prefix - strips prefix and passes arguments as-is
            if (flag.starts_with("SHELL:")) {
                std::string rest = flag.substr(6);
                std::stringstream ss(rest);
                std::string arg;
                while (ss >> arg) {
                    cmd.push_back(arg);
                }
            } else if (flag.starts_with("LINKER:")) {
                // CMake LINKER: prefix - split on commas, each piece gets -Wl,
                // e.g. LINKER:--version-script,/path/to/map -> -Wl,--version-script -Wl,/path/to/map
                // e.g. LINKER:-Bsymbolic-functions -> -Wl,-Bsymbolic-functions
                std::string rest = flag.substr(7);
                std::string_view sv(rest);
                size_t pos = 0;
                while (pos < sv.size()) {
                    auto comma = sv.find(',', pos);
                    if (comma == std::string_view::npos) comma = sv.size();
                    cmd.push_back("-Wl," + std::string(sv.substr(pos, comma - pos)));
                    pos = comma + 1;
                }
            } else {
                cmd.push_back(flag);
            }
        }

        cmd.push_back("-Wl,-rpath,'$ORIGIN'");
        if (ctx.is_shared) cmd.push_back("-shared");
        cmd.push_back("-o");
        cmd.push_back(ctx.output);

        // Partition ctx.objects into: .o files, static libs (.a), shared libs (.so).
        // GNU ld processes left-to-right: static libs only pull in objects for
        // currently unresolved symbols, so shared libs that satisfy static lib
        // references must come AFTER them.
        std::vector<std::string> obj_files, static_libs, shared_libs;
        for (const auto& obj : ctx.objects) {
            if (obj.ends_with(".a"))
                static_libs.push_back(obj);
            else if (obj.find(".so") != std::string::npos)
                shared_libs.push_back(obj);
            else
                obj_files.push_back(obj);
        }

        for (const auto& o : obj_files) cmd.push_back(o);
        // Wrap static libs in --start-group/--end-group so the linker
        // rescans and resolves circular dependencies between archives.
        if (!static_libs.empty()) {
            cmd.push_back("-Wl,--start-group");
            for (const auto& a : static_libs) cmd.push_back(a);
            cmd.push_back("-Wl,--end-group");
        }
        for (const auto& so : shared_libs) cmd.push_back(so);

        for (const auto& dir : ctx.lib_dirs) cmd.push_back("-L" + dir);
        for (const auto& lib : ctx.libs) {
            // Handle different library formats:
            // - Flags like "-pthread" -> pass through as-is
            // - Already prefixed "-l..." -> pass through as-is
            // - Paths containing "/" -> pass through as-is
            // - Plain library names -> add "-l" prefix
            if (lib.starts_with("-") || lib.find('/') != std::string::npos) {
                cmd.push_back(lib);
            } else {
                cmd.push_back("-l" + lib);
            }
        }

        for (const auto& arg : cmd) assert_no_genex(arg, "link command");
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
            // Escape quotes in definition values so they reach the preprocessor
            std::string escaped_def;
            escaped_def.reserve(clean_def.size() + 4);
            for (char c : clean_def) {
                if (c == '"') {
                    escaped_def += "\\\"";
                } else {
                    escaped_def += c;
                }
            }
            cmd.push_back("-D" + escaped_def);
        }

        // Source file
        cmd.push_back(ctx.source);

        return cmd;
    }

    // Platform detection for GCC — runs subprocess calls in parallel
    PlatformInfo detect_platform() const override {
        PlatformInfo info;
        info.compiler_id = "GNU";

        // System info from uname (no subprocess needed)
#ifdef __unix__
        struct utsname uname_info;
        if (uname(&uname_info) == 0) {
            info.system_name = uname_info.sysname;
            info.system_processor = uname_info.machine;
        }
#endif
        info.sizeof_void_p = std::to_string(sizeof(void*));

        // Implicit link libraries - common for GCC (no subprocess needed)
        info.implicit_link_libs = {"stdc++", "m", "gcc_s", "c"};

        // Launch all subprocess calls in parallel
        std::string lang_flag = (lang_ == Language::C || lang_ == Language::ASM) ? "c" : "c++";

        auto version_future = std::async(std::launch::async, [&] {
            return detail::run_command(binary_ + " --version 2>&1");
        });
        auto verbose_future = std::async(std::launch::async, [&] {
            return detail::run_command("echo | " + binary_ + " -E -v -x " + lang_flag + " - 2>&1");
        });
        auto search_dirs_future = std::async(std::launch::async, [&] {
            return detail::run_command(binary_ + " -print-search-dirs 2>&1");
        });
        auto default_std_future = std::async(std::launch::async, [&]() -> int {
            std::string x_lang = (lang_ == Language::CXX) ? "c++" : "c";
            std::string macro_name = (lang_ == Language::CXX) ? "__cplusplus" : "__STDC_VERSION__";
            std::string output = detail::run_command(binary_ + " -dM -E -x " + x_lang + " /dev/null 2>/dev/null");
            std::istringstream iss(output);
            std::string macro_line;
            while (std::getline(iss, macro_line)) {
                if (macro_line.find(macro_name + " ") != std::string::npos) {
                    auto pos = macro_line.rfind(' ');
                    if (pos == std::string::npos) break;
                    std::string val = macro_line.substr(pos + 1);
                    if (!val.empty() && val.back() == 'L') val.pop_back();
                    auto v_opt = parse_number<long>(val);
                    if (!v_opt) break;
                    long v = *v_opt;
                    if (lang_ == Language::CXX) {
                        if (v >= 202602L)      return 26;
                        if (v >= 202302L)      return 23;
                        if (v >= 202002L)      return 20;
                        if (v >= 201703L)      return 17;
                        if (v >= 201402L)      return 14;
                        if (v >= 201103L)      return 11;
                        return 98;
                    } else {
                        if (v >= 202311L)      return 23;
                        if (v >= 201710L)      return 17;
                        if (v >= 201112L)      return 11;
                        if (v >= 199901L)      return 99;
                        return 90;
                    }
                }
            }
            return 0;
        });

        // Collect results
        std::string version_output = version_future.get();
        static auto version_re = Regex::compile(R"((\d+\.\d+\.\d+))").value();
        std::vector<std::string> captures;
        if (version_re.search(version_output, captures)) {
            info.compiler_version = captures[1];
        }

        // Parse implicit includes from -E -v output
        std::string verbose_output = verbose_future.get();
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
                std::string dir = line.substr(1);
                auto paren = dir.find(" (");
                if (paren != std::string::npos) {
                    dir = dir.substr(0, paren);
                }
                if (!dir.empty()) {
                    info.implicit_includes.push_back(dir);
                }
            }
        }

        // Parse implicit link dirs from -print-search-dirs
        std::string search_dirs = search_dirs_future.get();
        std::istringstream search_iss(search_dirs);
        while (std::getline(search_iss, line)) {
            if (line.rfind("libraries: =", 0) == 0) {
                std::string paths = line.substr(12);
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

        // Collect default standard
        int default_std = default_std_future.get();
        if (lang_ == Language::CXX) {
            info.default_cxx_standard = default_std;
        } else {
            info.default_c_standard = default_std;
        }

        return info;
    }

private:
    std::string binary_;
    Language lang_;
};

} // namespace kiln
