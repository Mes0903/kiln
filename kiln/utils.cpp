#include "utils.hpp"
#include "build_system.hpp"
#include "container_utils.hpp"
#include "inner/blake2b.h"
#include "inner/sha256.h"
#include "inner/md5.h"
#include "inner/sha1.h"
#include "platform/host.hpp"
#include <type_traits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <charconv>
#include <filesystem>

kiln::Hash256 kiln::blake2b(const void* data, size_t len, const void* key, size_t keylen) {
    Hash256 hash;
    static_assert(sizeof(hash) == 32, "Hash256 must be 32 bytes");
    static_assert(std::is_standard_layout<Hash256>::value, "Hash256 must be a standard layout type");
    kiln_blake2b(&hash, sizeof(hash), data, len, key, keylen);
    return hash;
}

kiln::Hash256 kiln::sha256(const void* data, size_t len) {
    Hash256 hash;
    SHA256_CTX ctx;
    kiln_sha256_init(&ctx);
    kiln_sha256_update(&ctx, (const uint8_t*) data, len);
    kiln_sha256_final(&ctx, (uint8_t*) &hash);
    return hash;
}

kiln::Hash160 kiln::sha1(const void* data, size_t len) {
    Hash160 hash;
    SHA1_CTX ctx;
    kiln_sha1_init(&ctx);
    kiln_sha1_update(&ctx, (const unsigned char*) data, len);
    kiln_sha1_final((unsigned char*) &hash, &ctx);
    return hash;
}

kiln::Hash128 kiln::md5(const void* data, size_t len) {
    Hash128 hash;
    MD5_CTX ctx;
    kiln_md5_init(&ctx);
    kiln_md5_update(&ctx, (const uint8_t*) data, len);
    kiln_md5_final(&ctx, (uint8_t*) &hash);
    return hash;
}

std::string kiln::Hash256::to_string() const {
    std::string result;
    result.reserve(2 * sizeof(bytes));
    for (unsigned char byte : bytes) {
        result += "0123456789abcdef"[byte >> 4];
        result += "0123456789abcdef"[byte & 0xf];
    }
    return result;
}

std::string kiln::Hash160::to_string() const {
    std::string result;
    result.reserve(2 * sizeof(bytes));
    for (unsigned char byte : bytes) {
        result += "0123456789abcdef"[byte >> 4];
        result += "0123456789abcdef"[byte & 0xf];
    }
    return result;
}

std::string kiln::Hash128::to_string() const {
    std::string result;
    result.reserve(2 * sizeof(bytes));
    for (unsigned char byte : bytes) {
        result += "0123456789abcdef"[byte >> 4];
        result += "0123456789abcdef"[byte & 0xf];
    }
    return result;
}

kiln::CommandResult kiln::run_command(const std::string& command, const std::string& working_dir) {
    // Avoid popen/sh whenever possible. shell_split handles quoting/escapes
    // exactly like the shell would for the simple word-splitting case; the
    // vector overload then handles redirects (<, >, >>, 2>) natively via
    // fork+dup2 and only falls back to spawning a shell for genuine shell
    // pipelines/logic (|, &&, ||, 2>&1). This is critical: the string
    // overload was previously the hottest user of process-global chdir,
    // which races between concurrent build workers.
    auto argv = shell_split(command);
    if (argv.empty()) { return {-1, "Empty command"}; }
    return run_command(argv, working_dir);
}

std::string kiln::escape_shell_arg(const std::string& arg) {
    if (arg.empty()) return "''";

    bool needed = false;
    for (char c : arg) {
        if (std::isspace(static_cast<unsigned char>(c)) || c == '\'' || c == '"' || c == '\\' || c == '`' || c == '$' || c == '&'
            || c == '|' || c == ';' || c == '<' || c == '>' || c == '(' || c == ')' || c == '{' || c == '}' || c == '[' || c == ']'
            || c == '*' || c == '?' || c == '~' || c == '#' || c == '!' || c == '^') {
            needed = true;
            break;
        }
    }

    if (!needed) return arg;

    std::string result = "'";
    for (char c : arg) {
        if (c == '\'') {
            result += "'\\''";
        } else {
            result += c;
        }
    }
    result += "'";
    return result;
}

