#include "interperter.hpp"
#include <stack>

#include <iostream>
#include <sstream>
#include <unistd.h>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace dmake {

// --- Target Method Implementations ---

void Target::add_sources(const std::vector<std::string>& sources, PropertyVisibility visibility) {
    sources_[visibility].insert(sources_[visibility].end(), sources.begin(), sources.end());
}

const std::vector<std::string>& Target::get_sources(PropertyVisibility visibility) const {
    static const std::vector<std::string> empty;
    auto it = sources_.find(visibility);
    return (it != sources_.end()) ? it->second : empty;
}

void Target::add_linked_libraries(const std::vector<std::string>& libs, PropertyVisibility visibility) {
    linked_libraries_[visibility].insert(linked_libraries_[visibility].end(), libs.begin(), libs.end());
}

const std::vector<std::string>& Target::get_linked_libraries(PropertyVisibility visibility) const {
    static const std::vector<std::string> empty;
    auto it = linked_libraries_.find(visibility);
    return (it != linked_libraries_.end()) ? it->second : empty;
}

void Target::add_include_directories(const std::vector<std::string>& dirs, PropertyVisibility visibility) {
    include_directories_[visibility].insert(include_directories_[visibility].end(), dirs.begin(), dirs.end());
}

const std::vector<std::string>& Target::get_include_directories(PropertyVisibility visibility) const {
    static const std::vector<std::string> empty;
    auto it = include_directories_.find(visibility);
    return (it != include_directories_.end()) ? it->second : empty;
}

void Target::set_output_name(std::string output_name) {
    output_name_ = std::move(output_name);
}

const std::string& Target::get_output_name() const {
    return output_name_.empty() ? name_ : output_name_;
}

// --- ExecutableTarget Method Implementations ---

std::string ExecutableTarget::get_build_command(const std::string& build_dir, const std::string& script_dir) const {
    std::ostringstream cmd;
    std::filesystem::path output_path = std::filesystem::path(script_dir) / build_dir / get_output_name();
    cmd << "g++ -std=c++23 -o " << output_path.string();

    // Add include directories
    for (const auto& dir : get_include_directories(PropertyVisibility::PRIVATE)) {
        std::filesystem::path include_path = std::filesystem::path(script_dir) / dir;
        cmd << " -I" << include_path.string();
    }
     for (const auto& dir : get_include_directories(PropertyVisibility::PUBLIC)) {
        std::filesystem::path include_path = std::filesystem::path(script_dir) / dir;
        cmd << " -I" << include_path.string();
    }

    // Add sources
    for (const auto& source : get_sources(PropertyVisibility::PRIVATE)) {
        std::filesystem::path source_path = std::filesystem::path(script_dir) / source;
        cmd << " " << source_path.string();
    }

    // Add linker search paths and libraries
    std::filesystem::path build_path = std::filesystem::path(script_dir) / build_dir;
    cmd << " -L" << build_path.string();
    for (const auto& lib : get_linked_libraries(PropertyVisibility::PRIVATE)) {
        cmd << " -l" << lib;
    }
    return cmd.str();
}

// --- LibraryTarget Method Implementations ---

std::string LibraryTarget::get_build_command(const std::string& build_dir, const std::string& script_dir) const {
    std::ostringstream cmd;
    std::string lib_name = "lib" + get_output_name() + ".so";
    std::filesystem::path output_path = std::filesystem::path(script_dir) / build_dir / lib_name;
    cmd << "g++ -std=c++23 -shared -fPIC -o " << output_path.string();

    // Add include directories
    for (const auto& dir : get_include_directories(PropertyVisibility::PRIVATE)) {
        cmd << " -I" << (std::filesystem::path(script_dir) / dir).string();
    }
    for (const auto& dir : get_include_directories(PropertyVisibility::PUBLIC)) {
        cmd << " -I" << (std::filesystem::path(script_dir) / dir).string();
    }

    // Add sources
    for (const auto& source : get_sources(PropertyVisibility::PRIVATE)) {
        cmd << " " << (std::filesystem::path(script_dir) / source).string();
    }

    return cmd.str();
}

// --- Interpreter Method Implementations ---

