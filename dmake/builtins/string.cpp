#include "registry.hpp"
#include "../interperter.hpp"
#include "../command_parser.hpp"
#include "../utils.hpp"
#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>
#include <iomanip>
#include <random>
#include <ctime>
#include <chrono>
#include <sys/stat.h>
#include <filesystem>

namespace dmake {

namespace {

// Helper to convert string to uppercase
std::string to_upper(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return result;
}

// Helper to convert string to lowercase
std::string to_lower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

// Helper to strip leading and trailing whitespace
std::string strip(const std::string& str) {
    size_t start = 0;
    while (start < str.size() && std::isspace(static_cast<unsigned char>(str[start]))) {
        start++;
    }
    if (start == str.size()) return "";

    size_t end = str.size();
    while (end > start && std::isspace(static_cast<unsigned char>(str[end - 1]))) {
        end--;
    }

    return str.substr(start, end - start);
}

// Helper to escape regex special characters
std::string regex_quote(const std::string& str) {
    static const std::string special_chars = "\\^$.|?*+()[]{}-";
    std::string result;
    result.reserve(str.size() * 2);

    for (char c : str) {
        if (special_chars.find(c) != std::string::npos) {
            result += '\\';
        }
        result += c;
    }

    return result;
}

// Helper to strip generator expressions
std::string genex_strip(const std::string& str) {
    std::string result;
    int depth = 0;
    size_t i = 0;

    while (i < str.size()) {
        if (i + 1 < str.size() && str[i] == '$' && str[i + 1] == '<') {
            depth++;
            i += 2;
            continue;
        }

        if (depth > 0 && str[i] == '>') {
            depth--;
            i++;
            continue;
        }

        if (depth == 0) {
            result += str[i];
        }
        i++;
    }

    return result;
}

// Helper to convert CMake-style regex replacement string to C++ std::regex format
// CMake uses \1, \2, etc. for capture groups, $ is literal
// C++ std::regex_replace uses $1, $2, etc., $$ for literal $
std::string cmake_to_cpp_replacement(const std::string& cmake_fmt) {
    std::string result;
    size_t i = 0;

    while (i < cmake_fmt.size()) {
        if (cmake_fmt[i] == '\\' && i + 1 < cmake_fmt.size()) {
            char next = cmake_fmt[i + 1];

            // Check if it's a capture group reference (\1, \2, ..., \9)
            if (next >= '0' && next <= '9') {
                result += '$';
                result += next;
                i += 2;
                continue;
            }

            // Check for escaped backslash (\\)
            if (next == '\\') {
                result += '\\';
                i += 2;
                continue;
            }

            // Other escape sequences - preserve as-is
            result += cmake_fmt[i];
            i++;
        } else if (cmake_fmt[i] == '$') {
            // In CMake, $ is literal, but in C++ regex it's special
            // so we need to escape it as $$
            result += "$$";
            i++;
        } else {
            result += cmake_fmt[i];
            i++;
        }
    }

    return result;
}

// Helper to make a valid C identifier
std::string make_c_identifier(const std::string& str) {
    std::string result;
    result.reserve(str.size());

    for (char c : str) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            result += c;
        } else {
            result += '_';
        }
    }

    return result;
}

// Helper to convert byte to hex
std::string byte_to_hex(unsigned char byte) {
    std::stringstream ss;
    ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    return ss.str();
}

// Helper to perform configure-style variable substitution
std::string configure_string(const std::string& input, Interpreter& interp, bool at_only, bool escape_quotes) {
    std::string result;
    size_t i = 0;

    while (i < input.size()) {
        // Handle ${VAR} syntax
        if (!at_only && i + 2 < input.size() && input[i] == '$' && input[i + 1] == '{') {
            size_t end = input.find('}', i + 2);
            if (end != std::string::npos) {
                std::string var_name = input.substr(i + 2, end - i - 2);
                std::string value = interp.get_variable(var_name);
                if (escape_quotes) {
                    for (char c : value) {
                        if (c == '"') result += '\\';
                        result += c;
                    }
                } else {
                    result += value;
                }
                i = end + 1;
                continue;
            }
        }

        // Handle @VAR@ syntax
        if (i + 1 < input.size() && input[i] == '@') {
            size_t end = input.find('@', i + 1);
            if (end != std::string::npos) {
                std::string var_name = input.substr(i + 1, end - i - 1);
                std::string value = interp.get_variable(var_name);
                if (escape_quotes) {
                    for (char c : value) {
                        if (c == '"') result += '\\';
                        result += c;
                    }
                } else {
                    result += value;
                }
                i = end + 1;
                continue;
            }
        }

        result += input[i];
        i++;
    }

    return result;
}

} // anonymous namespace