// Check if arg starts with a shell redirection prefix.
// Returns the length of the prefix (0 if none).
// Handles: >>, 2>>, 2>, 1>, >, <
static size_t shell_redirect_prefix_len(const std::string& arg) {
    // Order matters: check longer prefixes first
    if (arg.starts_with("2>>")) return 3;
    if (arg.starts_with("1>>")) return 3;
    if (arg.starts_with(">>")) return 2;
    if (arg.starts_with("2>")) return 2;
    if (arg.starts_with("1>")) return 2;
    if (arg.starts_with(">")) return 1;
    if (arg.starts_with("<")) return 1;
    return 0;
}

// Find an embedded redirect operator within an argument (not at position 0).
// Returns {position, operator_length} or {npos, 0} if none found.
// Handles cases like "file.sql>" or "file.sql>output" where the redirect
// is glued to the preceding text (common in CMake COMMAND arguments).
static std::pair<size_t, size_t> find_embedded_redirect(const std::string& arg) {
    for (size_t i = 1; i < arg.size(); ++i) {
        if (arg[i] == '>') {
            if (i + 1 < arg.size() && arg[i + 1] == '>') {
                return {i, 2}; // >>
            }
            return {i, 1}; // >
        }
        if (arg[i] == '<') {
            return {i, 1}; // <
        }
    }
    return {std::string::npos, 0};
}

static bool is_shell_operator(const std::string& arg) {
    return arg == "|" || arg == "&&" || arg == "||" || arg == "2>&1" || arg == "(" || arg == ")";
}

std::string kiln::join_command(const std::vector<std::string>& args) {
    std::string result;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) result += " ";
        if (is_shell_operator(args[i])) {
            result += args[i];
        } else if (size_t pfx = shell_redirect_prefix_len(args[i])) {
            // Split redirection operator from path: ">file" → > + escaped(file)
            result += args[i].substr(0, pfx);
            std::string path = args[i].substr(pfx);
            if (!path.empty()) { result += escape_shell_arg(path); }
        } else if (auto [pos, len] = find_embedded_redirect(args[i]); pos != std::string::npos) {
            // Split embedded redirect: "file.sql>" → escaped(file.sql) + " >" + escaped(path)
            result += escape_shell_arg(args[i].substr(0, pos));
            result += " ";
            result += args[i].substr(pos, len);
            std::string path = args[i].substr(pos + len);
            if (!path.empty()) { result += escape_shell_arg(path); }
        } else {
            result += escape_shell_arg(args[i]);
        }
    }
    return result;
}

std::string kiln::join_command_raw(const std::vector<std::string>& args) {
    return join(args, " ");
}

// Strip shell-style embedded quoting from a COMMAND argument.
//
// CMake allows embedded quoted segments in unquoted arguments like:
//   -flag="${VAR}"  →  should become -flag=value (quotes are shell grouping)
//
// But we must NOT strip quotes that are content, like:
//   print("hello")  →  should stay as print("hello") (quotes are Python syntax)
//
// The heuristic: only strip quotes that form a "value segment" pattern:
//   - Quotes immediately after = (like -DFOO="bar")
//   - Quotes that wrap the entire argument (like "value")
//   - Single quotes following shell conventions
//
// We do NOT strip quotes that appear mid-content (like function calls).
std::string kiln::strip_shell_quoting(const std::string& arg) {
    if (arg.empty()) return arg;

    // Case 1: Entire argument is quoted (rare, but handle it)
    if ((arg.front() == '"' && arg.back() == '"' && arg.size() >= 2) || (arg.front() == '\'' && arg.back() == '\'' && arg.size() >= 2)) {
        return arg.substr(1, arg.size() - 2);
    }

    // Case 2: Pattern like -flag="value" or VAR="value"
    // Look for =" or =' and strip the quotes around the value part
    std::string result;
    result.reserve(arg.size());

    for (size_t i = 0; i < arg.size(); ++i) {
        // Check for =" or =' pattern
        if (arg[i] == '=' && i + 1 < arg.size() && (arg[i + 1] == '"' || arg[i + 1] == '\'')) {
            char quote = arg[i + 1];
            size_t end = arg.find(quote, i + 2);
            if (end != std::string::npos && (end == arg.size() - 1 || arg[end + 1] == '=' || arg[end + 1] == ' ')) {
                // Found a complete quoted value segment after =
                result += '=';
                result.append(arg, i + 2, end - i - 2);
                i = end;
                continue;
            }
        }
        result += arg[i];
    }

    return result;
}

kiln::CommandResult kiln::run_command(const std::vector<std::string>& command, const std::string& working_dir) {
    return platform::run_command(command, working_dir);
}

std::string kiln::get_executable_path() {
    return platform::executable_path();
}

