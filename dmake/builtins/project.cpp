#include "registry.hpp"
#include "../interperter.hpp"
#include "../command_parser.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <regex>
#include <cstdlib>

namespace dmake {

namespace {

// Substitute @VAR@ patterns in content
std::string substitute_at_vars(Interpreter& interp, const std::string& content, bool escape_quotes) {
    std::string result;
    result.reserve(content.size());

    size_t i = 0;
    while (i < content.size()) {
        if (content[i] == '@') {
            size_t end = content.find('@', i + 1);
            if (end != std::string::npos) {
                std::string var_name = content.substr(i + 1, end - i - 1);
                // Variable names must be valid identifiers
                bool valid = !var_name.empty() && (std::isalpha(static_cast<unsigned char>(var_name[0])) || var_name[0] == '_');
                for (size_t j = 1; valid && j < var_name.size(); ++j) {
                    valid = std::isalnum(static_cast<unsigned char>(var_name[j])) || var_name[j] == '_';
                }
                if (valid) {
                    std::string value = interp.get_variable(var_name);
                    if (escape_quotes) {
                        for (char c : value) {
                            if (c == '"') result += "\\\"";
                            else result += c;
                        }
                    } else {
                        result += value;
                    }
                    i = end + 1;
                    continue;
                }
            }
        }
        result += content[i++];
    }

    return result;
}

// Substitute $<prefix>{<name>} patterns
// prefix can be empty (regular var), ENV, or CACHE
std::string substitute_dollar_vars(Interpreter& interp, const std::string& content, bool escape_quotes) {
    std::string result;
    result.reserve(content.size());

    size_t i = 0;
    while (i < content.size()) {
        if (content[i] == '$' && i + 1 < content.size()) {
            // Find the opening brace
            size_t brace_pos = content.find('{', i + 1);
            if (brace_pos != std::string::npos) {
                std::string prefix = content.substr(i + 1, brace_pos - i - 1);
                // Valid prefixes: empty, ENV, CACHE
                if (prefix.empty() || prefix == "ENV" || prefix == "CACHE") {
                    size_t end = content.find('}', brace_pos + 1);
                    if (end != std::string::npos) {
                        std::string var_name = content.substr(brace_pos + 1, end - brace_pos - 1);
                        std::string value;

                        if (prefix == "ENV") {
                            const char* env_val = std::getenv(var_name.c_str());
                            value = env_val ? env_val : "";
                        } else {
                            // Both empty prefix and CACHE use get_variable
                            value = interp.get_variable(var_name);
                        }

                        if (escape_quotes) {
                            for (char c : value) {
                                if (c == '"') result += "\\\"";
                                else result += c;
                            }
                        } else {
                            result += value;
                        }
                        i = end + 1;
                        continue;
                    }
                }
            }
        }
        result += content[i++];
    }

    return result;
}

// Process #cmakedefine and #cmakedefine01 directives
std::string process_cmakedefine(Interpreter& interp, const std::string& content) {
    std::istringstream stream(content);
    std::ostringstream result;
    std::string line;

    // Regex for #cmakedefine directives (supports whitespace between # and keyword)
    std::regex cmakedefine_regex(R"(^(\s*)#(\s*)cmakedefine\s+(\w+)(.*)$)");
    std::regex cmakedefine01_regex(R"(^(\s*)#(\s*)cmakedefine01\s+(\w+)\s*$)");

    bool first_line = true;
    while (std::getline(stream, line)) {
        if (!first_line) result << '\n';
        first_line = false;

        std::smatch match;

        if (std::regex_match(line, match, cmakedefine01_regex)) {
            std::string leading = match[1].str();
            std::string hash_space = match[2].str();
            std::string var_name = match[3].str();

            std::string value = interp.get_variable(var_name);
            bool is_true = !Interpreter::is_falsy(value);

            result << leading << "#" << hash_space << "define " << var_name
                   << " " << (is_true ? "1" : "0");
        }
        else if (std::regex_match(line, match, cmakedefine_regex)) {
            std::string leading = match[1].str();
            std::string hash_space = match[2].str();
            std::string var_name = match[3].str();
            std::string rest = match[4].str();

            std::string value = interp.get_variable(var_name);
            bool is_defined = !Interpreter::is_falsy(value);

            if (is_defined) {
                if (rest.empty() || rest.find_first_not_of(" \t") == std::string::npos) {
                    // No value after variable name
                    if (value.empty() || value == "ON" || value == "TRUE" ||
                        value == "YES" || value == "1") {
                        result << leading << "#" << hash_space << "define " << var_name;
                    } else {
                        result << leading << "#" << hash_space << "define " << var_name << " " << value;
                    }
                } else {
                    result << leading << "#" << hash_space << "define " << var_name << rest;
                }
            } else {
                result << "/* " << leading << "#" << hash_space << "undef " << var_name << " */";
            }
        }
        else {
            result << line;
        }
    }

    return result.str();
}

// Convert line endings
std::string convert_newlines(const std::string& content, const std::string& style) {
    std::string result;
    result.reserve(content.size());

    bool use_crlf = (style == "DOS" || style == "WIN32" || style == "CRLF");

    for (size_t i = 0; i < content.size(); ++i) {
        if (content[i] == '\r') {
            if (i + 1 < content.size() && content[i + 1] == '\n') {
                continue; // Skip \r before \n
            }
            // Standalone \r
            if (use_crlf) result += "\r\n";
            else result += '\n';
        } else if (content[i] == '\n') {
            if (use_crlf) result += "\r\n";
            else result += '\n';
        } else {
            result += content[i];
        }
    }

    return result;
}

} // anonymous namespace

void register_project_builtins(Interpreter& interp) {
    interp.add_builtin("cmake_minimum_required", [](Interpreter&, const std::vector<std::string>&) {});

    interp.add_builtin("project", [](Interpreter& interp, const std::vector<std::string>& args) {
        if (args.empty()) {
            interp.set_fatal_error("project() requires at least one argument");
            return;
        }

        interp.set_variable("PROJECT_NAME", args[0]);

        std::vector<std::string> languages;
        bool in_languages = false;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "LANGUAGES") {
                in_languages = true;
            } else if (args[i] == "VERSION" || args[i] == "DESCRIPTION" ||
                       args[i] == "HOMEPAGE_URL") {
                in_languages = false;
            } else if (in_languages) {
                languages.push_back(args[i]);
            }
        }

        if (!languages.empty()) {
            std::string enabled_langs;
            for (size_t i = 0; i < languages.size(); ++i) {
                if (i > 0) enabled_langs += ";";
                enabled_langs += languages[i];
            }
            interp.get_global_properties()["ENABLED_LANGUAGES"] = enabled_langs;
        }
    });

    interp.add_builtin("cmake_policy", [](Interpreter&, const std::vector<std::string>&) {});
    interp.add_builtin("mark_as_advanced", [](Interpreter&, const std::vector<std::string>&) {});

    interp.add_builtin("configure_file", [](Interpreter& interp, const std::vector<std::string>& args) {
        if (args.size() < 2) {
            interp.set_fatal_error("configure_file() requires input and output arguments");
            return;
        }

        // Parse arguments
        std::filesystem::path input_path = args[0];
        std::filesystem::path output_path = args[1];

        bool copyonly = false;
        bool at_only = false;
        bool escape_quotes = false;
        bool no_source_permissions = false;
        bool use_source_permissions = false;
        std::string newline_style;
        std::vector<std::string> file_permissions;

        for (size_t i = 2; i < args.size(); ++i) {
            if (args[i] == "COPYONLY") {
                copyonly = true;
            } else if (args[i] == "@ONLY") {
                at_only = true;
            } else if (args[i] == "ESCAPE_QUOTES") {
                escape_quotes = true;
            } else if (args[i] == "NO_SOURCE_PERMISSIONS") {
                no_source_permissions = true;
            } else if (args[i] == "USE_SOURCE_PERMISSIONS") {
                use_source_permissions = true;
            } else if (args[i] == "NEWLINE_STYLE") {
                if (i + 1 >= args.size()) {
                    interp.set_fatal_error("configure_file() NEWLINE_STYLE requires a value");
                    return;
                }
                newline_style = args[++i];
                if (newline_style != "UNIX" && newline_style != "DOS" &&
                    newline_style != "WIN32" && newline_style != "LF" &&
                    newline_style != "CRLF") {
                    interp.set_fatal_error("configure_file() invalid NEWLINE_STYLE: " + newline_style);
                    return;
                }
            } else if (args[i] == "FILE_PERMISSIONS") {
                ++i;
                while (i < args.size() && args[i].find("_") != std::string::npos) {
                    file_permissions.push_back(args[i++]);
                }
                --i; // Adjust for loop increment
            }
        }

        // Validate incompatible options
        if (copyonly && !newline_style.empty()) {
            interp.set_fatal_error("configure_file() COPYONLY and NEWLINE_STYLE are incompatible");
            return;
        }

        if ((no_source_permissions ? 1 : 0) + (use_source_permissions ? 1 : 0) +
            (!file_permissions.empty() ? 1 : 0) > 1) {
            interp.set_fatal_error("configure_file() permission options are mutually exclusive");
            return;
        }

        // Resolve paths
        if (!input_path.is_absolute()) {
            input_path = std::filesystem::path(interp.get_variable("CMAKE_CURRENT_SOURCE_DIR")) / input_path;
        }
        if (!output_path.is_absolute()) {
            output_path = std::filesystem::path(interp.get_variable("CMAKE_CURRENT_BINARY_DIR")) / output_path;
        }

        // Read input file
        std::ifstream infile(input_path, std::ios::binary);
        if (!infile) {
            interp.set_fatal_error("configure_file() could not open input file: " + input_path.string());
            return;
        }
        std::string content((std::istreambuf_iterator<char>(infile)), std::istreambuf_iterator<char>());
        infile.close();

        // Process content (unless COPYONLY)
        if (!copyonly) {
            // Process #cmakedefine directives first
            content = process_cmakedefine(interp, content);

            // Then substitute variables
            if (at_only) {
                content = substitute_at_vars(interp, content, escape_quotes);
            } else {
                content = substitute_at_vars(interp, content, escape_quotes);
                content = substitute_dollar_vars(interp, content, escape_quotes);
            }

            // Convert newlines if specified
            if (!newline_style.empty()) {
                content = convert_newlines(content, newline_style);
            }
        }

        // Create output directory if needed
        std::filesystem::create_directories(output_path.parent_path());

        // Write output file
        std::ofstream outfile(output_path, std::ios::binary);
        if (!outfile) {
            interp.set_fatal_error("configure_file() could not open output file: " + output_path.string());
            return;
        }
        outfile << content;
        outfile.close();

        // Handle permissions
        // Default is USE_SOURCE_PERMISSIONS (copy from input)
        if (!file_permissions.empty()) {
            namespace fs = std::filesystem;
            fs::perms perms = fs::perms::none;
            for (const auto& p : file_permissions) {
                if (p == "OWNER_READ") perms |= fs::perms::owner_read;
                else if (p == "OWNER_WRITE") perms |= fs::perms::owner_write;
                else if (p == "OWNER_EXECUTE") perms |= fs::perms::owner_exec;
                else if (p == "GROUP_READ") perms |= fs::perms::group_read;
                else if (p == "GROUP_WRITE") perms |= fs::perms::group_write;
                else if (p == "GROUP_EXECUTE") perms |= fs::perms::group_exec;
                else if (p == "WORLD_READ") perms |= fs::perms::others_read;
                else if (p == "WORLD_WRITE") perms |= fs::perms::others_write;
                else if (p == "WORLD_EXECUTE") perms |= fs::perms::others_exec;
            }
            std::filesystem::permissions(output_path, perms);
        } else if (no_source_permissions) {
            // Standard 644 permissions
            namespace fs = std::filesystem;
            fs::permissions(output_path,
                fs::perms::owner_read | fs::perms::owner_write |
                fs::perms::group_read | fs::perms::others_read);
        } else {
            // USE_SOURCE_PERMISSIONS is the default - copy from input
            auto src_perms = std::filesystem::status(input_path).permissions();
            std::filesystem::permissions(output_path, src_perms);
        }
    });
}

} // namespace dmake