// Parse arguments in UNIX_COMMAND mode
std::vector<std::string> parse_unix_command(const std::string& input) {
    std::vector<std::string> result;
    std::string current;
    bool in_single_quote = false;
    bool in_double_quote = false;
    bool escape_next = false;

    for (size_t i = 0; i < input.size(); ++i) {
        char c = input[i];

        if (escape_next) {
            // Backslash escapes the next literal character
            current += c;
            escape_next = false;
            continue;
        }

        if (c == '\\') {
            escape_next = true;
            continue;
        }

        if (c == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
            continue;
        }

        if (c == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
            continue;
        }

        if (std::isspace(c) && !in_single_quote && !in_double_quote) {
            if (!current.empty()) {
                result.push_back(current);
                current.clear();
            }
            continue;
        }

        current += c;
    }

    if (!current.empty()) {
        result.push_back(current);
    }

    return result;
}

// Parse arguments in WINDOWS_COMMAND mode
std::vector<std::string> parse_windows_command(const std::string& input) {
    std::vector<std::string> result;
    std::string current;
    bool in_quote = false;
    size_t i = 0;

    while (i < input.size()) {
        // Count consecutive backslashes
        size_t backslash_count = 0;
        while (i < input.size() && input[i] == '\\') {
            backslash_count++;
            i++;
        }

        if (i < input.size() && input[i] == '"') {
            // Backslashes before quote
            // Even number: half the backslashes in output, quote is delimiter
            // Odd number: half the backslashes (rounded down) in output, quote is literal
            current += std::string(backslash_count / 2, '\\');

            if (backslash_count % 2 == 0) {
                // Quote is not escaped
                // Check for "" (double quote within quoted string = literal quote)
                if (in_quote && i + 1 < input.size() && input[i + 1] == '"') {
                    current += '"';
                    i += 2; // Skip both quotes
                } else {
                    // Toggle quote mode
                    in_quote = !in_quote;
                    i++;
                }
            } else {
                // Quote is escaped, add literal quote
                current += '"';
                i++;
            }
        } else {
            // Backslashes not followed by quote are literal
            current += std::string(backslash_count, '\\');

            if (i < input.size()) {
                char c = input[i];

                if (std::isspace(static_cast<unsigned char>(c)) && !in_quote) {
                    if (!current.empty()) {
                        result.push_back(current);
                        current.clear();
                    }
                    i++;
                } else {
                    current += c;
                    i++;
                }
            }
        }
    }

    if (!current.empty()) {
        result.push_back(current);
    }

    return result;
}

// Search for a program in PATH
std::string find_program_in_path(const std::string& program_name) {
    // If already an absolute path and executable, return as-is
    std::filesystem::path p(program_name);
    if (p.is_absolute()) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(p, ec)) {
            struct stat st;
            if (stat(p.c_str(), &st) == 0 && (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
                return program_name;
            }
        }
    }

    // Search in PATH
    const char* path_env = std::getenv("PATH");
    if (!path_env) return "";

    std::string path_str(path_env);
    size_t start = 0;

    while (start < path_str.size()) {
        size_t end = path_str.find(':', start);
        if (end == std::string::npos) {
            end = path_str.size();
        }

        std::string dir = path_str.substr(start, end - start);
        if (!dir.empty()) {
            std::filesystem::path candidate = std::filesystem::path(dir) / program_name;
            std::error_code ec;

            if (std::filesystem::is_regular_file(candidate, ec)) {
                struct stat st;
                if (stat(candidate.c_str(), &st) == 0 &&
                    (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
                    return std::filesystem::absolute(candidate).string();
                }
            }
        }

        start = end + 1;
    }

    return "";
}

