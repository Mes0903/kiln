#pragma once
#include "compiler.hpp"
#include "language.hpp"
#include <sstream>

namespace dmake {

class GnuCompiler : public Compiler {
public:
    explicit GnuCompiler(std::string binary, Language lang) 
        : binary_(std::move(binary)), lang_(lang) {}

    std::string get_compile_command(const CompileContext& ctx) const override {
        std::ostringstream cmd;
        cmd << binary_;

        if (!ctx.standard.empty()) {
            cmd << " -std=" << (lang_ == Language::C ? "c" : "c++") << ctx.standard;
        }

        if (ctx.color_diagnostics) {
            cmd << " -fdiagnostics-color=always";
        }

        for (const auto& opt : ctx.options) cmd << " " << opt;
        for (const auto& def : ctx.definitions) cmd << " -D" << def;

        cmd << " -MMD -MF " << ctx.output << ".d";

        if (ctx.is_shared) cmd << " -fPIC";

        cmd << " -c -o " << ctx.output;

        for (const auto& dir : ctx.includes) {
            cmd << " -I" << dir;
        }

        if (!ctx.pch_include.empty()) {
            cmd << " " << ctx.pch_include;
        }

        cmd << " " << ctx.source;

        return cmd.str();
    }

    std::string get_link_command(const LinkContext& ctx) const override {
        std::ostringstream cmd;
        cmd << binary_;

        if (!ctx.standard.empty()) {
            cmd << " -std=" << (lang_ == Language::C ? "c" : "c++") << ctx.standard;
        }

        if (ctx.color_diagnostics) {
            cmd << " -fdiagnostics-color=always";
        }

        cmd << " -Wl,-rpath,'$ORIGIN'";
        if (ctx.is_shared) cmd << " -shared";
        cmd << " -o " << ctx.output;

        for (const auto& obj : ctx.objects) cmd << " " << obj;
        
        for (const auto& dir : ctx.lib_dirs) cmd << " -L" << dir;
        for (const auto& lib : ctx.libs) cmd << " -l" << lib;

        return cmd.str();
    }

    std::string get_archive_command(const std::string& output, const std::vector<std::string>& objs) const override {
        std::ostringstream cmd;
        cmd << "ar rcs " << output;
        for (const auto& obj : objs) cmd << " " << obj;
        return cmd.str();
    }

private:
    std::string binary_;
    Language lang_;
};

} // namespace dmake
