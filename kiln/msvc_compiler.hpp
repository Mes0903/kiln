#pragma once

#include "compiler.hpp"
#include "genex_parser.hpp"
#include "platform/host.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace kiln {

namespace msvc_detail {

#if defined(_WIN32)
inline FILE* open_command_pipe(const char* command) {
    return _popen(command, "r");
}

inline int close_command_pipe(FILE* pipe) {
    return _pclose(pipe);
}
#else
inline FILE* open_command_pipe(const char* command) {
    return popen(command, "r");
}

inline int close_command_pipe(FILE* pipe) {
    return pclose(pipe);
}
#endif

inline std::string quote_shell_argument(std::string_view value) {
#if defined(_WIN32)
    std::string quoted;
    quoted.reserve(value.size() + 2);
    quoted.push_back('"');
    for (char ch : value) {
        if (ch == '"') {
            quoted += "\\\"";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted.push_back('"');
    return quoted;
#else
    std::string quoted = "'";
    for (char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted.push_back('\'');
    return quoted;
#endif
}

inline std::string version_probe_command(std::string_view binary) {
    return quote_shell_argument(binary) + " 2>&1";
}

inline std::string run_command(const std::string& command) {
    std::array<char, 256> buffer;
    std::string result;

    std::unique_ptr<FILE, int (*)(FILE*)> pipe(open_command_pipe(command.c_str()), close_command_pipe);
    if (!pipe) { return {}; }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) { result += buffer.data(); }
    return result;
}

inline std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

inline bool has_path_separator(std::string_view value) {
    return value.find('/') != std::string_view::npos || value.find('\\') != std::string_view::npos;
}

inline bool ends_with_case_insensitive(std::string_view value, std::string_view suffix) {
    if (value.size() < suffix.size()) return false;
    const std::string tail(value.substr(value.size() - suffix.size()));
    return lowercase(tail) == lowercase(std::string(suffix));
}

inline std::vector<std::string> split_path_list(const char* value) {
    std::vector<std::string> result;
    if (value == nullptr || *value == '\0') return result;

    std::stringstream ss(value);
    std::string part;
    while (std::getline(ss, part, ';')) {
        if (!part.empty()) result.push_back(part);
    }
    return result;
}

inline bool tool_output_looks_missing(const std::string& output) {
    const std::string lower = lowercase(output);
    return output.empty() || lower.find("not recognized") != std::string::npos || lower.find("not found") != std::string::npos
           || lower.find("no such file") != std::string::npos;
}

inline std::string developer_prompt_hint() {
    return "Run kiln from Developer PowerShell for VS or a Native Tools Command Prompt so cl.exe, link.exe, lib.exe, INCLUDE, LIB, and "
           "LIBPATH are all initialized.";
}

inline void append_shell_split(std::vector<std::string>& cmd, const std::string& value) {
    std::stringstream ss(value);
    std::string arg;
    while (ss >> arg) cmd.push_back(arg);
}

inline void append_linker_prefixed(std::vector<std::string>& cmd, std::string_view value) {
    std::size_t pos = 0;
    while (pos <= value.size()) {
        const auto comma = value.find(',', pos);
        const auto end = comma == std::string_view::npos ? value.size() : comma;
        if (end > pos) cmd.emplace_back(value.substr(pos, end - pos));
        if (comma == std::string_view::npos) break;
        pos = comma + 1;
    }
}

inline bool is_msvc_binary_name(std::string_view binary) {
    const auto slash = binary.find_last_of("/\\");
    const std::string name = lowercase(std::string(slash == std::string_view::npos ? binary : binary.substr(slash + 1)));
    return name == "cl" || name == "cl.exe";
}

} // namespace msvc_detail

class MsvcCompiler : public Compiler {
public:
    explicit MsvcCompiler(std::string binary, Language lang) : binary_(std::move(binary)), lang_(lang) {}

    const std::string& binary() const override { return binary_; }

    std::string std_compile_option(Language lang, int standard) const override {
        if (lang == Language::CXX) {
            if (standard == 14 || standard == 17 || standard == 20) return "/std:c++" + std::to_string(standard);
            if (standard == 23) return "/std:c++latest";
            return {};
        }
        if (lang == Language::C) {
            if (standard == 11 || standard == 17) return "/std:c" + std::to_string(standard);
            return {};
        }
        return {};
    }

    CompilerCommand get_compile_command(const CompileContext& ctx) const override {
        std::vector<std::string> cmd;
        cmd.push_back(binary_);
        cmd.push_back("/nologo");
        cmd.push_back("/c");

        if (!ctx.standard.empty() && lang_ != Language::ASM) {
            const std::string flag = standard_flag_from_string(lang_, ctx.standard);
            if (!flag.empty()) cmd.push_back(flag);
        }

        for (const auto& opt : ctx.options) {
            if (opt.empty()) continue;
            if (opt.starts_with("SHELL:")) {
                msvc_detail::append_shell_split(cmd, opt.substr(6));
            } else {
                cmd.push_back(opt);
            }
        }

        for (const auto& dir : ctx.includes) cmd.push_back("/I" + dir);
        for (const auto& dir : ctx.system_includes) cmd.push_back("/external:I" + dir);

        for (const auto& def : ctx.definitions) {
            std::string clean = def;
            if (clean.starts_with("-D") || clean.starts_with("/D")) clean = clean.substr(2);
            if (!clean.empty()) cmd.push_back("/D" + clean);
        }

        cmd.push_back("/Fo" + ctx.output);
        cmd.push_back(ctx.source);

        for (const auto& arg : cmd) assert_no_genex(arg, "MSVC compile command");
        return {cmd, cmd};
    }

    CompilerCommand get_link_command(const LinkContext& ctx) const override {
        if (ctx.is_shared) { throw std::runtime_error("MSVC shared-library linking is not implemented yet"); }

        std::vector<std::string> cmd;
        cmd.push_back("link.exe");
        cmd.push_back("/NOLOGO");
        cmd.push_back("/OUT:" + ctx.output);

        for (const auto& obj : ctx.objects) cmd.push_back(obj);
        for (const auto& dir : ctx.lib_dirs) cmd.push_back("/LIBPATH:" + dir);
        for (const auto& lib : ctx.libs) cmd.push_back(normalize_library_argument(lib));
        for (const auto& flag : ctx.linker_flags) append_linker_flag(cmd, flag);

        for (const auto& arg : cmd) assert_no_genex(arg, "MSVC link command");
        return {cmd, cmd};
    }

    std::vector<std::string> get_archive_command(const std::string& output, const std::vector<std::string>& objs) const override {
        std::vector<std::string> cmd;
        cmd.push_back("lib.exe");
        cmd.push_back("/NOLOGO");
        cmd.push_back("/OUT:" + output);
        for (const auto& obj : objs) cmd.push_back(obj);
        for (const auto& arg : cmd) assert_no_genex(arg, "MSVC archive command");
        return cmd;
    }

    static std::string version_probe_output(const std::string& binary) {
        return msvc_detail::run_command(msvc_detail::version_probe_command(binary));
    }

    PlatformInfo detect_platform() const override {
        std::vector<std::string> missing;

        const std::string cl_output = version_probe_output(binary_);
        if (!banner_is_msvc(cl_output)) missing.push_back(binary_);

        const std::string link_output = msvc_detail::run_command("link.exe /? 2>&1");
        if (msvc_detail::tool_output_looks_missing(link_output) || link_output.find("Microsoft") == std::string::npos)
            missing.push_back("link.exe");

        const std::string lib_output = msvc_detail::run_command("lib.exe /? 2>&1");
        if (msvc_detail::tool_output_looks_missing(lib_output) || lib_output.find("Microsoft") == std::string::npos)
            missing.push_back("lib.exe");

        auto env_missing = [](const char* name) {
            const char* value = std::getenv(name);
            return value == nullptr || *value == '\0';
        };
        if (env_missing("INCLUDE")) missing.push_back("INCLUDE");
        if (env_missing("LIB")) missing.push_back("LIB");
        if (env_missing("LIBPATH")) missing.push_back("LIBPATH");

        if (!missing.empty()) {
            std::string message = "Incomplete MSVC environment: missing ";
            for (std::size_t i = 0; i < missing.size(); ++i) {
                if (i != 0) message += ", ";
                message += missing[i];
            }
            message += ". ";
            message += msvc_detail::developer_prompt_hint();
            throw std::runtime_error(message);
        }

        PlatformInfo info;
        info.compiler_id = "MSVC";
        info.compiler_version = parse_version(cl_output);
        const auto host = platform::host_info();
        info.system_name = host.system_name.empty() ? "Windows" : host.system_name;
        info.system_processor = host.machine;
        info.sizeof_void_p = std::to_string(sizeof(void*));
        info.implicit_includes = msvc_detail::split_path_list(std::getenv("INCLUDE"));
        info.implicit_link_dirs = msvc_detail::split_path_list(std::getenv("LIB"));
        auto libpath_dirs = msvc_detail::split_path_list(std::getenv("LIBPATH"));
        info.implicit_link_dirs.insert(info.implicit_link_dirs.end(), libpath_dirs.begin(), libpath_dirs.end());
        info.default_cxx_standard = 14;
        info.default_c_standard = 0;
        return info;
    }

private:
    std::string standard_flag_from_string(Language lang, const std::string& standard) const {
        try {
            return std_compile_option(lang, std::stoi(standard));
        } catch (...) {
            return {};
        }
    }

    static bool banner_is_msvc(const std::string& output) {
        return output.find("Microsoft") != std::string::npos && output.find("C/C++") != std::string::npos
               && output.find("Version") != std::string::npos;
    }

    static std::string parse_version(const std::string& output) {
        std::smatch match;
        static const std::regex version_re(R"(Version[ \t]+([0-9]+(?:\.[0-9]+)+))");
        if (std::regex_search(output, match, version_re) && match.size() > 1) return match[1].str();
        return {};
    }

    static std::string normalize_library_argument(const std::string& lib) {
        if (lib.empty()) return lib;
        if (lib.starts_with("-") || lib.starts_with("/")) return lib;
        if (msvc_detail::has_path_separator(lib)) return lib;
        if (msvc_detail::ends_with_case_insensitive(lib, ".lib")) return lib;
        if (msvc_detail::ends_with_case_insensitive(lib, ".a")) return lib;
        return lib + ".lib";
    }

    static void append_linker_flag(std::vector<std::string>& cmd, const std::string& flag) {
        if (flag.empty()) return;
        if (flag.starts_with("SHELL:")) {
            msvc_detail::append_shell_split(cmd, flag.substr(6));
            return;
        }
        if (flag.starts_with("LINKER:")) {
            msvc_detail::append_linker_prefixed(cmd, std::string_view(flag).substr(7));
            return;
        }
        cmd.push_back(flag);
    }

    std::string binary_;
    Language lang_;
};

inline bool is_msvc_compiler_binary(std::string_view binary) {
    return msvc_detail::is_msvc_binary_name(binary);
}

} // namespace kiln
