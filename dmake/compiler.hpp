#pragma once
#include <string>
#include <vector>
#include <memory>

namespace dmake {

struct CompileContext {
    std::string source;
    std::string output;
    std::vector<std::string> includes;
    std::vector<std::string> definitions;
    std::vector<std::string> options;
    std::string standard;
    bool is_shared = false;
    std::string pch_include;
    bool color_diagnostics = false;
};

struct LinkContext {
    std::string output;
    std::vector<std::string> objects;
    std::vector<std::string> lib_dirs;
    std::vector<std::string> libs;
    std::vector<std::string> linker_flags;
    bool is_shared = false;
    std::string standard;
    bool color_diagnostics = false;
};

class Compiler {
public:
    virtual ~Compiler() = default;
    virtual std::vector<std::string> get_compile_command(const CompileContext& ctx) const = 0;
    virtual std::vector<std::string> get_link_command(const LinkContext& ctx) const = 0;
    virtual std::vector<std::string> get_archive_command(const std::string& output, const std::vector<std::string>& objs) const = 0;
};

} // namespace dmake