Interpreter::Interpreter(std::string script_dir, std::ostream* out, std::ostream* err, Interpreter* parent)
    : build_dir_("build"), out_(out), err_(err), parent_(parent) {
    call_stack_.push({std::move(script_dir), {}});
    call_stack_.top().variables["CMAKE_CURRENT_SOURCE_DIR"] = call_stack_.top().script_dir;

    if (parent_ == nullptr) {
        add_builtin("message", [this](const std::vector<Argument>& args) {
            if (args.empty()) {
                return;
            }

            size_t arg_idx = 0;
            std::string mode = "INFO"; // Default mode

            if (!args[0].quoted) {
                std::string first_arg = evaluate_argument(args[0]);
                std::transform(first_arg.begin(), first_arg.end(), first_arg.begin(), ::toupper);

                if (first_arg == "STATUS") {
                    mode = "STATUS";
                    arg_idx = 1;
                } else if (first_arg == "WARNING") {
                    mode = "WARN";
                    arg_idx = 1;
                } else if (first_arg == "FATAL_ERROR") {
                    mode = "FATAL";
                    arg_idx = 1;
                } else if (first_arg == "ERROR") {
                    mode = "ERROR";
                    arg_idx = 1;
                }
            }

            std::ostringstream oss;
            for (size_t i = arg_idx; i < args.size(); ++i) {
                oss << evaluate_argument(args[i]);
                if (i < args.size() - 1) {
                    oss << " ";
                }
            }
            std::string message_content = oss.str();

            if (mode == "STATUS") {
                print_message("STATUS", message_content, false);
            } else if (mode == "WARN") {
                print_message("WARN", message_content, true);
            } else if (mode == "ERROR") {
                print_message("ERROR", message_content, true);
            } else if (mode == "FATAL") {
                set_fatal_error(message_content);
            } else { // INFO
                print_message("INFO", message_content, false);
            }
        });

        add_builtin("set", [this](const std::vector<Argument>& args) {
            if (args.size() < 2) {
                print_message("ERROR", "set() requires at least 2 arguments", true);
                return;
            }
            std::string var_name = evaluate_argument(args[0]);
            std::string value;
            for (size_t i = 1; i < args.size(); ++i) {
                value += evaluate_argument(args[i]);
            }
            set_variable(var_name, value);
        });

        add_builtin("add_executable", [this](const std::vector<Argument>& args) {
            if (args.empty()) {
                print_message("ERROR", "add_executable() requires a target name", true);
                return;
            }
            std::string name = evaluate_argument(args[0]);
            auto target = std::make_shared<ExecutableTarget>(name);
            std::vector<std::string> sources;
            for(size_t i = 1; i < args.size(); ++i) {
                sources.push_back(evaluate_argument(args[i]));
            }
            target->add_sources(sources, PropertyVisibility::PRIVATE);
            targets_[name] = target;
        });

        add_builtin("add_library", [this](const std::vector<Argument>& args) {
             if (args.size() < 2) {
                print_message("ERROR", "add_library() requires a name and sources", true);
                return;
            }
            std::string name = evaluate_argument(args[0]);
            std::vector<std::string> sources;
            bool is_shared = false;
            for(size_t i = 1; i < args.size(); ++i) {
                std::string arg_val = evaluate_argument(args[i]);
                if(arg_val == "SHARED") {
                    is_shared = true;
                } else if (arg_val != "STATIC") { // Ignore STATIC
                    sources.push_back(arg_val);
                }
            }

            TargetType type = is_shared ? TargetType::SHARED_LIBRARY : TargetType::STATIC_LIBRARY;
            auto target = std::make_shared<LibraryTarget>(name, type);
            target->add_sources(sources, PropertyVisibility::PRIVATE);
            targets_[name] = target;
        });

        add_builtin("target_include_directories", [this](const std::vector<Argument>& args) {
            if (args.size() < 2) {
                print_message("ERROR", "target_include_directories() requires a target and directories", true);
                return;
            }
            std::string target_name = evaluate_argument(args[0]);
            auto it = targets_.find(target_name);
            if (it == targets_.end()) {
                print_message("ERROR", "target_include_directories() given unknown target: " + target_name, true);
                return;
            }

            PropertyVisibility visibility = PropertyVisibility::PRIVATE;
            std::vector<std::string> dirs;
            for(size_t i = 1; i < args.size(); ++i) {
                 std::string arg_val = evaluate_argument(args[i]);
                if (arg_val == "PUBLIC") {
                    visibility = PropertyVisibility::PUBLIC;
                } else if (arg_val == "PRIVATE") {
                    visibility = PropertyVisibility::PRIVATE;
                } else if (arg_val == "INTERFACE") {
                    visibility = PropertyVisibility::INTERFACE;
                } else {
                    dirs.push_back(arg_val);
                }
            }
            it->second->add_include_directories(dirs, visibility);
        });

        add_builtin("target_link_libraries", [this](const std::vector<Argument>& args) {
            if (args.size() < 2) {
                print_message("ERROR", "target_link_libraries() requires a target and libraries", true);
                return;
            }
            std::string target_name = evaluate_argument(args[0]);
            auto it = targets_.find(target_name);
            if (it == targets_.end()) {
                print_message("ERROR", "target_link_libraries() given unknown target: " + target_name, true);
                return;
            }

            PropertyVisibility visibility = PropertyVisibility::PRIVATE;
            std::vector<std::string> libs;
            for(size_t i = 1; i < args.size(); ++i) {
                std::string arg_val = evaluate_argument(args[i]);
                if (arg_val == "PUBLIC") {
                    visibility = PropertyVisibility::PUBLIC;
                } else if (arg_val == "PRIVATE") {
                    visibility = PropertyVisibility::PRIVATE;
                } else if (arg_val == "INTERFACE") {
                    visibility = PropertyVisibility::INTERFACE;
                } else {
                    libs.push_back(arg_val);
                }
            }
            it->second->add_linked_libraries(libs, visibility);
        });

        add_builtin("set_target_properties", [this](const std::vector<Argument>& args) {
            if (args.size() < 4) {
                 print_message("ERROR", "set_target_properties() has incorrect format", true);
                 return;
            }
            std::string target_name = evaluate_argument(args[0]);
            auto it = targets_.find(target_name);
            if (it == targets_.end()) {
                print_message("ERROR", "set_target_properties() given unknown target: " + target_name, true);
                return;
            }

            if(evaluate_argument(args[1]) != "PROPERTIES") {
                print_message("ERROR", "set_target_properties() expected PROPERTIES keyword", true);
                return;
            }

            for(size_t i = 2; i < args.size() - 1; i+=2) {
                std::string key = evaluate_argument(args[i]);
                std::string value = evaluate_argument(args[i+1]);
                if (key == "OUTPUT_NAME") {
                    it->second->set_output_name(value);
                }
            }
        });

        add_builtin("cmake_minimum_required", [this](const std::vector<Argument>&) {
            print_message("INFO", "Ignoring cmake_minimum_required command.", false);
        });

        add_builtin("project", [this](const std::vector<Argument>&) {
            print_message("INFO", "Ignoring project command.", false);
        });

        add_builtin("add_subdirectory", [this](const std::vector<Argument>& args) {
            if (args.empty()) {
                print_message("ERROR", "add_subdirectory() requires a directory argument", true);
                return;
            }
            std::string subdir_name = evaluate_argument(args[0]);
            std::filesystem::path subdir_path = std::filesystem::path(call_stack_.top().script_dir) / subdir_name;
            std::filesystem::path cmake_lists_path = subdir_path / "CMakeLists.txt";

            if (!std::filesystem::exists(cmake_lists_path)) {
                set_fatal_error("CMakeLists.txt not found in " + subdir_path.string());
                return;
            }

            std::string content;
            std::ifstream file(cmake_lists_path);
            if(file) {
                std::ostringstream ss;
                ss << file.rdbuf();
                content = ss.str();
            } else {
                set_fatal_error("Could not read " + cmake_lists_path.string());
                return;
            }

            Interpreter sub_interpreter(subdir_path.string(), out_, err_, this);
            sub_interpreter.set_current_file(cmake_lists_path.string());
            Parser sub_parser(content);
            auto ast_or_error = sub_parser.parse();
            if (ast_or_error) {
                auto result = sub_interpreter.interpret(ast_or_error.value());
                if (!result) {
                    // Propagate the error - it already has the correct file/line info
                    set_fatal_error(result.error());
                }
            } else {
                const auto& parse_error = ast_or_error.error();
                set_fatal_error(InterpreterError{cmake_lists_path.string(), parse_error.row, parse_error.col, parse_error.reason});
            }
        });

        add_builtin("include", [this](const std::vector<Argument>& args) {
            if (args.empty()) {
                set_fatal_error("include() requires a file argument");
                return;
            }

            std::string file_arg = evaluate_argument(args[0]);

            // Check for OPTIONAL keyword
            bool optional = false;
            for (size_t i = 1; i < args.size(); ++i) {
                std::string arg_val = evaluate_argument(args[i]);
                if (arg_val == "OPTIONAL") {
                    optional = true;
                }
            }

            // Resolve file path (absolute or relative to current source dir)
            std::filesystem::path include_path;
            if (std::filesystem::path(file_arg).is_absolute()) {
                include_path = file_arg;
            } else {
                include_path = std::filesystem::path(call_stack_.top().script_dir) / file_arg;
            }

            // Add .cmake extension if no extension present
            if (!include_path.has_extension()) {
                include_path.replace_extension(".cmake");
            }

            if (!std::filesystem::exists(include_path)) {
                if (optional) {
                    return; // Silently ignore missing optional files
                }
                set_fatal_error("include() could not find file: " + include_path.string());
                return;
            }

            // Read the file
            std::string content;
            std::ifstream file(include_path);
            if (file) {
                std::ostringstream ss;
                ss << file.rdbuf();
                content = ss.str();
            } else {
                set_fatal_error("include() could not read file: " + include_path.string());
                return;
            }

            // Parse the file
            Parser parser(content);
            auto ast_or_error = parser.parse();
            if (!ast_or_error) {
                const auto& parse_error = ast_or_error.error();
                set_fatal_error(InterpreterError{include_path.string(), parse_error.row, parse_error.col, parse_error.reason});
                return;
            }

            // Execute in current scope - save and restore current file
            std::string saved_file = current_file_;
            current_file_ = include_path.string();

            auto result = interpret(ast_or_error.value());

            current_file_ = saved_file;

            if (!result) {
                set_fatal_error(result.error());
            }
        });
    }
}

