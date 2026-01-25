#include "interperter.hpp"
#include "builtins/registry.hpp"
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
        return {};
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

// --- Interpreter Method Implementations ---

Interpreter* Interpreter::get_root() {
    Interpreter* r = this;
    while (r->parent_) r = r->parent_;
    return r;
}

std::expected<void, InterpreterError> Interpreter::run_build(int jobs) {
    if (parent_ != nullptr) return get_root()->run_build(jobs);

    std::string root_dir = call_stack_.top().script_dir;
    std::filesystem::path full_build_path = std::filesystem::path(root_dir) / build_dir_;

    print_message("STATUS", "Generating build graph...");
    BuildGraph graph;

    // 1. Generate tasks for all artifacts
    for (const auto& [name, artifact] : artifacts_) {
        artifact->generate_tasks(graph, build_dir_, root_dir);
    }

    // Resolve cross-artifact library dependencies
    for (const auto& [name, artifact] : artifacts_) {
        std::string out_path = artifact->get_output_path(build_dir_, root_dir);
        if (graph.has_task(out_path)) {
            auto& link_task = graph.get_task(out_path);
            
            auto add_lib_deps = [&](PropertyVisibility vis) {
                for (const auto& lib_name : artifact->get_linked_libraries(vis)) {
                    if (artifacts_.count(lib_name)) {
                        std::string lib_out = artifacts_[lib_name]->get_output_path(build_dir_, root_dir);
                        link_task.inputs.push_back(lib_out);
                    }
                }
            };
            
            add_lib_deps(PropertyVisibility::PRIVATE);
            add_lib_deps(PropertyVisibility::PUBLIC);
        }
    }

    // 2. Execute the build graph
    print_message("STATUS", "Starting build...");
    auto result = graph.execute(full_build_path.string(), jobs);

    if (!result) {
        return std::unexpected(InterpreterError{current_file_, 0, 0, result.error()});
    }

    print_message("STATUS", "Build finished.");
    return {};
}

Interpreter::Interpreter(std::string script_dir, std::ostream* out, std::ostream* err, Interpreter* parent)
    : build_dir_("build"), out_(out), err_(err), parent_(parent) {
    call_stack_.push({std::move(script_dir), {}});

    auto& vars = call_stack_.top().variables;
    vars["CMAKE_CURRENT_SOURCE_DIR"] = call_stack_.top().script_dir;
    vars["CMAKE_CURRENT_BINARY_DIR"] = (std::filesystem::path(vars["CMAKE_CURRENT_SOURCE_DIR"]) / build_dir_).string();

    if (parent_ == nullptr) {
        // Global root variables
        vars["CMAKE_SOURCE_DIR"] = vars["CMAKE_CURRENT_SOURCE_DIR"];
        vars["CMAKE_BINARY_DIR"] = vars["CMAKE_CURRENT_BINARY_DIR"];
        vars["PROJECT_SOURCE_DIR"] = vars["CMAKE_CURRENT_SOURCE_DIR"];
        vars["PROJECT_BINARY_DIR"] = vars["CMAKE_CURRENT_BINARY_DIR"];

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

        // Register non-internal builtins
        register_message_builtins(*this);
        register_variable_builtins(*this);
        register_list_builtins(*this);
        register_target_builtins(*this);
        register_project_builtins(*this);

        // Internal builtins (interact with interpreter state/stack)
        add_builtin("add_subdirectory", [](Interpreter& interp, const std::vector<Argument>& args) {
            if (args.empty()) return;
            std::string subdir = interp.evaluate_argument(args[0]);
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
            Parser parser(content);
            auto ast = parser.parse();
            if (ast) {
                auto res = sub_interp.interpret(ast.value());
                if (!res) interp.set_fatal_error(res.error());
            } else {
                interp.set_fatal_error(InterpreterError{cmake_file.string(), ast.error().row, ast.error().col, ast.error().reason});
            }
        });

        add_builtin("include", [](Interpreter& interp, const std::vector<Argument>& args) {
            if (args.empty()) { interp.set_fatal_error("include() requires an argument"); return; }
            std::string file_arg = interp.evaluate_argument(args[0]);
            bool optional = false;
            for (size_t i = 1; i < args.size(); ++i) if (interp.evaluate_argument(args[i]) == "OPTIONAL") optional = true;

            std::filesystem::path path = std::filesystem::path(file_arg).is_absolute() ?
                std::filesystem::path(file_arg) :
                std::filesystem::path(interp.get_variable("CMAKE_CURRENT_SOURCE_DIR")) / file_arg;
            if (!path.has_extension()) path.replace_extension(".cmake");

            if (!std::filesystem::exists(path)) {
                if (optional) return;
                interp.set_fatal_error("include() could not find: " + path.string());
                return;
            }

            std::ifstream file(path);
            if (!file) { interp.set_fatal_error("include() could not read: " + path.string()); return; }
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

            Parser parser(content);
            auto ast = parser.parse();
            if (!ast) {
                interp.set_fatal_error(InterpreterError{path.string(), ast.error().row, ast.error().col, ast.error().reason});
                return;
            }

            auto res = interp.interpret(ast.value());
            if (!res) interp.set_fatal_error(res.error());
        });

        add_builtin("break", [](Interpreter& interp, const std::vector<Argument>&) {
            if (interp.get_loop_depth() == 0) {
                interp.set_fatal_error("break() can only be called inside a loop");
                return;
            }
            interp.set_loop_control(Interpreter::LoopControl::BREAK);
        });

        add_builtin("continue", [](Interpreter& interp, const std::vector<Argument>&) {
            if (interp.get_loop_depth() == 0) {
                interp.set_fatal_error("continue() can only be called inside a loop");
                return;
            }
            interp.set_loop_control(Interpreter::LoopControl::CONTINUE);
        });
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
    }
    return {};
}

