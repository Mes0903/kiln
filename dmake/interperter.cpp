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

// --- CMakeList Method Implementations ---

std::vector<std::string> CMakeList::split_by_semicolon(const std::string& str) {
    if (str.empty()) {
        return {}; // Empty string = empty list
    }

    std::vector<std::string> result;
    std::string current;

    for (char c : str) {
        if (c == ';') {
            result.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }

    // Add the last element
    result.push_back(current);

    return result;
}

CMakeList::CMakeList(const std::string& semicolon_separated)
    : items_(split_by_semicolon(semicolon_separated)) {
}

CMakeList::CMakeList(const std::vector<std::string>& items)
    : items_(items) {
}

CMakeList::CMakeList(std::initializer_list<std::string> items)
    : items_(items) {
}

CMakeList CMakeList::from_arguments(const std::vector<Argument>& args, Interpreter* interp) {
    std::vector<std::string> items;
    items.reserve(args.size());
    for (const auto& arg : args) {
        items.push_back(interp->evaluate_argument(arg));
    }
    return CMakeList(items);
}

std::string CMakeList::to_string() const {
    if (items_.empty()) {
        return "";
    }

    std::ostringstream oss;
    for (size_t i = 0; i < items_.size(); ++i) {
        oss << items_[i];
        if (i < items_.size() - 1) {
            oss << ";";
        }
    }
    return oss.str();
}

std::vector<std::string> CMakeList::to_vector() const {
    return items_;
}

void CMakeList::append(const std::string& item) {
    items_.push_back(item);
}

void CMakeList::append(const CMakeList& other) {
    items_.insert(items_.end(), other.items_.begin(), other.items_.end());
}

void CMakeList::reverse() {
    std::reverse(items_.begin(), items_.end());
}

void CMakeList::sort() {
    std::sort(items_.begin(), items_.end());
}

void CMakeList::remove_duplicates() {
    // Preserve order while removing duplicates
    std::vector<std::string> unique_items;
    std::vector<std::string> seen;

    for (const auto& item : items_) {
        if (std::find(seen.begin(), seen.end(), item) == seen.end()) {
            unique_items.push_back(item);
            seen.push_back(item);
        }
    }

    items_ = std::move(unique_items);
}

CMakeList CMakeList::sublist(size_t begin_idx, size_t length) const {
    if (begin_idx >= items_.size()) {
        return CMakeList();
    }

    size_t end_idx = std::min(begin_idx + length, items_.size());
    std::vector<std::string> subvec(items_.begin() + begin_idx, items_.begin() + end_idx);
    return CMakeList(subvec);
}

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

            // Build the value as a list if multiple arguments provided
            std::vector<Argument> value_args(args.begin() + 1, args.end());
            CMakeList value_list = CMakeList::from_arguments(value_args, this);
            std::string value = value_list.to_string();

            set_variable(var_name, value);
        });

        add_builtin("list", [this](const std::vector<Argument>& args) {
            if (args.size() < 2) {
                print_message("ERROR", "list() requires at least 2 arguments", true);
                return;
            }

            std::string operation = evaluate_argument(args[0]);
            std::transform(operation.begin(), operation.end(), operation.begin(), ::toupper);

            if (operation == "LENGTH") {
                // list(LENGTH <list_var> <output_var>)
                if (args.size() != 3) {
                    print_message("ERROR", "list(LENGTH) requires exactly 3 arguments", true);
                    return;
                }
                std::string list_var = evaluate_argument(args[1]);
                std::string output_var = evaluate_argument(args[2]);

                CMakeList list(get_variable(list_var));
                set_variable(output_var, std::to_string(list.size()));
            }
            else if (operation == "GET") {
                // list(GET <list_var> <index> [<index> ...] <output_var>)
                if (args.size() < 4) {
                    print_message("ERROR", "list(GET) requires at least 4 arguments", true);
                    return;
                }
                std::string list_var = evaluate_argument(args[1]);
                CMakeList list(get_variable(list_var));

                CMakeList result_list;
                for (size_t i = 2; i < args.size() - 1; ++i) {
                    std::string index_str = evaluate_argument(args[i]);
                    size_t index = std::stoul(index_str);
                    if (index < list.size()) {
                        result_list.append(list[index]);
                    }
                }

                std::string output_var = evaluate_argument(args[args.size() - 1]);
                set_variable(output_var, result_list.to_string());
            }
            else if (operation == "APPEND") {
                // list(APPEND <list_var> [<element> ...])
                if (args.size() < 2) {
                    print_message("ERROR", "list(APPEND) requires at least 2 arguments", true);
                    return;
                }
                std::string list_var = evaluate_argument(args[1]);
                CMakeList list(get_variable(list_var));

                for (size_t i = 2; i < args.size(); ++i) {
                    list.append(evaluate_argument(args[i]));
                }

                set_variable(list_var, list.to_string());
            }
            else if (operation == "REVERSE") {
                // list(REVERSE <list_var>)
                if (args.size() != 2) {
                    print_message("ERROR", "list(REVERSE) requires exactly 2 arguments", true);
                    return;
                }
                std::string list_var = evaluate_argument(args[1]);
                CMakeList list(get_variable(list_var));
                list.reverse();
                set_variable(list_var, list.to_string());
            }
            else if (operation == "SORT") {
                // list(SORT <list_var>)
                if (args.size() != 2) {
                    print_message("ERROR", "list(SORT) requires exactly 2 arguments", true);
                    return;
                }
                std::string list_var = evaluate_argument(args[1]);
                CMakeList list(get_variable(list_var));
                list.sort();
                set_variable(list_var, list.to_string());
            }
            else if (operation == "REMOVE_DUPLICATES") {
                // list(REMOVE_DUPLICATES <list_var>)
                if (args.size() != 2) {
                    print_message("ERROR", "list(REMOVE_DUPLICATES) requires exactly 2 arguments", true);
                    return;
                }
                std::string list_var = evaluate_argument(args[1]);
                CMakeList list(get_variable(list_var));
                list.remove_duplicates();
                set_variable(list_var, list.to_string());
            }
            else if (operation == "SUBLIST") {
                // list(SUBLIST <list_var> <begin> <length> <output_var>)
                if (args.size() != 5) {
                    print_message("ERROR", "list(SUBLIST) requires exactly 5 arguments", true);
                    return;
                }
                std::string list_var = evaluate_argument(args[1]);
                size_t begin_idx = std::stoul(evaluate_argument(args[2]));
                size_t length = std::stoul(evaluate_argument(args[3]));
                std::string output_var = evaluate_argument(args[4]);

                CMakeList list(get_variable(list_var));
                CMakeList sublist = list.sublist(begin_idx, length);
                set_variable(output_var, sublist.to_string());
            }
            else {
                print_message("ERROR", "list() unknown operation: " + operation, true);
            }
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

        add_builtin("break", [this](const std::vector<Argument>&) {
            if (loop_depth_ == 0) {
                set_fatal_error("break() can only be called inside a loop");
                return;
            }
            set_loop_control(LoopControl::BREAK);
        });

        add_builtin("continue", [this](const std::vector<Argument>&) {
            if (loop_depth_ == 0) {
                set_fatal_error("continue() can only be called inside a loop");
                return;
            }
            set_loop_control(LoopControl::CONTINUE);
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
        } else if (std::holds_alternative<FunctionBlock>(node)) {
            auto result = execute_function_block(std::get<FunctionBlock>(node));
            if (!result) {
                return result;
            }
        } else if (std::holds_alternative<MacroBlock>(node)) {
            auto result = execute_macro_block(std::get<MacroBlock>(node));
            if (!result) {
                return result;
            }
        } else if (std::holds_alternative<ForeachBlock>(node)) {
            auto result = execute_foreach_block(std::get<ForeachBlock>(node));
            if (!result) {
                return result;
            }
        }

        // Check for loop control after each statement
        // If break() or continue() was called, stop executing further statements
        auto control = get_loop_control();
        if (control == LoopControl::BREAK || control == LoopControl::CONTINUE) {
            return {};  // Exit interpret() early, preserving the control flag
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

    // Check for builtins
    auto it = builtins_.find(cmd.identifier);
    if (it != builtins_.end()) {
        it->second(cmd.arguments);
        if (auto error = get_fatal_error()) {
            clear_fatal_error();
            return std::unexpected(*error);
        }
        return {};
    }

    // Check for user-defined functions
    auto func_it = user_functions_.find(cmd.identifier);
    if (func_it != user_functions_.end()) {
        return invoke_user_function(cmd.identifier, cmd.arguments);
    }

    // Check for user-defined macros
    auto macro_it = user_macros_.find(cmd.identifier);
    if (macro_it != user_macros_.end()) {
        return invoke_user_macro(cmd.identifier, cmd.arguments);
    }

    return std::unexpected(InterpreterError{current_file_, cmd.row, cmd.col, "Unknown command: " + cmd.identifier});
}

std::expected<void, InterpreterError> Interpreter::execute_if_block(const IfBlock& if_block) {
    if (evaluate_condition(if_block.condition)) {
        return interpret(if_block.then_branch);
    } else {
        return interpret(if_block.else_branch);
    }
}

std::expected<void, InterpreterError> Interpreter::execute_function_block(const FunctionBlock& function_block) {
    // Register the function definition (store in root interpreter)
    if (parent_) {
        // Delegate to parent
        UserFunction func{function_block.parameters, function_block.body};
        parent_->user_functions_[function_block.name] = func;
    } else {
        UserFunction func{function_block.parameters, function_block.body};
        user_functions_[function_block.name] = func;
    }
    return {};
}

std::expected<void, InterpreterError> Interpreter::execute_macro_block(const MacroBlock& macro_block) {
    // Register the macro definition (store in root interpreter)
    if (parent_) {
        // Delegate to parent
        UserMacro macro{macro_block.parameters, macro_block.body};
        parent_->user_macros_[macro_block.name] = macro;
    } else {
        UserMacro macro{macro_block.parameters, macro_block.body};
        user_macros_[macro_block.name] = macro;
    }
    return {};
}

std::expected<void, InterpreterError> Interpreter::execute_foreach_block(const ForeachBlock& foreach_block) {
    // Increment loop depth
    if (parent_) {
        parent_->loop_depth_++;
    } else {
        loop_depth_++;
    }

    // Build list of items to iterate over based on the foreach mode
    CMakeList items_to_iterate;

    if (std::holds_alternative<ForeachSimple>(foreach_block.params)) {
        // Simple mode: iterate over literal items
        const auto& simple = std::get<ForeachSimple>(foreach_block.params);
        items_to_iterate = CMakeList::from_arguments(simple.items, this);

    } else if (std::holds_alternative<ForeachRange>(foreach_block.params)) {
        // RANGE mode: iterate over numeric range
        const auto& range = std::get<ForeachRange>(foreach_block.params);

        // Evaluate range bounds
        long start = 0;
        if (range.start.has_value()) {
            std::string start_str = evaluate_argument(range.start.value());
            try {
                start = std::stol(start_str);
            } catch (...) {
                // Decrement loop depth before returning error
                if (parent_) {
                    parent_->loop_depth_--;
                } else {
                    loop_depth_--;
                }
                return std::unexpected(InterpreterError{
                    current_file_, foreach_block.row, foreach_block.col,
                    "foreach(RANGE) start value must be an integer: " + start_str
                });
            }
        }

        std::string stop_str = evaluate_argument(range.stop);
        long stop;
        try {
            stop = std::stol(stop_str);
        } catch (...) {
            // Decrement loop depth before returning error
            if (parent_) {
                parent_->loop_depth_--;
            } else {
                loop_depth_--;
            }
            return std::unexpected(InterpreterError{
                current_file_, foreach_block.row, foreach_block.col,
                "foreach(RANGE) stop value must be an integer: " + stop_str
            });
        }

        long step = 1;
        if (range.step.has_value()) {
            std::string step_str = evaluate_argument(range.step.value());
            try {
                step = std::stol(step_str);
            } catch (...) {
                // Decrement loop depth before returning error
                if (parent_) {
                    parent_->loop_depth_--;
                } else {
                    loop_depth_--;
                }
                return std::unexpected(InterpreterError{
                    current_file_, foreach_block.row, foreach_block.col,
                    "foreach(RANGE) step value must be an integer: " + step_str
                });
            }
        }

        // Validate step
        if (step == 0) {
            // Decrement loop depth before returning error
            if (parent_) {
                parent_->loop_depth_--;
            } else {
                loop_depth_--;
            }
            return std::unexpected(InterpreterError{
                current_file_, foreach_block.row, foreach_block.col,
                "foreach(RANGE) step cannot be zero"
            });
        }

        // Generate range values (inclusive on both ends, like CMake)
        if ((step > 0 && start <= stop) || (step < 0 && start >= stop)) {
            for (long i = start; (step > 0) ? (i <= stop) : (i >= stop); i += step) {
                items_to_iterate.append(std::to_string(i));
            }
        }
        // else: empty range (start > stop with positive step, or start < stop with negative step)

    } else if (std::holds_alternative<ForeachIn>(foreach_block.params)) {
        // IN mode: iterate over lists and/or items
        const auto& in_params = std::get<ForeachIn>(foreach_block.params);

        // Expand LISTS (variable names whose values are lists)
        for (const auto& list_arg : in_params.lists) {
            std::string list_var_name = evaluate_argument(list_arg);
            std::string list_value = get_variable(list_var_name);
            CMakeList list(list_value);
            items_to_iterate.append(list);
        }

        // Add ITEMS (literal values)
        CMakeList literal_items = CMakeList::from_arguments(in_params.items, this);
        items_to_iterate.append(literal_items);
    }

    // Execute the loop body for each item
    for (const auto& item : items_to_iterate) {
        set_variable(foreach_block.loop_var, item);
        auto result = interpret(foreach_block.body);
        if (!result) {
            // Decrement loop depth before returning error
            if (parent_) {
                parent_->loop_depth_--;
            } else {
                loop_depth_--;
            }
            return result;
        }

        // Check for loop control signals
        auto control = get_loop_control();
        if (control == LoopControl::BREAK) {
            clear_loop_control();
            break;  // Exit the loop
        } else if (control == LoopControl::CONTINUE) {
            clear_loop_control();
            continue;  // Skip to next iteration
        }
    }

    // Decrement loop depth
    if (parent_) {
        parent_->loop_depth_--;
    } else {
        loop_depth_--;
    }

    return {};
}

std::expected<void, InterpreterError> Interpreter::invoke_user_function(const std::string& name, const std::vector<Argument>& args) {
    // Functions create a NEW scope (new call frame)
    // Variables set inside the function do NOT affect the parent scope

    const UserFunction* func = nullptr;
    if (parent_) {
        // Look up in parent
        auto it = parent_->user_functions_.find(name);
        if (it != parent_->user_functions_.end()) {
            func = &it->second;
        }
    } else {
        auto it = user_functions_.find(name);
        if (it != user_functions_.end()) {
            func = &it->second;
        }
    }

    if (!func) {
        return std::unexpected(InterpreterError{current_file_, current_cmd_row_, current_cmd_col_,
            "Unknown function: " + name});
    }

    // Create a new call frame for the function
    CallFrame new_frame{call_stack_.top().script_dir, {}};

    // Build list of all evaluated arguments
    CMakeList all_args = CMakeList::from_arguments(args, this);

    // Set ARGC
    new_frame.variables["ARGC"] = std::to_string(all_args.size());

    // Set ARGV (all arguments as semicolon-separated list)
    new_frame.variables["ARGV"] = all_args.to_string();

    // Set ARGN (arguments beyond the declared parameters)
    CMakeList extra_args = all_args.sublist(func->parameters.size(), all_args.size());
    new_frame.variables["ARGN"] = extra_args.to_string();

    // Set individual ARGV0, ARGV1, etc.
    for (size_t i = 0; i < all_args.size(); ++i) {
        new_frame.variables["ARGV" + std::to_string(i)] = all_args[i];
    }

    // Set named parameters
    for (size_t i = 0; i < func->parameters.size() && i < all_args.size(); ++i) {
        new_frame.variables[func->parameters[i]] = all_args[i];
    }

    // Push the new frame and execute the function body
    call_stack_.push(new_frame);
    auto result = interpret(func->body);
    call_stack_.pop();

    return result;
}

std::expected<void, InterpreterError> Interpreter::invoke_user_macro(const std::string& name, const std::vector<Argument>& args) {
    // Macros do NOT create a new scope
    // Variables set inside the macro affect the current scope

    const UserMacro* macro = nullptr;
    if (parent_) {
        // Look up in parent
        auto it = parent_->user_macros_.find(name);
        if (it != parent_->user_macros_.end()) {
            macro = &it->second;
        }
    } else {
        auto it = user_macros_.find(name);
        if (it != user_macros_.end()) {
            macro = &it->second;
        }
    }

    if (!macro) {
        return std::unexpected(InterpreterError{current_file_, current_cmd_row_, current_cmd_col_,
            "Unknown macro: " + name});
    }

    // Save current variables that we'll override
    std::map<std::string, std::string> saved_vars;
    auto save_if_exists = [&](const std::string& var_name) {
        if (!call_stack_.empty()) {
            auto it = call_stack_.top().variables.find(var_name);
            if (it != call_stack_.top().variables.end()) {
                saved_vars[var_name] = it->second;
            }
        }
    };

    // Build list of all evaluated arguments
    CMakeList all_args = CMakeList::from_arguments(args, this);

    save_if_exists("ARGC");
    save_if_exists("ARGV");
    save_if_exists("ARGN");
    for (size_t i = 0; i < all_args.size(); ++i) {
        save_if_exists("ARGV" + std::to_string(i));
    }
    for (const auto& param : macro->parameters) {
        save_if_exists(param);
    }

    // Set ARGC
    set_variable("ARGC", std::to_string(all_args.size()));

    // Set ARGV (all arguments as semicolon-separated list)
    set_variable("ARGV", all_args.to_string());

    // Set ARGN (arguments beyond the declared parameters)
    CMakeList extra_args = all_args.sublist(macro->parameters.size(), all_args.size());
    set_variable("ARGN", extra_args.to_string());

    // Set individual ARGV0, ARGV1, etc.
    for (size_t i = 0; i < all_args.size(); ++i) {
        set_variable("ARGV" + std::to_string(i), all_args[i]);
    }

    // Set named parameters
    for (size_t i = 0; i < macro->parameters.size() && i < all_args.size(); ++i) {
        set_variable(macro->parameters[i], all_args[i]);
    }

    // Execute the macro body in the current scope
    auto result = interpret(macro->body);

    // Restore saved variables (but NOT other variables that may have been set in the macro)
    for (const auto& [var_name, var_value] : saved_vars) {
        set_variable(var_name, var_value);
    }

    // Remove ARGC, ARGV, etc. if they weren't saved
    auto remove_if_not_saved = [&](const std::string& var_name) {
        if (saved_vars.find(var_name) == saved_vars.end() && !call_stack_.empty()) {
            call_stack_.top().variables.erase(var_name);
        }
    };

    remove_if_not_saved("ARGC");
    remove_if_not_saved("ARGV");
    remove_if_not_saved("ARGN");
    for (size_t i = 0; i < args.size(); ++i) {
        remove_if_not_saved("ARGV" + std::to_string(i));
    }
    for (const auto& param : macro->parameters) {
        remove_if_not_saved(param);
    }

    return result;
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
    // Search through all frames in the call stack (from top to bottom)
    if (!call_stack_.empty()) {
        std::stack<CallFrame> temp_stack = call_stack_;
        while (!temp_stack.empty()) {
            const auto& frame_vars = temp_stack.top().variables;
            auto it = frame_vars.find(var_name);
            if (it != frame_vars.end()) {
                return it->second;
            }
            temp_stack.pop();
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

void Interpreter::set_loop_control(LoopControl control) {
    if (parent_) {
        parent_->set_loop_control(control);
    } else {
        loop_control_ = control;
    }
}

Interpreter::LoopControl Interpreter::get_loop_control() const {
    if (parent_) {
        return parent_->get_loop_control();
    }
    return loop_control_;
}

void Interpreter::clear_loop_control() {
    if (parent_) {
        parent_->clear_loop_control();
    } else {
        loop_control_ = LoopControl::NONE;
    }
}

int Interpreter::get_loop_depth() const {
    if (parent_) {
        return parent_->get_loop_depth();
    }
    return loop_depth_;
}

} // namespace dmake