std::expected<void, InterpreterError> Interpreter::interpret(const std::vector<AstNode>& ast) {
    for (const auto& node : ast) {
        if (std::holds_alternative<CommandInvocation>(node)) {
            auto result = execute_command(std::get<CommandInvocation>(node));
            if (!result) {
                return result;
            }
        } else if (std::holds_alternative<IfBlock>(node)) {
            auto result = execute_if_block(std::get<IfBlock>(node));
            if (!result) {
                return result;
            }
        }
    }
    return {};
}

void Interpreter::set_fatal_error(const std::string& message) {
    if (parent_) {
        parent_->set_fatal_error(InterpreterError{current_file_, current_cmd_row_, current_cmd_col_, message});
    } else {
        fatal_error_ = InterpreterError{current_file_, current_cmd_row_, current_cmd_col_, message};
    }
}

void Interpreter::set_fatal_error(const InterpreterError& error) {
    if (parent_) {
        parent_->set_fatal_error(error);
    } else {
        fatal_error_ = error;
    }
}

std::optional<InterpreterError> Interpreter::get_fatal_error() const {
    if (parent_) {
        return parent_->get_fatal_error();
    }
    return fatal_error_;
}

void Interpreter::clear_fatal_error() {
    if (parent_) {
        parent_->clear_fatal_error();
    } else {
        fatal_error_ = std::nullopt;
    }
}

