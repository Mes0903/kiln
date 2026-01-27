#pragma once
#include "compiler.hpp"
#include "language.hpp"
#include <sstream>

namespace dmake {

class GnuCompiler : public Compiler {
public:
    explicit GnuCompiler(std::string binary, Language lang) 
        : binary_(std::move(binary)), lang_(lang) {}

    std::vector<std::string> get_compile_command(const CompileContext& ctx) const override {
        std::vector<std::string> cmd;
        cmd.push_back(binary_);

        if (!ctx.standard.empty()) {
            cmd.push_back("-std=" + std::string(lang_ == Language::C ? "c" : "c++") + ctx.standard);
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
        for (const auto& def : ctx.definitions) cmd.push_back("-D" + def);

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
            cmd.push_back("-std=" + std::string(lang_ == Language::C ? "c" : "c++") + ctx.standard);
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
        for (const auto& lib : ctx.libs) cmd.push_back("-l" + lib);

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

        // Definitions
        for (const auto& def : ctx.definitions) {
            cmd.push_back("-D" + def);
        }

        // Source file
        cmd.push_back(ctx.source);

        return cmd;
    }

private:
    std::string binary_;
    Language lang_;
};

} // namespace dmake
