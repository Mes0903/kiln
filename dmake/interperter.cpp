#include "interperter.hpp"
#include "command_parser.hpp"
#include "target.hpp"
#include "build_system.hpp"
#include "gnu_compiler.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <array>
#include <regex>
#include "builtins/registry.hpp"

namespace dmake {

Interpreter* Interpreter::get_root() {
    Interpreter* current = this;
    while (current->parent_ != nullptr) current = current->parent_;
    return current;
}

const Interpreter* Interpreter::get_root() const {
    const Interpreter* current = this;
    while (current->parent_ != nullptr) current = current->parent_;
    return current;
}


std::expected<dmake::Interpreter*, dmake::BuildError> dmake::Interpreter::run_build(int jobs, const std::vector<std::string>& requested_targets) {
    // Sanity check CMAKE_BUILD_TYPE
    std::array<std::string, 4> stanard_build_types_lower = {"debug", "release", "minsize", "relwithdebinfo"};
    auto build_type = get_variable("CMAKE_BUILD_TYPE");
    std::transform(build_type.begin(), build_type.end(), build_type.begin(), ::tolower);
    if (std::find(stanard_build_types_lower.begin(), stanard_build_types_lower.end(), build_type) == stanard_build_types_lower.end()) {
        print_message("WARN", "Build type '" + build_type + "' is not a standard build type. Things MIGHT go wrong.");
    }

    if (parent_ != nullptr) return get_root()->run_build(jobs, requested_targets);

    // Determine which targets to build
    std::set<std::string> targets_to_build;
    if (requested_targets.empty()) {
        bool any_all = false;
        for (const auto& [name, target] : targets_) {
            auto custom = std::dynamic_pointer_cast<CustomTarget>(target);
            if (custom && custom->is_build_by_default()) {
                any_all = true;
                break;
            }
        }

        for (const auto& [name, target] : targets_) {
            if (any_all) {
                auto custom = std::dynamic_pointer_cast<CustomTarget>(target);
                if (custom && custom->is_build_by_default()) {
                    targets_to_build.insert(name);
                }
                // Executables and libraries are usually "ALL" in CMake by default unless EXCLUDE_FROM_ALL is set
                // In dmake we currently treat them as ALL.
                if (target->get_type() != TargetType::CUSTOM) {
                    targets_to_build.insert(name);
                }
            } else {
                targets_to_build.insert(name);
            }
        }
    } else {
        std::function<void(const std::string&)> collect = [&](const std::string& name) {
            if (targets_to_build.count(name)) return;
            if (!targets_.count(name)) {
                // Not a dmake target, might be a system lib or imported target
                return;
            }
            targets_to_build.insert(name);
            auto target = targets_[name];
            for (const auto& lib : target->get_property_list("LINK_LIBRARIES", PropertyVisibility::PRIVATE)) collect(lib);
            for (const auto& lib : target->get_property_list("LINK_LIBRARIES", PropertyVisibility::PUBLIC)) collect(lib);
            for (const auto& lib : target->get_property_list("LINK_LIBRARIES", PropertyVisibility::INTERFACE)) collect(lib);

            auto custom = std::dynamic_pointer_cast<CustomTarget>(target);
            if (custom) {
                for (const auto& dep : custom->get_custom_dependencies()) {
                    collect(dep);
                }
            }
        };
        for (const auto& t : requested_targets) {
            if (!targets_.count(t)) {
                return std::unexpected(BuildError{current_file_, "Unknown target: " + t});
            }
            collect(t);
        }
    }

    std::string root_binary_dir = get_variable("CMAKE_BINARY_DIR");
    std::filesystem::create_directories(root_binary_dir);

    print_message("STATUS", "Generating build graph...");
    BuildGraph graph;

    // 1. (Redundant property propagation removed - handled by Target::resolve)

    // 2. Generate tasks for selected targets
    // Build linker flags from CMAKE variables
    std::vector<std::string> exe_linker_flags;
    std::vector<std::string> shared_linker_flags;

    // Handle CMAKE_LINKER_TYPE (convert to -fuse-ld=<type>)
    std::string linker_type = get_variable("CMAKE_LINKER_TYPE");
    if (!linker_type.empty()) {
        std::string linker_type_upper = linker_type;
        std::transform(linker_type_upper.begin(), linker_type_upper.end(), linker_type_upper.begin(), ::toupper);

        if (linker_type_upper == "BFD" || linker_type_upper == "GOLD" ||
            linker_type_upper == "MOLD" || linker_type_upper == "LLD") {
            std::string linker_type_lower = linker_type;
            std::transform(linker_type_lower.begin(), linker_type_lower.end(), linker_type_lower.begin(), ::tolower);
            std::string flag = "-fuse-ld=" + linker_type_lower;
            exe_linker_flags.push_back(flag);
            shared_linker_flags.push_back(flag);
        } else {
            return std::unexpected(BuildError{current_file_, "Invalid CMAKE_LINKER_TYPE: " + linker_type + ". Must be one of: BFD, GOLD, MOLD, LLD"});
        }
    }

    // Handle CMAKE_EXE_LINKER_FLAGS
    CMakeList exe_flags_list(get_variable("CMAKE_EXE_LINKER_FLAGS"));
    for (const auto& flag : exe_flags_list) {
        if (!flag.empty()) exe_linker_flags.push_back(flag);
    }

    // Handle CMAKE_SHARED_LINKER_FLAGS
    CMakeList shared_flags_list(get_variable("CMAKE_SHARED_LINKER_FLAGS"));
    for (const auto& flag : shared_flags_list) {
        if (!flag.empty()) shared_linker_flags.push_back(flag);
    }

    for (const auto& name : targets_to_build) {
        targets_[name]->generate_tasks(graph, get_root()->toolchain_, targets_, exe_linker_flags, shared_linker_flags);
    }

    // Link dependency resolution (adding inputs to link tasks)
    for (const auto& name : targets_to_build) {
        auto target = targets_[name];
        std::string out_path = target->get_output_path();

        // For custom targets, out_path might be empty or same as name
        std::string task_id = out_path.empty() ? target->get_name() : out_path;

        if (graph.has_task(task_id)) {
            auto& task = graph.get_task(task_id);

            auto add_lib_inputs = [&](PropertyVisibility vis) {
                for (const auto& lib_name : target->get_property_list("LINK_LIBRARIES", vis)) {
                    if (targets_.count(lib_name)) {
                        auto dep = targets_[lib_name];
                        std::string lib_out = dep->get_output_path();
                        if (!lib_out.empty()) {
                            task.inputs.push_back(lib_out);
                        } else {
                            // Dependency on a utility target
                            task.dependencies.insert(lib_name);
                        }
                    }
                }
            };
            add_lib_inputs(PropertyVisibility::PRIVATE);
            add_lib_inputs(PropertyVisibility::PUBLIC);

            // If it's a standard target (linking), we need to add libraries to the command line
            if (target->get_type() != TargetType::CUSTOM && !task.commands.empty()) {
                 // The link command already has these from Target::generate_tasks
                 // but we need them as inputs for the graph.
            }
        }
    }

    // 3. Generate compile_commands.json (on by default)
    std::string export_cmds = get_variable("CMAKE_EXPORT_COMPILE_COMMANDS");
    if (!is_falsy(export_cmds)) {
        auto result = graph.generate_compile_commands(root_binary_dir);
        if (!result) {
            return std::unexpected(BuildError{current_file_, result.error()});
        }
    }

    // 4. Execute the build graph
    print_message("STATUS", "Starting " + build_type + " build...");
    auto result = graph.execute(root_binary_dir, jobs);


    if (!result) {
        return std::unexpected(BuildError{current_file_, result.error()});
    }

    print_message("STATUS", "Build finished.");
    return this;
}

Interpreter::Interpreter(std::string script_dir, std::ostream* out, std::ostream* err, Interpreter* parent, std::optional<std::string> build_dir)
    : out_(out), err_(err), parent_(parent) {

    std::filesystem::path abs_script_dir = script_dir.empty() ?
        std::filesystem::current_path() :
        std::filesystem::absolute(script_dir).lexically_normal();
    call_stack_.push_front({abs_script_dir.string(), {}});

    auto& vars = call_stack_.front().variables;
    vars["CMAKE_CURRENT_SOURCE_DIR"] = abs_script_dir.string();
    vars["CMAKE_CURRENT_LIST_DIR"] = abs_script_dir.string();
    vars["CMAKE_CURRENT_LIST_FILE"] = (abs_script_dir / "CMakeLists.txt").string(); // Default assumption

    if (parent_ == nullptr) {
        std::filesystem::path abs_binary_dir;
        if (build_dir.has_value()) {
            build_dir_ = *build_dir;
            abs_binary_dir = std::filesystem::absolute(build_dir_).lexically_normal();
        } else {
            build_dir_ = "build";
            abs_binary_dir = abs_script_dir / build_dir_;
        }

        vars["DMAKE_VERSION"] = "0.1.0";

        vars["CMAKE_SOURCE_DIR"] = vars["CMAKE_CURRENT_SOURCE_DIR"];
        vars["CMAKE_BINARY_DIR"] = abs_binary_dir.string();
        vars["CMAKE_CURRENT_BINARY_DIR"] = abs_binary_dir.string();
        vars["CMAKE_EXPORT_COMPILE_COMMANDS"] = "ON";

        if (abs_binary_dir == abs_script_dir) {
            set_fatal_error("Build directory cannot be the same as the source directory: " + abs_script_dir.string());
        }

        vars["CMAKE_VERSION"] = "3.20.0";
        vars["CMAKE_MAJOR_VERSION"] = "3";
        vars["CMAKE_MINOR_VERSION"] = "20";
        vars["CMAKE_PATCH_VERSION"] = "0";

        vars["CMAKE_FILES_DIRECTORY"] = "/CMakeFiles";

        // Platform flags
#ifdef __unix__
        vars["UNIX"] = "1";
#endif
#ifdef __APPLE__
        vars["APPLE"] = "1";
#endif
#ifdef _WIN32
        vars["WIN32"] = "1";
#endif

        vars["CMAKE_EXECUTABLE_SUFFIX"] = "";
        vars["CMAKE_SHARED_LIBRARY_SUFFIX"] = ".so";
        vars["CMAKE_STATIC_LIBRARY_SUFFIX"] = ".a";

        vars["CMAKE_COMMAND"] = get_executable_path();
        vars["CMAKE_ROOT"] = "/usr/share/cmake";

        // Initialize default toolchain
        toolchain_.set_compiler(Language::CXX, std::make_unique<GnuCompiler>("g++", Language::CXX));
        toolchain_.set_compiler(Language::C, std::make_unique<GnuCompiler>("gcc", Language::C));

        // Register non-internal builtins
        register_message_builtins(*this);
        register_variable_builtins(*this);
        register_list_builtins(*this);
        register_target_builtins(*this);
        register_project_builtins(*this);
        register_file_builtins(*this);
        register_find_commands_builtins(*this);
        register_math_builtins(*this);
        register_string_builtins(*this);
        register_process_builtins(*this);

        add_builtin("enable_testing", [](Interpreter& interp, const std::vector<std::string>& args) {
            if (!args.empty()) {
                interp.print_message("WARN", "enable_testing() expects no arguments");
            }
            interp.enable_testing_globally();
        });

        add_builtin("add_test", [](Interpreter& interp, const std::vector<std::string>& args) {
            if (!interp.is_testing_enabled()) {
                return;
            }

            // Warn if BUILD_TESTING is OFF but add_test is being called
            auto build_testing = interp.get_variable("BUILD_TESTING");
            if (Interpreter::is_falsy(build_testing)) {
                interp.print_message("WARN", "add_test() called but BUILD_TESTING is OFF. Tests may not have been built. Use -DBUILD_TESTING=ON");
            }

            CommandParser parser("add_test");
            std::string name;
            std::string command;
            std::vector<std::string> cmd_args;
            std::string working_dir;
            std::vector<std::string> raw_cmd;

            parser.add_value("NAME", name);
            parser.add_list("COMMAND", raw_cmd);
            parser.add_value("WORKING_DIRECTORY", working_dir);

            auto parse_res = parser.parse(args);
            if (!parse_res) {
                interp.set_fatal_error(parse_res.error());
                return;
            }

            if (name.empty() || raw_cmd.empty()) {
                interp.set_fatal_error("add_test requires NAME and COMMAND");
                return;
            }

            TestDefinition test;
            test.name = name;
            test.command = raw_cmd[0];
            for (size_t i = 1; i < raw_cmd.size(); ++i) {
                test.args.push_back(raw_cmd[i]);
            }
            test.working_dir = working_dir.empty() ? interp.get_variable("CMAKE_CURRENT_SOURCE_DIR") : working_dir;

            interp.get_tests().push_back(std::move(test));
        });
        register_find_package_builtins(*this);

        add_builtin("include_guard", [](Interpreter& interp, const std::vector<std::string>& args) {
            std::string scope = "DIRECTORY";
            if (!args.empty()) scope = args[0];

            std::string current_file = interp.get_current_file();
            if (current_file.empty()) return;

            if (scope == "GLOBAL") {
                interp.get_root()->global_guarded_files_.insert(current_file);
            } else {
                interp.directory_guarded_files_.insert(current_file);
            }
        });

        // Internal builtins (interact with interpreter state/stack)

        add_builtin("add_subdirectory", [](Interpreter& interp, const std::vector<std::string>& args) {
            if (args.empty()) return;
            std::string subdir = args[0];
            std::filesystem::path path = std::filesystem::path(interp.get_variable("CMAKE_CURRENT_SOURCE_DIR")) / subdir;
            std::filesystem::path cmake_file = path / "CMakeLists.txt";

            if (!std::filesystem::exists(cmake_file)) {
                interp.set_fatal_error("CMakeLists.txt not found in " + path.string());
                return;
            }

            std::ifstream file(cmake_file);
            if(!file) { interp.set_fatal_error("Could not read " + cmake_file.string()); return; }
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

            Interpreter sub_interp(path.string(), interp.out_, interp.err_, &interp);
            sub_interp.set_current_file(cmake_file.string());
            sub_interp.set_variable("CMAKE_CURRENT_LIST_FILE", cmake_file.string());
            sub_interp.set_variable("CMAKE_CURRENT_LIST_DIR", path.string());

            Parser parser(content);
            auto ast = parser.parse();
            if (ast) {
                auto res = sub_interp.interpret(ast.value());
                if (!res) interp.set_fatal_error(res.error());
            } else {
                interp.set_fatal_error(InterpreterError{cmake_file.string(), ast.error().row, ast.error().col, ast.error().offset, ast.error().length, ast.error().reason, {}});
            }
        });

        add_builtin("include", [](Interpreter& interp, const std::vector<std::string>& args) {
            if (args.empty()) { interp.set_fatal_error("include() requires an argument"); return; }
            bool optional = false;
            for (size_t i = 1; i < args.size(); ++i) if (args[i] == "OPTIONAL") optional = true;

            auto res = interp.include_file(args[0], optional);
            if (!res) interp.set_fatal_error(res.error());
        });


        add_builtin("break", [](Interpreter& interp, const std::vector<std::string>&) {
            if (interp.get_loop_depth() == 0) {
                interp.set_fatal_error("break() can only be called inside a loop");
                return;
            }
            interp.set_loop_control(Interpreter::LoopControl::BREAK);
        });

        add_builtin("continue", [](Interpreter& interp, const std::vector<std::string>&) {
            if (interp.get_loop_depth() == 0) {
                interp.set_fatal_error("continue() can only be called inside a loop");
                return;
            }
            interp.set_loop_control(Interpreter::LoopControl::CONTINUE);
        });

        add_builtin("return", [](Interpreter& interp, const std::vector<std::string>& args) {
            // CMake's return() doesn't accept arguments, but we silently ignore them for compatibility
            (void)args;
            interp.request_return();
        });

        add_builtin("cmake_language", [](Interpreter& interp, const std::vector<std::string>& args) {
            if (args.empty()) {
                interp.set_fatal_error("cmake_language requires arguments");
                return;
            }

            if (args[0] == "EVAL") {
                 if (args.size() < 3 || args[1] != "CODE") {
                     interp.set_fatal_error("cmake_language(EVAL) requires CODE keyword and code arguments");
                     return;
                 }
                 std::string code;
                 for (size_t i = 2; i < args.size(); ++i) {
                     code += args[i];
                 }

                 Parser p(code);
                 auto ast = p.parse();
                 if (!ast) {
                     std::vector<CallLocation> backtrace;
                     Interpreter* root = interp.get_root();
                     if (!root->trace_stack_.empty()) {
                         // For parse errors during EVAL, the current command (cmake_language)
                         // should be part of the backtrace.
                         backtrace = root->trace_stack_;
                     }
                     InterpreterError err{"<EVAL>", ast.error().row, ast.error().col, ast.error().offset, ast.error().length, "cmake_language(EVAL) parse error: " + ast.error().reason, backtrace, code};
                     interp.set_fatal_error(err);
                     return;
                 }

                 std::string old_file = interp.get_current_file();
                 interp.set_current_file("<EVAL>");
                 auto res = interp.interpret(ast.value());
                 interp.set_current_file(old_file);

                 if (!res) {
                     InterpreterError err = res.error();
                     if (err.file == "<EVAL>" && !err.source_content) {
                         err.source_content = code;
                     }
                     interp.set_fatal_error(err);
                 }

            } else if (args[0] == "CALL") {
                 if (args.size() < 2) {
                     interp.set_fatal_error("cmake_language(CALL) requires a command name");
                     return;
                 }
                 std::string cmd = args[1];
                 std::vector<std::string> cmd_args;
                 if (args.size() > 2) {
                     cmd_args.assign(args.begin() + 2, args.end());
                 }

                 auto res = interp.execute_command_with_args(cmd, cmd_args);
                 if (!res) interp.set_fatal_error(res.error());
            } else {
                interp.set_fatal_error("Unknown cmake_language mode: " + args[0]);
            }
        });
    } else {
        vars["CMAKE_SOURCE_DIR"] = parent_->get_variable("CMAKE_SOURCE_DIR");
        vars["CMAKE_BINARY_DIR"] = parent_->get_variable("CMAKE_BINARY_DIR");
        build_dir_ = parent_->build_dir_;

        // Calculate CMAKE_CURRENT_BINARY_DIR based on relative path from source root
        std::filesystem::path rel_path = std::filesystem::relative(abs_script_dir, parent_->get_variable("CMAKE_SOURCE_DIR"));
        vars["CMAKE_CURRENT_BINARY_DIR"] = (std::filesystem::path(vars["CMAKE_BINARY_DIR"]) / rel_path).lexically_normal().string();
    }
}

std::expected<void, InterpreterError> Interpreter::interpret(const std::vector<AstNode>& ast) {
    for (const auto& node : ast) {
        std::expected<void, InterpreterError> res;
        if (std::holds_alternative<CommandInvocation>(node)) res = execute_command(std::get<CommandInvocation>(node));
        else if (std::holds_alternative<IfBlock>(node)) res = execute_if_block(std::get<IfBlock>(node));
        else if (std::holds_alternative<FunctionBlock>(node)) res = execute_function_block(std::get<FunctionBlock>(node));
        else if (std::holds_alternative<MacroBlock>(node)) res = execute_macro_block(std::get<MacroBlock>(node));
        else if (std::holds_alternative<ForeachBlock>(node)) res = execute_foreach_block(std::get<ForeachBlock>(node));

        if (!res) return res;
        if (loop_control_ != LoopControl::NONE) return {};
        if (return_requested_) return {};  // Early return from script/function
    }
    return {};
}

std::expected<void, InterpreterError> Interpreter::include_file(const std::string& file_path, bool optional) {
    std::filesystem::path path = std::filesystem::path(file_path);

    // Helper to check if a path exists, trying with and without .cmake extension
    auto try_path = [](const std::filesystem::path& p) -> std::optional<std::filesystem::path> {
        if (std::filesystem::exists(p) && !std::filesystem::is_directory(p)) return p;
        if (!p.has_extension()) {
            std::filesystem::path with_ext = p;
            with_ext.replace_extension(".cmake");
            if (std::filesystem::exists(with_ext) && !std::filesystem::is_directory(with_ext)) return with_ext;
        }
        return std::nullopt;
    };

    auto find_in_dir = [&try_path](const std::filesystem::path& dir, const std::string& file_path) -> std::optional<std::filesystem::path> {
        auto candidate = dir / file_path;
        return try_path(candidate);
    };

    std::optional<std::filesystem::path> found_path;

    if (path.is_absolute()) {
        found_path = try_path(path);
    } else {
        // Try relative to CMAKE_CURRENT_SOURCE_DIR first
        std::filesystem::path candidate = std::filesystem::path(get_variable("CMAKE_CURRENT_SOURCE_DIR")) / file_path;
        found_path = try_path(candidate);

        // If not found, search CMAKE_MODULE_PATH
        if (!found_path) {
            CMakeList module_paths(get_variable("CMAKE_MODULE_PATH"));
            for (const auto& dir : module_paths) {
                if (!dir.empty()) {
                    found_path = find_in_dir(dir, file_path);
                    if (found_path) break;
                }
            }
        }

        // If not found, search system paths
        if (!found_path) {
            std::vector<std::string> system_modules = {
                "/usr/share/cmake/Modules",
                "/usr/local/share/cmake/Modules",
                "/usr/lib/cmake/Modules",
                "/usr/lib/x86_64-linux-gnu/cmake/Modules"
            };
            for (const auto& dir : system_modules) {
                found_path = find_in_dir(dir, file_path);
                if (found_path) break;
            }
        }
    }

    if (!found_path) {
        if (optional) return {};
        return std::unexpected(InterpreterError{current_file_, current_cmd_row_, current_cmd_col_, 0, 0, "include() could not find: " + file_path, {}});
    }

    path = *found_path;

    // Check if it's a directory (reject directories, but accept symlinks to files)
    if (std::filesystem::is_directory(path)) {
        if (optional) return {};
        return std::unexpected(InterpreterError{current_file_, current_cmd_row_, current_cmd_col_, 0, 0, "include() path is a directory: " + path.string(), {}});
    }

    std::string abs_path = std::filesystem::absolute(path).string();

    // Check include guards
    if (get_root()->global_guarded_files_.contains(abs_path)) return {};
    if (directory_guarded_files_.contains(abs_path)) return {};

    std::ifstream file(path);
    if (!file) {
        return std::unexpected(InterpreterError{path.string(), 0, 0, 0, 0, "include() could not read: " + path.string(), {}});
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    Parser parser(content);
    auto ast = parser.parse();
    if (!ast) {
        return std::unexpected(InterpreterError{path.string(), ast.error().row, ast.error().col, ast.error().offset, ast.error().length, ast.error().reason, {}});
    }

    std::string old_file = get_current_file();
    std::string abs_dir = std::filesystem::path(abs_path).parent_path().string();

    std::string old_list_file = get_variable("CMAKE_CURRENT_LIST_FILE");
    std::string old_list_dir = get_variable("CMAKE_CURRENT_LIST_DIR");

    // Save return state - return() in included file shouldn't affect caller
    bool saved_return = return_requested_;
    return_requested_ = false;

    set_variable("CMAKE_CURRENT_LIST_FILE", abs_path);
    set_variable("CMAKE_CURRENT_LIST_DIR", abs_dir);
    set_current_file(abs_path);

    auto res = interpret(ast.value());

    set_variable("CMAKE_CURRENT_LIST_FILE", old_list_file);
    set_variable("CMAKE_CURRENT_LIST_DIR", old_list_dir);
    set_current_file(old_file);

    // Restore return state - included file's return() doesn't propagate
    return_requested_ = saved_return;

    return res;
}

void Interpreter::set_fatal_error(const std::string& message) {
    Interpreter* root = get_root();
    std::vector<CallLocation> backtrace;
    if (!root->trace_stack_.empty()) {
        // The last element is the current command, we want everything BEFORE it as backtrace
        for (size_t i = 0; i < root->trace_stack_.size() - 1; ++i) {
            backtrace.push_back(root->trace_stack_[i]);
        }

        const auto& current = root->trace_stack_.back();
        set_fatal_error(InterpreterError{current.file, current.row, current.col, current.offset, current.length, message, backtrace});
    } else {
        set_fatal_error(InterpreterError{current_file_, current_cmd_row_, current_cmd_col_, 0, 0, message, {}});
    }
}

void Interpreter::set_fatal_error(const InterpreterError& error) {
    Interpreter* root = get_root();
    if (!root->fatal_error_) {
        root->fatal_error_ = error;
    } else {
        // Enrich existing error if possible
        if (error.source_content && !root->fatal_error_->source_content && error.file == root->fatal_error_->file) {
            root->fatal_error_->source_content = error.source_content;
        }
    }
}

std::optional<InterpreterError> Interpreter::get_fatal_error() const {
    return const_cast<Interpreter*>(this)->get_root()->fatal_error_;
}

void Interpreter::clear_fatal_error() {
    get_root()->fatal_error_ = std::nullopt;
}

void Interpreter::add_builtin(const std::string& name, BuiltinFunction func) {
    get_root()->builtins_[name] = func;
}

std::vector<std::string> Interpreter::expand_arguments(const std::vector<Argument>& args) {
    std::vector<std::string> result;
    for (const auto& arg : args) {
        std::string val = evaluate_argument(arg);
        if (arg.quoted) {
            result.push_back(val);
        } else {
            if (val.empty()) continue;
            // Split by semicolon for unquoted arguments (list expansion)
            CMakeList lst(val);
            for(const auto& item : lst) {
                result.push_back(item);
            }
        }
    }
    return result;
}

std::expected<void, InterpreterError> Interpreter::execute_command(const CommandInvocation& cmd) {
    current_cmd_row_ = cmd.row;
    current_cmd_col_ = cmd.col;

    // Push to backtrace stack
    Interpreter* root = get_root();
    root->trace_stack_.push_back({current_file_, cmd.row, cmd.col, cmd.offset, cmd.length, cmd.identifier});

    // Expand arguments once
    std::vector<std::string> expanded_args = expand_arguments(cmd.arguments);

    auto res = execute_command_with_args(cmd.identifier, expanded_args);

    if (!res) {
        if (auto err = get_fatal_error()) {
             // The error is already set, just clean up stack and return it
             safe_pop_trace_stack("error in: " + cmd.identifier);
             return std::unexpected(*err);
        }
    }

    safe_pop_trace_stack("command: " + cmd.identifier);
    return res;
}

std::expected<void, InterpreterError> Interpreter::execute_command_with_args(const std::string& identifier, const std::vector<std::string>& args) {
    Interpreter* root = get_root();
    std::string lower_identifier = identifier;
    std::transform(lower_identifier.begin(), lower_identifier.end(), lower_identifier.begin(), ::tolower);

    auto bit = root->builtins_.find(lower_identifier);
    if (bit != root->builtins_.end()) {
        bit->second(*this, args);
        if (auto err = get_fatal_error()) {
            return std::unexpected(*err);
        }
        return {};
    }

    Interpreter* curr = this;
    while (curr) {
        auto fit = curr->user_functions_.find(lower_identifier);
        if (fit != curr->user_functions_.end()) {
            return invoke_user_function(fit->second, args);
        }
        auto mit = curr->user_macros_.find(lower_identifier);
        if (mit != curr->user_macros_.end()) {
            return invoke_user_macro(mit->second, args);
        }
        curr = curr->parent_;
    }

    set_fatal_error("Unknown command: " + identifier);
    return std::unexpected(*get_fatal_error());
}

std::expected<void, InterpreterError> Interpreter::execute_if_block(const IfBlock& if_block) {
    Interpreter* root = get_root();
    root->trace_stack_.push_back({current_file_, if_block.row, if_block.col, if_block.offset, if_block.length, "if"});

    auto cond_result = evaluate_condition(if_block.condition, if_block.row, if_block.col, if_block.offset, if_block.length);
    if (!cond_result) {
        set_fatal_error(cond_result.error());
        safe_pop_trace_stack("if block condition error");
        return std::unexpected(cond_result.error());
    }

    if (cond_result.value()) {
        auto res = interpret(if_block.then_branch);
        if (!res) set_fatal_error(res.error());
        safe_pop_trace_stack("if block");
        return res;
    }

    for (const auto& elseif : if_block.elseif_branches) {
        auto elseif_cond = evaluate_condition(elseif.condition, elseif.row, elseif.col, elseif.offset, elseif.length);
        if (!elseif_cond) {
            set_fatal_error(elseif_cond.error());
            safe_pop_trace_stack("elseif block condition error");
            return std::unexpected(elseif_cond.error());
        }
        if (elseif_cond.value()) {
            auto res = interpret(elseif.body);
            if (!res) set_fatal_error(res.error());
            safe_pop_trace_stack("elseif block");
            return res;
        }
    }

    auto res = interpret(if_block.else_branch);
    if (!res) set_fatal_error(res.error());
    safe_pop_trace_stack("if block");
    return res;
}

std::expected<void, InterpreterError> Interpreter::execute_function_block(const FunctionBlock& block) {
    std::string lower_name = block.name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
    user_functions_[lower_name] = {block.parameters, block.body, current_file_};
    user_macros_.erase(block.name);
    return {};
}

std::expected<void, InterpreterError> Interpreter::execute_macro_block(const MacroBlock& block) {
    std::string lower_name = block.name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
    user_macros_[lower_name] = {block.parameters, block.body, current_file_};
    user_functions_.erase(lower_name);
    return {};
}

std::expected<void, InterpreterError> Interpreter::execute_foreach_block(const ForeachBlock& block) {
    Interpreter* root = get_root();
    root->trace_stack_.push_back({current_file_, block.row, block.col, block.offset, block.length, "foreach"});

    loop_depth_++;
    CMakeList items;
    if (std::holds_alternative<ForeachSimple>(block.params)) {
        items = from_arguments(expand_arguments(std::get<ForeachSimple>(block.params).items));
    } else if (std::holds_alternative<ForeachRange>(block.params)) {
        const auto& r = std::get<ForeachRange>(block.params);
        long start = r.start ? std::stol(evaluate_argument(*r.start)) : 0;
        long stop = std::stol(evaluate_argument(r.stop));
        long step = r.step ? std::stol(evaluate_argument(*r.step)) : 1;
        if (step == 0) {
            if (loop_depth_ <= 0) {
                std::cerr << "FATAL: loop_depth_ is " << loop_depth_ << " when handling step=0 error\n";
                std::abort();
            }
            loop_depth_--;
            set_fatal_error("Step cannot be zero");
            auto err = get_fatal_error();
            clear_fatal_error();
            safe_pop_trace_stack("foreach step=0 error");
            return std::unexpected(*err);
        }
        for (long i = start; (step > 0) ? (i <= stop) : (i >= stop); i += step) items.append(std::to_string(i));
    } else if (std::holds_alternative<ForeachIn>(block.params)) {
        const auto& in = std::get<ForeachIn>(block.params);
        for (const auto& l : in.lists) items.append(CMakeList(get_variable(evaluate_argument(l))));
        items.append(from_arguments(expand_arguments(in.items)));
    }

    for (const auto& item : items) {
        set_variable(block.loop_var, item);
        auto res = interpret(block.body);
        if (!res) {
            set_fatal_error(res.error());
            if (loop_depth_ <= 0) {
                std::cerr << "FATAL: loop_depth_ is " << loop_depth_ << " when handling foreach body error\n";
                std::abort();
            }
            loop_depth_--;
            safe_pop_trace_stack("foreach body error");
            return res;
        }
        if (loop_control_ == LoopControl::BREAK) { clear_loop_control(); break; }
        if (loop_control_ == LoopControl::CONTINUE) clear_loop_control();
        if (return_requested_) break;  // return() exits the loop and propagates to caller
    }

    // Sanity check: loop depth should never go negative
    if (loop_depth_ <= 0) {
        std::cerr << "FATAL: loop_depth_ is " << loop_depth_ << " when trying to decrement in foreach\n";
        std::abort();
    }
    loop_depth_--;
    safe_pop_trace_stack("foreach");
    return {};
}

std::expected<void, InterpreterError> Interpreter::invoke_user_function(const UserFunction& func, const std::vector<std::string>& args) {
    CallFrame frame{call_stack_.front().script_dir, {}};
    CMakeList all(args);
    frame.variables["ARGC"] = std::to_string(all.size());
    frame.variables["ARGV"] = all.to_string();
    frame.variables["ARGN"] = all.sublist(func.parameters.size(), all.size()).to_string();
    for (size_t i = 0; i < all.size(); ++i) frame.variables["ARGV" + std::to_string(i)] = all[i];
    for (size_t i = 0; i < func.parameters.size() && i < all.size(); ++i) frame.variables[func.parameters[i]] = all[i];

    int saved_depth = loop_depth_;
    LoopControl saved_control = loop_control_;
    bool saved_return = return_requested_;
    std::string saved_file = current_file_;
    loop_depth_ = 0;
    loop_control_ = LoopControl::NONE;
    return_requested_ = false;
    current_file_ = func.definition_file;

    call_stack_.push_front(frame);
    auto res = interpret(func.body);

    // Sanity check: we should have at least one frame (the one we just pushed)
    if (call_stack_.empty()) {
        std::cerr << "FATAL: call_stack_ is empty when exiting function (should have at least our frame)\n";
        std::abort();
    }
    call_stack_.pop_front();

    if (!res) set_fatal_error(res.error());

    // Functions create a new scope, so return() inside doesn't affect caller
    return_requested_ = saved_return;
    loop_depth_ = saved_depth;
    loop_control_ = saved_control;
    current_file_ = saved_file;
    return res;
}

std::expected<void, InterpreterError> Interpreter::invoke_user_macro(const UserMacro& macro, const std::vector<std::string>& args) {
    std::map<std::string, std::string> saved;
    CMakeList all(args);
    auto save = [&](const std::string& k) { if (call_stack_.front().variables.count(k)) saved[k] = call_stack_.front().variables[k]; };
    save("ARGC"); save("ARGV"); save("ARGN");
    for (size_t i = 0; i < all.size(); ++i) save("ARGV" + std::to_string(i));
    for (const auto& p : macro.parameters) save(p);

    set_variable("ARGC", std::to_string(all.size()));
    set_variable("ARGV", all.to_string());
    set_variable("ARGN", all.sublist(macro.parameters.size(), all.size()).to_string());
    for (size_t i = 0; i < all.size(); ++i) set_variable("ARGV" + std::to_string(i), all[i]);
    for (size_t i = 0; i < macro.parameters.size() && i < all.size(); ++i) set_variable(macro.parameters[i], all[i]);

    std::string saved_file = current_file_;
    current_file_ = macro.definition_file;
    auto res = interpret(macro.body);
    current_file_ = saved_file;
    if (!res) set_fatal_error(res.error());

    for (const auto& [k, v] : saved) set_variable(k, v);
    auto& vars = call_stack_.front().variables;
    if (!saved.count("ARGC")) vars.erase("ARGC");
    if (!saved.count("ARGV")) vars.erase("ARGV");
    if (!saved.count("ARGN")) vars.erase("ARGN");
    for (size_t i = 0; i < all.size(); ++i) if (!saved.count("ARGV" + std::to_string(i))) vars.erase("ARGV" + std::to_string(i));
    for (const auto& p : macro.parameters) if (!saved.count(p)) vars.erase(p);
    return res;
}

bool Interpreter::is_falsy(const std::string& val) {
    if (val.empty()) return true;

    std::string upper_val = val;
    std::transform(upper_val.begin(), upper_val.end(), upper_val.begin(), ::toupper);

    // False constants (case-insensitive)
    if (upper_val == "0" || upper_val == "OFF" || upper_val == "NO" ||
        upper_val == "FALSE" || upper_val == "N" || upper_val == "IGNORE" ||
        upper_val == "NOTFOUND" || upper_val.ends_with("-NOTFOUND")) {
        return true;
    }

    return false;
}

std::expected<bool, InterpreterError> Interpreter::evaluate_condition(const std::vector<Argument>& condition, size_t row, size_t col, size_t offset, size_t length) {
    // Set of keywords that should not be dereferenced as variables
    static const std::set<std::string> keywords = {
        "NOT", "AND", "OR", "(", ")",
        "DEFINED", "TARGET", "EXISTS", "COMMAND", "POLICY", "TEST",
        "IS_DIRECTORY", "IS_SYMLINK", "IS_ABSOLUTE",
        "EQUAL", "LESS", "GREATER", "LESS_EQUAL", "GREATER_EQUAL", "NOT_EQUAL",
        "STREQUAL", "STRLESS", "STRGREATER", "STRLESS_EQUAL", "STRGREATER_EQUAL",
        "VERSION_EQUAL", "VERSION_LESS", "VERSION_GREATER",
        "VERSION_LESS_EQUAL", "VERSION_GREATER_EQUAL",
        "MATCHES", "IN_LIST"
    };

    // Helper to get the string value of an argument token
    // For unquoted arguments that are not keywords, dereference as variable
    auto get_token_string = [&](const Argument& arg) -> std::string {
        if (!arg.quoted && arg.parts.size() == 1 && std::holds_alternative<std::string>(arg.parts[0])) {
            return std::get<std::string>(arg.parts[0]);
        }
        return evaluate_argument(arg);
    };

    // Helper to check if a string looks like a numeric constant
    auto is_numeric_constant = [](const std::string& s) -> bool {
        if (s.empty()) return false;
        size_t start = 0;
        if (s[0] == '-' || s[0] == '+') start = 1;
        if (start >= s.length()) return false;

        // Check if rest of string is numeric
        for (size_t i = start; i < s.length(); ++i) {
            if (!std::isdigit(s[i]) && s[i] != '.') return false;
        }
        return true;
    };

    // Helper to check if a variable is defined in any scope
    auto is_variable_defined = [&](const std::string& name) -> bool {
        for (const auto& frame : call_stack_) {
            if (frame.variables.contains(name)) {
                return true;
            }
        }
        return false;
    };

    // Helper to evaluate an argument, dereferencing variables unless it's a keyword or constant
    // CMake behavior (pre-CMP0054):
    // - Keywords and quoted strings are returned as-is
    // - Numeric constants are returned as-is
    // - Everything else is dereferenced as a variable:
    //   - If defined, return the variable's value (even if empty)
    //   - If undefined, return empty string
    auto evaluate_token = [&](const Argument& arg) -> std::string {
        std::string token = get_token_string(arg);

        // Don't dereference keywords (including boolean constants like TRUE, OFF, etc.) or quoted strings
        if (arg.quoted || keywords.contains(token)) {
            return token;
        }

        // Don't dereference numeric constants (0, 1, -5, 3.14, etc.)
        if (is_numeric_constant(token)) {
            return token;
        }

        // Dereference as variable
        // If defined: return value (even if empty)
        // If undefined: return empty string
        // Note: This means undefined variables evaluate to empty (falsy)
        //       but named boolean constants like TRUE, FALSE, ON, OFF, YES, NO, Y, N
        //       are treated as variable names that dereference to empty if not defined
        return get_variable(token);
    };

    // Recursive descent parser with proper CMake precedence
    // Precedence (high to low): unary tests > binary tests > NOT > AND/OR
    size_t pos = 0;
    std::string error_msg;  // Track parsing errors

    std::function<bool()> parse_or;
    std::function<bool()> parse_and;
    std::function<bool()> parse_not;
    std::function<bool()> parse_comparison;
    std::function<bool()> parse_unary_or_primary;

    // AND/OR have lowest precedence and evaluate left-to-right
    // NOTE: CMake does NOT short-circuit - both sides are always evaluated
    parse_or = [&]() -> bool {
        bool left = parse_and();

        while (pos < condition.size() && error_msg.empty()) {
            std::string token = get_token_string(condition[pos]);
            if (token == "OR") {
                pos++;
                if (pos >= condition.size()) {
                    error_msg = "OR operator requires a right operand";
                    return false;
                }
                bool right = parse_and();  // Always evaluate right side (no short-circuit)
                left = left || right;
            } else {
                break;
            }
        }
        return left;
    };

    parse_and = [&]() -> bool {
        bool left = parse_not();

        while (pos < condition.size() && error_msg.empty()) {
            std::string token = get_token_string(condition[pos]);
            if (token == "AND") {
                pos++;
                if (pos >= condition.size()) {
                    error_msg = "AND operator requires a right operand";
                    return false;
                }
                bool right = parse_not();  // Always evaluate right side (no short-circuit)
                left = left && right;
            } else {
                break;
            }
        }
        return left;
    };

    // NOT has higher precedence than AND/OR but lower than comparisons
    parse_not = [&]() -> bool {
        if (pos >= condition.size()) {
            error_msg = "Unexpected end of condition";
            return false;
        }

        std::string token = get_token_string(condition[pos]);
        if (token == "NOT") {
            pos++;
            if (pos >= condition.size()) {
                error_msg = "NOT operator requires an operand";
                return false;
            }
            return !parse_not();  // Right-associative
        }
        return parse_comparison();
    };

    // Binary comparison operators (EQUAL, LESS, STREQUAL, etc.)
    parse_comparison = [&]() -> bool {
        if (pos >= condition.size()) return false;

        // Save start position in case this isn't a comparison
        size_t start_pos = pos;

        // Try to parse left operand
        bool unary_result = parse_unary_or_primary();

        // Check if next token is a binary operator
        if (pos >= condition.size()) {
            return unary_result;
        }

        std::string op = get_token_string(condition[pos]);

        // Numeric comparisons
        if (op == "EQUAL" || op == "LESS" || op == "GREATER" ||
            op == "LESS_EQUAL" || op == "GREATER_EQUAL" || op == "NOT_EQUAL") {
            pos++;
            if (pos >= condition.size()) {
                error_msg = op + " operator requires a right operand";
                return false;
            }

            std::string left = evaluate_token(condition[start_pos]);
            std::string right = evaluate_token(condition[pos++]);

            try {
                double left_num = std::stod(left);
                double right_num = std::stod(right);

                if (op == "EQUAL") return left_num == right_num;
                if (op == "NOT_EQUAL") return left_num != right_num;
                if (op == "LESS") return left_num < right_num;
                if (op == "GREATER") return left_num > right_num;
                if (op == "LESS_EQUAL") return left_num <= right_num;
                if (op == "GREATER_EQUAL") return left_num >= right_num;
            } catch (...) {
                // Fallback to string comparison for EQUAL/NOT_EQUAL
                if (op == "EQUAL") return left == right;
                if (op == "NOT_EQUAL") return left != right;
                return false;
            }
        }
        // String comparisons
        else if (op == "STREQUAL" || op == "STRLESS" || op == "STRGREATER" ||
                 op == "STRLESS_EQUAL" || op == "STRGREATER_EQUAL") {
            pos++;
            if (pos >= condition.size()) {
                error_msg = op + " operator requires a right operand";
                return false;
            }

            std::string left = evaluate_token(condition[start_pos]);
            std::string right = evaluate_token(condition[pos++]);

            if (op == "STREQUAL") return left == right;
            if (op == "STRLESS") return left < right;
            if (op == "STRGREATER") return left > right;
            if (op == "STRLESS_EQUAL") return left <= right;
            if (op == "STRGREATER_EQUAL") return left >= right;
        }
        // Version comparisons (simplified - real CMake does component-wise comparison)
        else if (op.starts_with("VERSION_")) {
            pos++;
            if (pos >= condition.size()) {
                error_msg = op + " operator requires a right operand";
                return false;
            }

            std::string left = evaluate_token(condition[start_pos]);
            std::string right = evaluate_token(condition[pos++]);

            if (op == "VERSION_EQUAL") {
                return left == right;
            }
            // Simplified lexicographic comparison (real CMake splits on . and compares numerically)
            bool less = std::lexicographical_compare(
                left.begin(), left.end(), right.begin(), right.end());

            if (op == "VERSION_LESS") return less;
            if (op == "VERSION_GREATER") return !less && left != right;
            if (op == "VERSION_LESS_EQUAL") return less || left == right;
            if (op == "VERSION_GREATER_EQUAL") return !less || left == right;
        }
        // regex
        else if(op == "MATCHES") {
            pos++; // Consume operator
            if (pos >= condition.size()) {
                error_msg = "MATCHES operator requires a right operand";
                return false;
            }
            std::string pattern = evaluate_token(condition[pos++]);
            std::regex regex(pattern);
            std::smatch match;
            std::string left = evaluate_token(condition[start_pos]);
            bool result = std::regex_search(left, match, regex);

            if (result) {
                set_variable("CMAKE_MATCH_COUNT", std::to_string(match.size() - 1));
                for (size_t i = 0; i < match.size() && i < 10; ++i) {
                    set_variable("CMAKE_MATCH_" + std::to_string(i), match[i].str());
                }
                // Clear remaining matches if fewer than previous
                 for (size_t i = match.size(); i < 10; ++i) {
                    set_variable("CMAKE_MATCH_" + std::to_string(i), "");
                }
            } else {
                 set_variable("CMAKE_MATCH_COUNT", "0");
                 for (size_t i = 0; i < 10; ++i) {
                    set_variable("CMAKE_MATCH_" + std::to_string(i), "");
                }
            }
            return result;
        }

        // Not a comparison operator - return the unary/primary result
        return unary_result;
    };

    // Unary operators (highest precedence) and primary values
    parse_unary_or_primary = [&]() -> bool {
        if (pos >= condition.size()) return false;

        std::string token = get_token_string(condition[pos]);

        // Parentheses for grouping
        if (token == "(") {
            pos++;
            bool result = parse_or();
            if (pos >= condition.size() || get_token_string(condition[pos]) != ")") {
                error_msg = "Expected ')' to close group";
                return false;
            }
            pos++;
            return result;
        }

        // Unary operators that take one argument
        // If there's no next token, treat the keyword as a primary value instead
        if (token == "DEFINED" && pos + 1 < condition.size()) {
            pos++;
            // DEFINED takes a variable name (don't dereference it)
            std::string var_name = get_token_string(condition[pos++]);

            // Check if variable is defined in any scope
            for (const auto& frame : call_stack_) {
                if (frame.variables.contains(var_name)) {
                    return true;
                }
            }
            return false;
        } else if (token == "TARGET" && pos + 1 < condition.size()) {
            pos++;
            std::string target_name = get_token_string(condition[pos++]);
            return targets_.contains(target_name);
        } else if (token == "EXISTS" && pos + 1 < condition.size()) {
            pos++;
            std::string path = evaluate_token(condition[pos++]);
            return std::filesystem::exists(path);
        } else if (token == "IS_DIRECTORY" && pos + 1 < condition.size()) {
            pos++;
            std::string path = evaluate_token(condition[pos++]);
            return std::filesystem::is_directory(path);
        } else if (token == "IS_ABSOLUTE" && pos + 1 < condition.size()) {
            pos++;
            std::string path = evaluate_token(condition[pos++]);
            return std::filesystem::path(path).is_absolute();
        } else if (token == "IS_SYMLINK" && pos + 1 < condition.size()) {
            pos++;
            std::string path = evaluate_token(condition[pos++]);
            return std::filesystem::is_symlink(path);
        } else if (token == "COMMAND" && pos + 1 < condition.size()) {
            pos++;
            std::string name = evaluate_token(condition[pos++]);
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            return user_functions_.contains(name);
        }

        // Primary value - evaluate and check truthiness
        // For keywords that aren't being used as operators (like standalone DEFINED, TARGET, etc.),
        // we need to dereference them as variables, not treat them as keywords
        const Argument& arg = condition[pos++];
        std::string token_str = get_token_string(arg);

        // If it's quoted or a numeric constant, use it as-is
        if (arg.quoted || is_numeric_constant(token_str)) {
            return !is_falsy(token_str);
        }

        // Otherwise dereference as a variable (even if it's a keyword name)
        std::string val = get_variable(token_str);
        return !is_falsy(val);
    };

    // Start parsing at the lowest precedence level (OR)
    bool result = parse_or();

    // Check if there was an error during parsing
    if (!error_msg.empty()) {
        set_fatal_error(error_msg);
        return std::unexpected(*get_fatal_error());
    }

    // Error if we didn't consume all tokens (indicates malformed condition)
    if (pos < condition.size()) {
        std::string remaining;
        for (size_t i = pos; i < condition.size(); ++i) {
            if (!remaining.empty()) remaining += " ";
            remaining += get_token_string(condition[i]);
        }
        set_fatal_error("Unexpected tokens in if() condition: " + remaining);
        return std::unexpected(*get_fatal_error());
    }

    return result;
}

std::string Interpreter::evaluate_variable_reference(const VariableReference& ref) {
    // Step 1: Recursively evaluate name_parts to build the final variable name
    std::string name;
    for (const auto& part : ref.name_parts) {
        if (std::holds_alternative<std::string>(part)) {
            name += std::get<std::string>(part);
        } else {
            name += evaluate_variable_reference(std::get<VariableReference>(part));
        }
    }

    // Step 2: Lookup based on namespace
    if (ref.namespace_prefix.empty()) {
        // Regular variable lookup
        return get_variable(name);
    } else if (ref.namespace_prefix == "ENV") {
        const char* env_var = getenv(name.c_str());
        return env_var ? env_var : "";
    } else if (ref.namespace_prefix == "CACHE") {
        auto* root = get_root();
        auto it = root->cache_variables_.find(name);
        return (it != root->cache_variables_.end()) ? it->second : "";
    }
    return "";
}

std::string Interpreter::evaluate_argument(const Argument& arg) {
    std::string res;
    for (const auto& p : arg.parts) {
        if (std::holds_alternative<std::string>(p)) {
            res += std::get<std::string>(p);
        } else {
            res += evaluate_variable_reference(std::get<VariableReference>(p));
        }
    }
    return res;
}

std::string Interpreter::get_variable(const std::string& name) const {
    if (call_stack_.empty()) {
        std::cerr << "FATAL: get_variable('" << name << "') called with empty call_stack_\n";
        std::abort();
    }

    std::deque<CallFrame> temp = call_stack_;
    while (!temp.empty()) {
        auto it = temp.front().variables.find(name);
        if (it != temp.front().variables.end()) return it->second;
        temp.pop_front();
    }
    return parent_ ? parent_->get_variable(name) : "";
}

void Interpreter::set_variable(const std::string& name, const std::string& val) {
    if (call_stack_.empty()) {
        std::cerr << "FATAL: set_variable('" << name << "', '" << val << "') called with empty call_stack_\n";
        std::abort();
    }
    call_stack_.front().variables[name] = val;
}

void Interpreter::set_cache_variable(const std::string& var_name, const std::string& value) {
    get_root()->cache_variables_[var_name] = value;
}

bool Interpreter::unset_variable(const std::string& name) {
    if (call_stack_.empty())
        return false;
    auto it = call_stack_.begin();
    while(it != call_stack_.end()) {
        auto it2 = it->variables.find(name);
        if (it2 != it->variables.end()) {
            it->variables.erase(it2);
            return true;
        }
        it++;
    }
    return false;
}

bool Interpreter::is_variable_set(const std::string& name) const {
    if (call_stack_.empty())
        return false;
    auto it = call_stack_.begin();
    while(it != call_stack_.end()) {
        auto it2 = it->variables.find(name);
        if (it2 != it->variables.end()) {
            return true;
        }
        it++;
    }
    return false;
}

void Interpreter::print_message(const std::string& mode, const std::string& msg, bool is_err) {
    std::ostream& os = is_err ? *err_ : *out_;
    bool color = isatty(is_err ? STDERR_FILENO : STDOUT_FILENO);
    std::string p, c = colors::RESET;
    if (mode == "STATUS") { p = "[STATUS]"; c = colors::CYAN; }
    else if (mode == "INFO") { p = "[INFO]"; }
    else if (mode == "WARN") { p = "[WARN]"; c = colors::YELLOW; }
    else if (mode == "ERROR") { p = "[ERROR]"; c = colors::RED; }
    else if (mode == "FATAL") { p = "[FATAL]"; c = colors::MAGENTA; }
    if (color) os << c << p << " " << msg << colors::RESET << std::endl;
    else os << p << " " << msg << std::endl;
}

CMakeList Interpreter::from_arguments(const std::vector<std::string>& args) {
    return CMakeList(args);
}

void Interpreter::check_invariants() const {
    // Critical invariant: call stack should never be empty during execution
    if (call_stack_.empty()) {
        std::cerr << "FATAL: call_stack_ is empty (this should never happen)\n";
        std::abort();
    }

    // Loop control should only be set when in a loop
    if (loop_control_ != LoopControl::NONE && loop_depth_ <= 0) {
        std::cerr << "FATAL: loop_control_ is set (" << static_cast<int>(loop_control_)
                  << ") but loop_depth_ is " << loop_depth_ << "\n";
        std::abort();
    }

    // Loop depth should never be negative
    if (loop_depth_ < 0) {
        std::cerr << "FATAL: loop_depth_ is negative (" << loop_depth_ << ")\n";
        std::abort();
    }

    // Root interpreter should have builtins
    if (parent_ == nullptr && builtins_.empty()) {
        std::cerr << "FATAL: root interpreter has no builtins (not properly initialized?)\n";
        std::abort();
    }
}

void Interpreter::safe_pop_trace_stack(const std::string& context) {
    Interpreter* root = get_root();
    if (root->trace_stack_.empty()) {
        std::cerr << "FATAL: Attempting to pop empty trace_stack_ in context: " << context << "\n";
        std::cerr << "       This indicates a push/pop imbalance in the interpreter\n";
        std::abort();
    }
    root->trace_stack_.pop_back();
}

}