std::expected<void, InterpreterError> Interpreter::run_build() {
    print_message("STATUS", "Starting build...", false);

    for (const auto& [name, target] : targets_) {
        if(target->get_type() != TargetType::EXECUTABLE) {
             std::string command = target->get_build_command(build_dir_, call_stack_.top().script_dir);
            print_message("INFO", "Building target: " + name, false);
            print_message("INFO", "Command: " + command, false);
            int result = system(command.c_str());
            if (result != 0) {
                print_message("ERROR", "Failed to build target: " + name, true);
            } else {
                print_message("INFO", "Successfully built target: " + name, false);
            }
        }
    }
     for (const auto& [name, target] : targets_) {
        if(target->get_type() == TargetType::EXECUTABLE) {
             std::string command = target->get_build_command(build_dir_, call_stack_.top().script_dir);
            print_message("INFO", "Building target: " + name, false);
            print_message("INFO", "Command: " + command, false);
            int result = system(command.c_str());
            if (result != 0) {
                print_message("ERROR", "Failed to build target: " + name, true);
            } else {
                print_message("INFO", "Successfully built target: " + name, false);
            }
        }
    }

    print_message("STATUS", "Build finished.", false);
    return {};
}

void Interpreter::add_builtin(const std::string& name, BuiltinFunction func) {
    if (parent_) {
        parent_->add_builtin(name, func);
    } else {
        builtins_[name] = func;
    }
}