void Interpreter::set_fatal_error(const std::string& message) {
    set_fatal_error(InterpreterError{current_file_, current_cmd_row_, current_cmd_col_, message});
}

void Interpreter::set_fatal_error(const InterpreterError& error) {
    Interpreter* root = get_root();
    if (!root->fatal_error_) root->fatal_error_ = error;
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

std::expected<void, InterpreterError> Interpreter::execute_command(const CommandInvocation& cmd) {
    current_cmd_row_ = cmd.row;
    current_cmd_col_ = cmd.col;

    Interpreter* root = get_root();
    auto bit = root->builtins_.find(cmd.identifier);
    if (bit != root->builtins_.end()) {
        bit->second(*this, cmd.arguments);
        if (auto err = get_fatal_error()) {
            InterpreterError e = *err;
            clear_fatal_error();
            return std::unexpected(e);
        }
        return {};
    }

    Interpreter* curr = this;
    while (curr) {
        auto fit = curr->user_functions_.find(cmd.identifier);
        if (fit != curr->user_functions_.end()) return invoke_user_function(fit->second, cmd.arguments);
        auto mit = curr->user_macros_.find(cmd.identifier);
        if (mit != curr->user_macros_.end()) return invoke_user_macro(mit->second, cmd.arguments);
        curr = curr->parent_;
    }

    return std::unexpected(InterpreterError{current_file_, cmd.row, cmd.col, "Unknown command: " + cmd.identifier});
}

std::expected<void, InterpreterError> Interpreter::execute_if_block(const IfBlock& if_block) {
    return interpret(evaluate_condition(if_block.condition) ? if_block.then_branch : if_block.else_branch);
}

std::expected<void, InterpreterError> Interpreter::execute_function_block(const FunctionBlock& block) {
    user_functions_[block.name] = {block.parameters, block.body};
    return {};
}

std::expected<void, InterpreterError> Interpreter::execute_macro_block(const MacroBlock& block) {
    user_macros_[block.name] = {block.parameters, block.body};
    return {};
}

std::expected<void, InterpreterError> Interpreter::execute_foreach_block(const ForeachBlock& block) {
    loop_depth_++;
    CMakeList items;
    if (std::holds_alternative<ForeachSimple>(block.params)) {
        items = CMakeList::from_arguments(std::get<ForeachSimple>(block.params).items, this);
    } else if (std::holds_alternative<ForeachRange>(block.params)) {
        const auto& r = std::get<ForeachRange>(block.params);
        long start = r.start ? std::stol(evaluate_argument(*r.start)) : 0;
        long stop = std::stol(evaluate_argument(r.stop));
        long step = r.step ? std::stol(evaluate_argument(*r.step)) : 1;
        if (step == 0) { loop_depth_--; return std::unexpected(InterpreterError{current_file_, block.row, block.col, "Step cannot be zero"}); }
        for (long i = start; (step > 0) ? (i <= stop) : (i >= stop); i += step) items.append(std::to_string(i));
    } else if (std::holds_alternative<ForeachIn>(block.params)) {
        const auto& in = std::get<ForeachIn>(block.params);
        for (const auto& l : in.lists) items.append(CMakeList(get_variable(evaluate_argument(l))));
        items.append(CMakeList::from_arguments(in.items, this));
    }

    for (const auto& item : items) {
        set_variable(block.loop_var, item);
        auto res = interpret(block.body);
        if (!res) { loop_depth_--; return res; }
        if (loop_control_ == LoopControl::BREAK) { clear_loop_control(); break; }
        if (loop_control_ == LoopControl::CONTINUE) clear_loop_control();
    }
    loop_depth_--;
    return {};
}

std::expected<void, InterpreterError> Interpreter::invoke_user_function(const UserFunction& func, const std::vector<Argument>& args) {
    CallFrame frame{call_stack_.top().script_dir, {}};
    CMakeList all = CMakeList::from_arguments(args, this);
    frame.variables["ARGC"] = std::to_string(all.size());
    frame.variables["ARGV"] = all.to_string();
    frame.variables["ARGN"] = all.sublist(func.parameters.size(), all.size()).to_string();
    for (size_t i = 0; i < all.size(); ++i) frame.variables["ARGV" + std::to_string(i)] = all[i];
    for (size_t i = 0; i < func.parameters.size() && i < all.size(); ++i) frame.variables[func.parameters[i]] = all[i];

    int saved_depth = loop_depth_;
    LoopControl saved_control = loop_control_;
    loop_depth_ = 0;
    loop_control_ = LoopControl::NONE;

    call_stack_.push(frame);
    auto res = interpret(func.body);
    call_stack_.pop();

    loop_depth_ = saved_depth;
    loop_control_ = saved_control;
    return res;
}

std::expected<void, InterpreterError> Interpreter::invoke_user_macro(const UserMacro& macro, const std::vector<Argument>& args) {
    std::map<std::string, std::string> saved;
    CMakeList all = CMakeList::from_arguments(args, this);
    auto save = [&](const std::string& k) { if (call_stack_.top().variables.count(k)) saved[k] = call_stack_.top().variables[k]; };
    save("ARGC"); save("ARGV"); save("ARGN");
    for (size_t i = 0; i < all.size(); ++i) save("ARGV" + std::to_string(i));
    for (const auto& p : macro.parameters) save(p);

    set_variable("ARGC", std::to_string(all.size()));
    set_variable("ARGV", all.to_string());
    set_variable("ARGN", all.sublist(macro.parameters.size(), all.size()).to_string());
    for (size_t i = 0; i < all.size(); ++i) set_variable("ARGV" + std::to_string(i), all[i]);
    for (size_t i = 0; i < macro.parameters.size() && i < all.size(); ++i) set_variable(macro.parameters[i], all[i]);

    auto res = interpret(macro.body);

    for (const auto& [k, v] : saved) set_variable(k, v);
    auto& vars = call_stack_.top().variables;
    if (!saved.count("ARGC")) vars.erase("ARGC");
    if (!saved.count("ARGV")) vars.erase("ARGV");
    if (!saved.count("ARGN")) vars.erase("ARGN");
    for (size_t i = 0; i < all.size(); ++i) if (!saved.count("ARGV" + std::to_string(i))) vars.erase("ARGV" + std::to_string(i));
    for (const auto& p : macro.parameters) if (!saved.count(p)) vars.erase(p);
    return res;
}

bool Interpreter::evaluate_condition(const std::vector<Argument>& condition) {
    if (condition.empty()) return false;

    const auto& arg = condition[0];
    std::string val;

    if (!arg.quoted && arg.parts.size() == 1 && std::holds_alternative<std::string>(arg.parts[0])) {
        std::string name = std::get<std::string>(arg.parts[0]);
        // If it's a variable name, use its value. Otherwise use the string itself.
        std::string var_val = get_variable(name);
        if (!var_val.empty() || name == "0" || name == "FALSE" || name == "OFF") {
            val = var_val;
        } else {
            val = name;
        }
    } else {
        val = evaluate_argument(arg);
    }

    std::transform(val.begin(), val.end(), val.begin(), ::toupper);
    return !(val == "FALSE" || val == "OFF" || val == "0" || val.empty() || val == "NOTFOUND" || val == "IGNORE");
}

std::string Interpreter::evaluate_argument(const Argument& arg) {
    std::string res;
    for (const auto& p : arg.parts) {
        if (std::holds_alternative<std::string>(p)) res += std::get<std::string>(p);
        else res += get_variable(std::get<VariableReference>(p).name);
    }
    return res;
}

std::string Interpreter::get_variable(const std::string& name) const {
    if (!call_stack_.empty()) {
        std::stack<CallFrame> temp = call_stack_;
        while (!temp.empty()) {
            auto it = temp.top().variables.find(name);
            if (it != temp.top().variables.end()) return it->second;
            temp.pop();
        }
    }
    return parent_ ? parent_->get_variable(name) : "";
}

void Interpreter::set_variable(const std::string& name, const std::string& val) {
    if (!call_stack_.empty()) call_stack_.top().variables[name] = val;
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

}