void register_string_builtins(Interpreter& interp) {
    interp.add_builtin("string", [](Interpreter& interp, const std::vector<std::string>& args) {
        if (args.empty()) {
            interp.set_fatal_error("string() requires at least one argument");
            return;
        }

        std::string operation = to_upper(args[0]);
        std::span<const std::string> sub_args(args.begin() + 1, args.end());

        // ========== Search and Replace ==========

        if (operation == "FIND") {
            CommandParser parser("string", "FIND");
            std::string input, substring, out_var;
            bool reverse = false;

            parser.positional(input, "input string");
            parser.positional(substring, "substring");
            parser.positional(out_var, "output variable");
            parser.flag("REVERSE", reverse);

            PARSE_OR_RETURN(parser, interp, sub_args);

            size_t pos;
            if (reverse) {
                pos = input.rfind(substring);
            } else {
                pos = input.find(substring);
            }

            interp.set_variable(out_var, (pos == std::string::npos) ? "-1" : std::to_string(pos));

        } else if (operation == "REPLACE") {
            CommandParser parser("string", "REPLACE");
            std::string match_str, replace_str, out_var;
            std::vector<std::string> inputs;

            parser.positional(match_str, "match string");
            parser.positional(replace_str, "replace string");
            parser.positional(out_var, "output variable");
            parser.positionals(inputs, "inputs");

            PARSE_OR_RETURN(parser, interp, sub_args);

            // Concatenate all inputs
            std::string input;
            for (const auto& s : inputs) {
                input += s;
            }

            // Replace all occurrences
            std::string result;
            size_t pos = 0;
            size_t last_pos = 0;

            while ((pos = input.find(match_str, last_pos)) != std::string::npos) {
                result += input.substr(last_pos, pos - last_pos);
                result += replace_str;
                last_pos = pos + match_str.size();
            }
            result += input.substr(last_pos);

            interp.set_variable(out_var, result);

        } else if (operation == "REGEX") {
            // REGEX operations require a subcommand
            if (sub_args.empty()) {
                interp.set_fatal_error("string(REGEX) requires a subcommand (MATCH, MATCHALL, REPLACE, QUOTE)");
                return;
            }

            std::string regex_op = to_upper(std::string(sub_args[0]));
            std::span<const std::string> regex_args(sub_args.begin() + 1, sub_args.end());

            if (regex_op == "MATCH") {
                CommandParser parser("string", "REGEX MATCH");
                std::string pattern, out_var;
                std::vector<std::string> inputs;

                parser.positional(pattern, "regex pattern");
                parser.positional(out_var, "output variable");
                parser.positionals(inputs, "inputs");

                PARSE_OR_RETURN(parser, interp, regex_args);

                // Concatenate all inputs
                std::string input;
                for (const auto& s : inputs) {
                    input += s;
                }

                try {
                    std::regex re(pattern);
                    std::smatch match;

                    if (std::regex_search(input, match, re)) {
                        interp.set_variable(out_var, match.str());
                        // Also set CMAKE_MATCH_<n> variables
                        for (size_t i = 0; i < match.size(); ++i) {
                            interp.set_variable("CMAKE_MATCH_" + std::to_string(i), match[i].str());
                        }
                        interp.set_variable("CMAKE_MATCH_COUNT", std::to_string(match.size() - 1));
                    } else {
                        interp.set_variable(out_var, "");
                    }
                } catch (const std::regex_error& e) {
                    interp.set_fatal_error("string(REGEX MATCH) invalid regex: " + std::string(e.what()));
                    return;
                }

            } else if (regex_op == "MATCHALL") {
                CommandParser parser("string", "REGEX MATCHALL");
                std::string pattern, out_var;
                std::vector<std::string> inputs;

                parser.positional(pattern, "regex pattern");
                parser.positional(out_var, "output variable");
                parser.positionals(inputs, "inputs");

                PARSE_OR_RETURN(parser, interp, regex_args);

                // Concatenate all inputs
                std::string input;
                for (const auto& s : inputs) {
                    input += s;
                }

                try {
                    std::regex re(pattern);
                    std::vector<std::string> matches;

                    auto begin = std::sregex_iterator(input.begin(), input.end(), re);
                    auto end = std::sregex_iterator();

                    for (std::sregex_iterator i = begin; i != end; ++i) {
                        matches.push_back(i->str());
                    }

                    CMakeList result;
                    for (const auto& m : matches) {
                        result.append(m);
                    }

                    interp.set_variable(out_var, result.to_string());
                } catch (const std::regex_error& e) {
                    interp.set_fatal_error("string(REGEX MATCHALL) invalid regex: " + std::string(e.what()));
                    return;
                }

            } else if (regex_op == "REPLACE") {
                CommandParser parser("string", "REGEX REPLACE");
                std::string pattern, replacement, out_var;
                std::vector<std::string> inputs;

                parser.positional(pattern, "regex pattern");
                parser.positional(replacement, "replacement expression");
                parser.positional(out_var, "output variable");
                parser.positionals(inputs, "inputs");

                PARSE_OR_RETURN(parser, interp, regex_args);

                // Concatenate all inputs
                std::string input;
                for (const auto& s : inputs) {
                    input += s;
                }

                try {
                    std::regex re(pattern);
                    // Convert CMake-style replacement (\1, \2) to C++ style ($1, $2)
                    std::string cpp_replacement = cmake_to_cpp_replacement(replacement);
                    std::string result = std::regex_replace(input, re, cpp_replacement);
                    interp.set_variable(out_var, result);
                } catch (const std::regex_error& e) {
                    interp.set_fatal_error("string(REGEX REPLACE) invalid regex: " + std::string(e.what()));
                    return;
                }

            } else if (regex_op == "QUOTE") {
                CommandParser parser("string", "REGEX QUOTE");
                std::string out_var;
                std::vector<std::string> inputs;

                parser.positional(out_var, "output variable");
                parser.positionals(inputs, "inputs");

                PARSE_OR_RETURN(parser, interp, regex_args);

                // Concatenate and quote all inputs
                std::string input;
                for (const auto& s : inputs) {
                    input += s;
                }

                interp.set_variable(out_var, regex_quote(input));

            } else {
                interp.set_fatal_error("string(REGEX) unknown subcommand: " + regex_op);
                return;
            }

        // ========== Manipulation ==========

        } else if (operation == "APPEND") {
            CommandParser parser("string", "APPEND");
            std::string var_name;
            std::vector<std::string> inputs;

            parser.positional(var_name, "variable name");
            parser.positionals(inputs, "inputs");

            PARSE_OR_RETURN(parser, interp, sub_args);

            std::string current = interp.get_variable(var_name);
            for (const auto& s : inputs) {
                current += s;
            }

            interp.set_variable(var_name, current);

        } else if (operation == "PREPEND") {
            CommandParser parser("string", "PREPEND");
            std::string var_name;
            std::vector<std::string> inputs;

            parser.positional(var_name, "variable name");
            parser.positionals(inputs, "inputs");

            PARSE_OR_RETURN(parser, interp, sub_args);

            std::string prefix;
            for (const auto& s : inputs) {
                prefix += s;
            }

            std::string current = interp.get_variable(var_name);
            interp.set_variable(var_name, prefix + current);

        } else if (operation == "CONCAT") {
            CommandParser parser("string", "CONCAT");
            std::string out_var;
            std::vector<std::string> inputs;

            parser.positional(out_var, "output variable");
            parser.positionals(inputs, "inputs");

            PARSE_OR_RETURN(parser, interp, sub_args);

            std::string result;
            for (const auto& s : inputs) {
                result += s;
            }

            interp.set_variable(out_var, result);

        } else if (operation == "JOIN") {
            CommandParser parser("string", "JOIN");
            std::string glue, out_var;
            std::vector<std::string> inputs;

            parser.positional(glue, "glue string");
            parser.positional(out_var, "output variable");
            parser.positionals(inputs, "inputs");

            PARSE_OR_RETURN(parser, interp, sub_args);

            std::string result;
            for (size_t i = 0; i < inputs.size(); ++i) {
                if (i > 0) result += glue;
                result += inputs[i];
            }

            interp.set_variable(out_var, result);

        } else if (operation == "TOLOWER") {
            CommandParser parser("string", "TOLOWER");
            std::string input, out_var;

            parser.positional(input, "input string");
            parser.positional(out_var, "output variable");

            PARSE_OR_RETURN(parser, interp, sub_args);

            interp.set_variable(out_var, to_lower(input));

        } else if (operation == "TOUPPER") {
            CommandParser parser("string", "TOUPPER");
            std::string input, out_var;

            parser.positional(input, "input string");
            parser.positional(out_var, "output variable");

            PARSE_OR_RETURN(parser, interp, sub_args);

            interp.set_variable(out_var, to_upper(input));

        } else if (operation == "LENGTH") {
            CommandParser parser("string", "LENGTH");
            std::string input, out_var;

            parser.positional(input, "input string");
            parser.positional(out_var, "output variable");

            PARSE_OR_RETURN(parser, interp, sub_args);

            interp.set_variable(out_var, std::to_string(input.size()));

        } else if (operation == "SUBSTRING") {
            CommandParser parser("string", "SUBSTRING");
            std::string input, begin_str, length_str, out_var;

            parser.positional(input, "input string");
            parser.positional(begin_str, "begin index");
            parser.positional(length_str, "length");
            parser.positional(out_var, "output variable");

            PARSE_OR_RETURN(parser, interp, sub_args);

            try {
                int begin = std::stoi(begin_str);
                int length = std::stoi(length_str);

                if (begin < 0 || static_cast<size_t>(begin) > input.size()) {
                    interp.set_fatal_error("string(SUBSTRING) begin index out of range: " + begin_str);
                    return;
                }

                std::string result;
                if (length < 0) {
                    // -1 means to the end
                    result = input.substr(begin);
                } else {
                    result = input.substr(begin, length);
                }

                interp.set_variable(out_var, result);
            } catch (const std::exception& e) {
                interp.set_fatal_error("string(SUBSTRING) invalid indices: " + std::string(e.what()));
                return;
            }

        } else if (operation == "STRIP") {
            CommandParser parser("string", "STRIP");
            std::string input, out_var;

            parser.positional(input, "input string");
            parser.positional(out_var, "output variable");

            PARSE_OR_RETURN(parser, interp, sub_args);

            interp.set_variable(out_var, strip(input));

        } else if (operation == "GENEX_STRIP") {
            CommandParser parser("string", "GENEX_STRIP");
            std::string input, out_var;

            parser.positional(input, "input string");
            parser.positional(out_var, "output variable");

            PARSE_OR_RETURN(parser, interp, sub_args);

            interp.set_variable(out_var, genex_strip(input));

        } else if (operation == "REPEAT") {
            CommandParser parser("string", "REPEAT");
            std::string input, count_str, out_var;

            parser.positional(input, "input string");
            parser.positional(count_str, "repeat count");
            parser.positional(out_var, "output variable");

            PARSE_OR_RETURN(parser, interp, sub_args);

            try {
                int count = std::stoi(count_str);
                if (count < 0) {
                    interp.set_fatal_error("string(REPEAT) count must be non-negative");
                    return;
                }

                std::string result;
                for (int i = 0; i < count; ++i) {
                    result += input;
                }

                interp.set_variable(out_var, result);
            } catch (const std::exception& e) {
                interp.set_fatal_error("string(REPEAT) invalid count: " + std::string(e.what()));
                return;
            }

        // ========== Comparison ==========

        } else if (operation == "COMPARE") {
            if (sub_args.empty()) {
                interp.set_fatal_error("string(COMPARE) requires an operator");
                return;
            }

            std::string op = to_upper(std::string(sub_args[0]));
            std::span<const std::string> cmp_args(sub_args.begin() + 1, sub_args.end());

            CommandParser parser("string", "COMPARE " + op);
            std::string str1, str2, out_var;

            parser.positional(str1, "string1");
            parser.positional(str2, "string2");
            parser.positional(out_var, "output variable");

            PARSE_OR_RETURN(parser, interp, cmp_args);

            bool result = false;

            if (op == "LESS") {
                result = str1 < str2;
            } else if (op == "GREATER") {
                result = str1 > str2;
            } else if (op == "EQUAL") {
                result = str1 == str2;
            } else if (op == "NOTEQUAL") {
                result = str1 != str2;
            } else if (op == "LESS_EQUAL") {
                result = str1 <= str2;
            } else if (op == "GREATER_EQUAL") {
                result = str1 >= str2;
            } else {
                interp.set_fatal_error("string(COMPARE) unknown operator: " + op);
                return;
            }

            interp.set_variable(out_var, result ? "1" : "0");

        // ========== Generation ==========

        } else if (operation == "ASCII") {
            CommandParser parser("string", "ASCII");
            std::string out_var;
            std::vector<std::string> codes;

            parser.positionals(codes, "codes");

            PARSE_OR_RETURN(parser, interp, sub_args);

            if (codes.empty()) {
                interp.set_fatal_error("string(ASCII) requires at least one character code");
                return;
            }

            // Last argument is the output variable
            out_var = codes.back();
            codes.pop_back();

            std::string result;
            for (const auto& code_str : codes) {
                try {
                    int code = std::stoi(code_str);
                    if (code < 0 || code > 127) {
                        interp.set_fatal_error("string(ASCII) code out of range [0, 127]: " + code_str);
                        return;
                    }
                    result += static_cast<char>(code);
                } catch (const std::exception& e) {
                    interp.set_fatal_error("string(ASCII) invalid code: " + code_str);
                    return;
                }
            }

            interp.set_variable(out_var, result);

        } else if (operation == "HEX") {
            CommandParser parser("string", "HEX");
            std::string input, out_var;

            parser.positional(input, "input string");
            parser.positional(out_var, "output variable");

            PARSE_OR_RETURN(parser, interp, sub_args);

            std::string result;
            for (unsigned char c : input) {
                result += byte_to_hex(c);
            }

            interp.set_variable(out_var, result);

        } else if (operation == "CONFIGURE") {
            CommandParser parser("string", "CONFIGURE");
            std::string input, out_var;
            bool at_only = false;
            bool escape_quotes = false;

            parser.positional(input, "input string");
            parser.positional(out_var, "output variable");
            parser.flag("@ONLY", at_only);
            parser.flag("ESCAPE_QUOTES", escape_quotes);

            PARSE_OR_RETURN(parser, interp, sub_args);

            std::string result = configure_string(input, interp, at_only, escape_quotes);
            interp.set_variable(out_var, result);

        } else if (operation == "MAKE_C_IDENTIFIER") {
            CommandParser parser("string", "MAKE_C_IDENTIFIER");
            std::string input, out_var;

            parser.positional(input, "input string");
            parser.positional(out_var, "output variable");

            PARSE_OR_RETURN(parser, interp, sub_args);

            interp.set_variable(out_var, make_c_identifier(input));

        } else if (operation == "RANDOM") {
            // Manual parsing for RANDOM since it has complex argument structure
            std::string length_str;
            std::string alphabet;
            std::string seed_str;
            std::string out_var;

            // Parse arguments manually
            size_t i = 0;
            while (i < sub_args.size()) {
                std::string arg = to_upper(std::string(sub_args[i]));

                if (arg == "LENGTH" && i + 1 < sub_args.size()) {
                    length_str = sub_args[i + 1];
                    i += 2;
                } else if (arg == "ALPHABET" && i + 1 < sub_args.size()) {
                    alphabet = sub_args[i + 1];
                    i += 2;
                } else if (arg == "RANDOM_SEED" && i + 1 < sub_args.size()) {
                    seed_str = sub_args[i + 1];
                    i += 2;
                } else {
                    // This should be the output variable (last positional arg)
                    out_var = sub_args[i];
                    i++;
                }
            }

            if (out_var.empty()) {
                interp.set_fatal_error("string(RANDOM) requires an output variable");
                return;
            }

            size_t length = 5; // Default length
            if (!length_str.empty()) {
                try {
                    length = std::stoul(length_str);
                } catch (...) {
                    interp.set_fatal_error("string(RANDOM) invalid LENGTH: " + length_str);
                    return;
                }
            }

            if (alphabet.empty()) {
                alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
            }

            if (alphabet.empty()) {
                interp.set_fatal_error("string(RANDOM) alphabet cannot be empty");
                return;
            }

            std::random_device rd;
            std::mt19937 gen(rd());

            if (!seed_str.empty()) {
                try {
                    unsigned int seed = std::stoul(seed_str);
                    gen.seed(seed);
                } catch (...) {
                    interp.set_fatal_error("string(RANDOM) invalid RANDOM_SEED: " + seed_str);
                    return;
                }
            }

            std::uniform_int_distribution<size_t> dis(0, alphabet.size() - 1);

            std::string result;
            for (size_t i = 0; i < length; ++i) {
                result += alphabet[dis(gen)];
            }

            interp.set_variable(out_var, result);

        } else if (operation == "TIMESTAMP") {
            CommandParser parser("string", "TIMESTAMP");
            std::string out_var, format;
            bool utc = false;
            std::vector<std::string> remaining_args;

            parser.positional(out_var, "output variable");
            parser.positionals(remaining_args, "args"); // For format string
            parser.flag("UTC", utc);

            PARSE_OR_RETURN(parser, interp, sub_args);

            // Extract format string (everything between out_var and UTC flag if present)
            if (sub_args.size() > 1) {
                // Check if last arg is UTC
                bool has_utc_flag = !sub_args.empty() && to_upper(std::string(sub_args.back())) == "UTC";
                size_t format_end = has_utc_flag ? sub_args.size() - 1 : sub_args.size();

                for (size_t i = 1; i < format_end; ++i) {
                    if (i > 1) format += " ";
                    format += sub_args[i];
                }

                if (has_utc_flag) {
                    utc = true;
                }
            }

            if (format.empty()) {
                format = "%Y-%m-%dT%H:%M:%S";
            }

            auto now = std::chrono::system_clock::now();
            std::time_t now_c = std::chrono::system_clock::to_time_t(now);
            std::tm* timeinfo = utc ? std::gmtime(&now_c) : std::localtime(&now_c);

            char buffer[256];
            std::strftime(buffer, sizeof(buffer), format.c_str(), timeinfo);

            interp.set_variable(out_var, std::string(buffer));

        } else if (operation == "UUID") {
            interp.set_fatal_error("string(UUID) is not implemented (requires hashing support)");
            return;

        } else if (operation == "SHA256") {
            CommandParser parser("string", "SHA256");
            std::string input, out_var;

            parser.positional(out_var, "output variable");
            parser.positional(input, "input string");

            PARSE_OR_RETURN(parser, interp, sub_args);
            interp.set_variable(out_var, sha256(input).to_string());

        } else if (operation == "BLAKE2B") {
            CommandParser parser("string", "BLAKE2B");
            std::string input, out_var, key;

            parser.positional(out_var, "output variable");
            parser.positional(input, "input string");
            parser.positional(key, "key", false);

            PARSE_OR_RETURN(parser, interp, sub_args);
            interp.set_variable(out_var, blake2b(input, key).to_string());

        } else {
            interp.set_fatal_error("string() unknown operation: " + operation);
            return;
        }
    });

    // Register separate_arguments() command
    interp.add_builtin("separate_arguments", [](Interpreter& interp, const std::vector<std::string>& args) {
        if (args.empty()) {
            interp.set_fatal_error("separate_arguments() requires at least one argument");
            return;
        }

        // Detect which form: simple conversion or advanced parsing
        // Simple form: separate_arguments(<var>)
        // Advanced form: separate_arguments(<variable> <mode> [PROGRAM [SEPARATE_ARGS]] <args>)

        std::string var_name = args[0];

        // Check if this is the simple form (only one argument)
        if (args.size() == 1) {
            // Simple form: replace spaces with semicolons in the variable's value
            std::string value = interp.get_variable(var_name);
            std::string result;

            for (char c : value) {
                if (std::isspace(c)) {
                    result += ';';
                } else {
                    result += c;
                }
            }

            interp.set_variable(var_name, result);
            return;
        }

        // Advanced form: requires at least 3 arguments (var, mode, args)
        if (args.size() < 3) {
            interp.set_fatal_error("separate_arguments() requires at least 3 arguments for advanced form: <variable> <mode> <args>");
            return;
        }

        std::string mode = to_upper(args[1]);

        // Validate mode
        if (mode != "UNIX_COMMAND" && mode != "WINDOWS_COMMAND" && mode != "NATIVE_COMMAND") {
            interp.set_fatal_error("separate_arguments() invalid mode: " + args[1] + " (expected UNIX_COMMAND, WINDOWS_COMMAND, or NATIVE_COMMAND)");
            return;
        }

        // Determine actual parsing mode
        std::string parse_mode = mode;
        if (mode == "NATIVE_COMMAND") {
#ifdef _WIN32
            parse_mode = "WINDOWS_COMMAND";
#else
            parse_mode = "UNIX_COMMAND";
#endif
        }

        // Check for PROGRAM flag
        bool has_program = false;
        bool has_separate_args = false;
        size_t args_start = 2;

        if (args.size() > 2 && to_upper(args[2]) == "PROGRAM") {
            has_program = true;
            args_start = 3;

            // Check for SEPARATE_ARGS flag
            if (args.size() > 3 && to_upper(args[3]) == "SEPARATE_ARGS") {
                has_separate_args = true;
                args_start = 4;
            }
        }

        if (args_start >= args.size()) {
            interp.set_fatal_error("separate_arguments() missing command string argument");
            return;
        }

        // Concatenate remaining arguments to form the command string
        std::string command_str;
        for (size_t i = args_start; i < args.size(); ++i) {
            if (i > args_start) command_str += " ";
            command_str += args[i];
        }

        // Parse the command string according to the mode
        std::vector<std::string> parsed_args;
        if (parse_mode == "UNIX_COMMAND") {
            parsed_args = parse_unix_command(command_str);
        } else {
            parsed_args = parse_windows_command(command_str);
        }

        // Handle PROGRAM option
        if (has_program) {
            if (parsed_args.empty()) {
                // No program found, set empty list
                interp.set_variable(var_name, "");
                return;
            }

            std::string program = parsed_args[0];
            std::string program_path = find_program_in_path(program);

            if (program_path.empty()) {
                // Program not found, set empty list
                interp.set_variable(var_name, "");
                return;
            }

            if (has_separate_args) {
                // Return [absolute_path, arg1, arg2, ...]
                CMakeList result;
                result.append(program_path);
                for (size_t i = 1; i < parsed_args.size(); ++i) {
                    result.append(parsed_args[i]);
                }
                interp.set_variable(var_name, result.to_string());
            } else {
                // Return [absolute_path, remaining_args_as_string]
                CMakeList result;
                result.append(program_path);

                if (parsed_args.size() > 1) {
                    std::string remaining;
                    for (size_t i = 1; i < parsed_args.size(); ++i) {
                        if (i > 1) remaining += " ";
                        remaining += parsed_args[i];
                    }
                    result.append(remaining);
                }

                interp.set_variable(var_name, result.to_string());
            }
        } else {
            // No PROGRAM option, just return parsed args as list
            CMakeList result;
            for (const auto& arg : parsed_args) {
                result.append(arg);
            }
            interp.set_variable(var_name, result.to_string());
        }
    });
}

} // namespace dmake