std::expected<void, InterpreterError> Interpreter::execute_command(const CommandInvocation& cmd) {
    if (parent_) {
        return parent_->execute_command(cmd);
    }

    current_cmd_row_ = cmd.row;
    current_cmd_col_ = cmd.col;

    auto it = builtins_.find(cmd.identifier);
    if (it != builtins_.end()) {
        it->second(cmd.arguments);
        if (auto error = get_fatal_error()) {
            clear_fatal_error();
            return std::unexpected(*error);
        }
    } else {
        return std::unexpected(InterpreterError{current_file_, cmd.row, cmd.col, "Unknown command: " + cmd.identifier});
    }
    return {};
}

std::expected<void, InterpreterError> Interpreter::execute_if_block(const IfBlock& if_block) {
    if (evaluate_condition(if_block.condition)) {
        return interpret(if_block.then_branch);
    } else {
        return interpret(if_block.else_branch);
    }
}

bool Interpreter::evaluate_condition(const std::vector<Argument>& condition) {
    if (condition.empty()) {
        return false;
    }

    const auto& arg = condition[0];
    std::string value;

    if (!arg.quoted && arg.parts.size() == 1 && std::holds_alternative<std::string>(arg.parts[0])) {
        // This is a simple unquoted argument, like `MY_VAR` in `if(MY_VAR)`.
        // It could be a variable name, or a literal.
        const std::string& potential_var = std::get<std::string>(arg.parts[0]);

        bool is_var = false;
        if (!call_stack_.empty()) {
            const auto& current_frame_vars = call_stack_.top().variables;
            if(current_frame_vars.count(potential_var)) is_var = true;
        }

        if (is_var) {
            value = get_variable(potential_var);
        } else {
            value = potential_var;
        }

    } else {
        // Quoted argument or contains ${...}, so we just evaluate it.
        value = evaluate_argument(arg);
    }

    std::transform(value.begin(), value.end(), value.begin(), ::toupper);

    if (value == "FALSE" || value == "OFF" || value == "0" || value.empty()) {
        return false;
    }

    return true;
}

std::string Interpreter::evaluate_argument(const Argument& arg) {
    std::string result;
    for (const auto& part : arg.parts) {
        if (std::holds_alternative<std::string>(part)) {
            result += std::get<std::string>(part);
        } else if (std::holds_alternative<VariableReference>(part)) {
            const auto& var_ref = std::get<VariableReference>(part);
            result += get_variable(var_ref.name);
        }
    }
    return result;
}

std::string Interpreter::get_variable(const std::string& var_name) const {
    if (!call_stack_.empty()) {
        const auto& current_frame_vars = call_stack_.top().variables;
        auto it = current_frame_vars.find(var_name);
        if (it != current_frame_vars.end()) {
            return it->second;
        }
    }
    if (parent_) {
        return parent_->get_variable(var_name);
    }
    return "";
}

void Interpreter::set_variable(const std::string& var_name, const std::string& value) {
    if (!call_stack_.empty()) {
        call_stack_.top().variables[var_name] = value;
    }
}

void Interpreter::print_message(const std::string& mode, const std::string& message, bool is_error) {
    std::ostream& os = is_error ? *err_ : *out_;
    bool use_color = isatty(is_error ? STDERR_FILENO : STDOUT_FILENO);

    std::string prefix;
    std::string color = colors::RESET;

    if (mode == "STATUS") {
        prefix = "[STATUS]";
        color = colors::CYAN;
    } else if (mode == "INFO") {
        prefix = "[INFO]";
        color = colors::RESET;
    } else if (mode == "WARN") {
        prefix = "[WARN]";
        color = colors::YELLOW;
    } else if (mode == "ERROR") {
        prefix = "[ERROR]";
        color = colors::RED;
    } else if (mode == "FATAL") {
        prefix = "[FATAL]";
        color = colors::MAGENTA;
    }

    if (use_color) {
        os << color << prefix << " " << message << colors::RESET << std::endl;
    } else {
        os << prefix << " " << message << std::endl;
    }
}

} // namespace dmake
