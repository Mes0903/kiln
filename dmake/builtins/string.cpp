#include "registry.hpp"
#include "../interperter.hpp"
#include "../command_parser.hpp"
#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>
#include <iomanip>
#include <random>
#include <ctime>
#include <chrono>

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

            parser.add_positional(input, "input string");
            parser.add_positional(substring, "substring");
            parser.add_positional(out_var, "output variable");
            parser.add_flag("REVERSE", reverse);

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

            parser.add_positional(match_str, "match string");
            parser.add_positional(replace_str, "replace string");
            parser.add_positional(out_var, "output variable");
            parser.add_default_list(inputs);

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

                parser.add_positional(pattern, "regex pattern");
                parser.add_positional(out_var, "output variable");
                parser.add_default_list(inputs);

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

                parser.add_positional(pattern, "regex pattern");
                parser.add_positional(out_var, "output variable");
                parser.add_default_list(inputs);

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

                parser.add_positional(pattern, "regex pattern");
                parser.add_positional(replacement, "replacement expression");
                parser.add_positional(out_var, "output variable");
                parser.add_default_list(inputs);

                PARSE_OR_RETURN(parser, interp, regex_args);

                // Concatenate all inputs
                std::string input;
                for (const auto& s : inputs) {
                    input += s;
                }

                try {
                    std::regex re(pattern);
                    std::string result = std::regex_replace(input, re, replacement);
                    interp.set_variable(out_var, result);
                } catch (const std::regex_error& e) {
                    interp.set_fatal_error("string(REGEX REPLACE) invalid regex: " + std::string(e.what()));
                    return;
                }

            } else if (regex_op == "QUOTE") {
                CommandParser parser("string", "REGEX QUOTE");
                std::string out_var;
                std::vector<std::string> inputs;

                parser.add_positional(out_var, "output variable");
                parser.add_default_list(inputs);

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

            parser.add_positional(var_name, "variable name");
            parser.add_default_list(inputs);

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

            parser.add_positional(var_name, "variable name");
            parser.add_default_list(inputs);

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

            parser.add_positional(out_var, "output variable");
            parser.add_default_list(inputs);

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

            parser.add_positional(glue, "glue string");
            parser.add_positional(out_var, "output variable");
            parser.add_default_list(inputs);

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

            parser.add_positional(input, "input string");
            parser.add_positional(out_var, "output variable");

            PARSE_OR_RETURN(parser, interp, sub_args);

            interp.set_variable(out_var, to_lower(input));

        } else if (operation == "TOUPPER") {
            CommandParser parser("string", "TOUPPER");
            std::string input, out_var;

            parser.add_positional(input, "input string");
            parser.add_positional(out_var, "output variable");

            PARSE_OR_RETURN(parser, interp, sub_args);

            interp.set_variable(out_var, to_upper(input));

        } else if (operation == "LENGTH") {
            CommandParser parser("string", "LENGTH");
            std::string input, out_var;

            parser.add_positional(input, "input string");
            parser.add_positional(out_var, "output variable");

            PARSE_OR_RETURN(parser, interp, sub_args);

            interp.set_variable(out_var, std::to_string(input.size()));

        } else if (operation == "SUBSTRING") {
            CommandParser parser("string", "SUBSTRING");
            std::string input, begin_str, length_str, out_var;

            parser.add_positional(input, "input string");
            parser.add_positional(begin_str, "begin index");
            parser.add_positional(length_str, "length");
            parser.add_positional(out_var, "output variable");

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

            parser.add_positional(input, "input string");
            parser.add_positional(out_var, "output variable");

            PARSE_OR_RETURN(parser, interp, sub_args);

            interp.set_variable(out_var, strip(input));

        } else if (operation == "GENEX_STRIP") {
            CommandParser parser("string", "GENEX_STRIP");
            std::string input, out_var;

            parser.add_positional(input, "input string");
            parser.add_positional(out_var, "output variable");

            PARSE_OR_RETURN(parser, interp, sub_args);

            interp.set_variable(out_var, genex_strip(input));

        } else if (operation == "REPEAT") {
            CommandParser parser("string", "REPEAT");
            std::string input, count_str, out_var;

            parser.add_positional(input, "input string");
            parser.add_positional(count_str, "repeat count");
            parser.add_positional(out_var, "output variable");

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

            parser.add_positional(str1, "string1");
            parser.add_positional(str2, "string2");
            parser.add_positional(out_var, "output variable");

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

            parser.add_default_list(codes);

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

            parser.add_positional(input, "input string");
            parser.add_positional(out_var, "output variable");

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

            parser.add_positional(input, "input string");
            parser.add_positional(out_var, "output variable");
            parser.add_flag("@ONLY", at_only);
            parser.add_flag("ESCAPE_QUOTES", escape_quotes);

            PARSE_OR_RETURN(parser, interp, sub_args);

            std::string result = configure_string(input, interp, at_only, escape_quotes);
            interp.set_variable(out_var, result);

        } else if (operation == "MAKE_C_IDENTIFIER") {
            CommandParser parser("string", "MAKE_C_IDENTIFIER");
            std::string input, out_var;

            parser.add_positional(input, "input string");
            parser.add_positional(out_var, "output variable");

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

            parser.add_positional(out_var, "output variable");
            parser.add_default_list(remaining_args); // For format string
            parser.add_flag("UTC", utc);

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
            // UUID requires hashing (MD5/SHA1) which user wants to skip
            interp.set_fatal_error("string(UUID) is not implemented (requires hashing support)");
            return;

        } else {
            interp.set_fatal_error("string() unknown operation: " + operation);
            return;
        }
    });
}

} // namespace dmake