kiln::PipelineResult kiln::execute_pipeline(const std::vector<std::vector<std::string>>& commands, const ProcessOptions& options) {
    return platform::execute_pipeline(commands, options);
}

std::string kiln::to_upper(std::string_view str) {
    std::string result;
    result.reserve(str.size());
    for (char c : str) { result += (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c; }
    return result;
}

std::string kiln::to_lower(std::string_view str) {
    std::string result;
    result.reserve(str.size());
    for (char c : str) { result += (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }
    return result;
}

std::string_view kiln::strip(std::string_view str) {
    constexpr const char* whitespace = " \t\n\r\f\v";
    size_t start = str.find_first_not_of(whitespace);
    if (start == std::string_view::npos) return {};
    size_t end = str.find_last_not_of(whitespace);
    return str.substr(start, end - start + 1);
}

std::string_view kiln::lstrip(std::string_view str) {
    constexpr const char* whitespace = " \t\n\r\f\v";
    size_t start = str.find_first_not_of(whitespace);
    if (start == std::string_view::npos) return {};
    return str.substr(start);
}

std::string_view kiln::rstrip(std::string_view str) {
    constexpr const char* whitespace = " \t\n\r\f\v";
    size_t end = str.find_last_not_of(whitespace);
    if (end == std::string_view::npos) return {};
    return str.substr(0, end + 1);
}

std::string kiln::replace_all(std::string str, std::string_view from, std::string_view to) {
    if (from.empty()) return str;
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.size(), to);
        pos += to.size();
    }
    return str;
}

std::vector<std::string> kiln::shell_split(std::string_view input) {
    std::vector<std::string> result;
    std::string current;
    char quote_char = 0;

    for (size_t i = 0; i < input.size(); ++i) {
        char c = input[i];

        if (quote_char) {
            if (c == quote_char) {
                quote_char = 0;
            } else {
                current += c;
            }
        } else {
            if (c == '"' || c == '\'') {
                quote_char = c;
            } else if (c == ' ' || c == '\t') {
                if (!current.empty()) {
                    result.push_back(std::move(current));
                    current.clear();
                }
            } else {
                current += c;
            }
        }
    }

    if (!current.empty()) { result.push_back(std::move(current)); }

    return result;
}

const std::string& kiln::cmake_extra_modules_root() {
    static const std::string root = [] -> std::string {
        if (std::filesystem::is_directory("/usr/share/cmake/Modules")) return {};

        std::vector<std::pair<std::pair<int, int>, std::string>> candidates;

        std::error_code ec;
        for (auto& entry : std::filesystem::directory_iterator("/usr/share", ec)) {
            auto name = entry.path().filename().string();
            if (!name.starts_with("cmake-")) continue;

            std::string_view ver(name.c_str() + 6, name.size() - 6);
            int major = 0, minor = 0;
            auto [p1, e1] = std::from_chars(ver.data(), ver.data() + ver.size(), major);
            if (e1 != std::errc{} || p1 >= ver.data() + ver.size() || *p1 != '.') continue;
            auto [p2, e2] = std::from_chars(p1 + 1, ver.data() + ver.size(), minor);
            if (e2 != std::errc{}) continue;

            candidates.emplace_back(std::pair{major, minor}, entry.path().string());
        }

        std::ranges::sort(candidates, std::greater{}, [](auto& c) { return c.first; });

        for (auto& [ver, path] : candidates) {
            if (std::filesystem::is_directory(path + "/Modules")) return path;
        }

        return {};
    }();
    return root;
}

const std::string& kiln::gnu_arch_triplet() {
    static const std::string triplet = [] {
#ifdef __linux__
        std::string machine = platform::host_info().machine;
        if (machine.empty()) {
            std::fprintf(stderr, "fatal: uname() failed\n");
            abort();
        }
        if (machine == "x86_64") return std::string("x86_64-linux-gnu");
        if (machine == "aarch64") return std::string("aarch64-linux-gnu");
        if (machine == "armv7l") return std::string("arm-linux-gnueabihf");
        if (machine == "i686" || machine == "i386") return std::string("i386-linux-gnu");
        if (machine == "riscv64") return std::string("riscv64-linux-gnu");
        if (machine == "s390x") return std::string("s390x-linux-gnu");
        if (machine == "ppc64le") return std::string("powerpc64le-linux-gnu");
        std::fprintf(stderr, "fatal: unrecognized architecture '%s' - please add support for it\n", machine.c_str());
        abort();
#else
        return std::string{};
#endif
    }();
    return triplet;
}
