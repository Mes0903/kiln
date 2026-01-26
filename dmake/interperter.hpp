#pragma once

#include "cmake-language.hpp"
#include "target.hpp"
#include "build_system.hpp"
#include <map>
#include <set>
#include <functional>
#include <iostream>
#include <unistd.h>
#include <vector>
#include <memory>
#include <deque>
#include <expected>
#include <optional>
#include "CMakeList.hpp"
#include "toolchain.hpp"

namespace dmake {

struct InterpreterError {
    std::string file;
    size_t row;
    size_t col;
    size_t offset;
    size_t length;
    std::string message;
    std::vector<CallLocation> backtrace;
};

struct BuildError {
    std::string file;
    std::string message;
};

struct TestDefinition {
    std::string name;
    std::string command; // Target name or path
    std::vector<std::string> args;
    std::string working_dir;
};

// ANSI escape codes for colors
namespace colors {
    const std::string RESET = "\033[0m";
    const std::string RED = "\033[31m";
    const std::string YELLOW = "\033[33m";
    const std::string CYAN = "\033[36m";
    const std::string MAGENTA = "\033[35m";
} // namespace colors

// Forward declaration
class Interpreter;

struct CallFrame {
    std::string script_dir;
    std::map<std::string, std::string> variables;
};

struct UserFunction {
    std::vector<std::string> parameters;
    std::vector<AstNode> body;
};

struct UserMacro {
    std::vector<std::string> parameters;
    std::vector<AstNode> body;
};

class Interpreter {
public:
    using BuiltinFunction = std::function<void(Interpreter&, const std::vector<std::string>&)>;
    enum class LoopControl { NONE, BREAK, CONTINUE };

    explicit Interpreter(std::string script_dir, std::ostream* out = &std::cout, std::ostream* err = &std::cerr, Interpreter* parent = nullptr, std::optional<std::string> build_dir = std::nullopt);

    std::expected<void, InterpreterError> interpret(const std::vector<AstNode>& ast);
    std::expected<Interpreter*, BuildError> run_build(int jobs = 0, const std::vector<std::string>& targets = {});
    void add_builtin(const std::string& name, BuiltinFunction func);
    std::string evaluate_argument(const Argument& arg);
    std::vector<std::string> expand_arguments(const std::vector<Argument>& args);

    void set_current_file(const std::string& file) { current_file_ = file; }
    std::string get_current_file() const { return current_file_; }

    // Public API for builtins and internal use
    void set_fatal_error(const std::string& message);
    void set_fatal_error(const InterpreterError& error);

    std::string get_variable(const std::string& var_name) const;
    void set_variable(const std::string& var_name, const std::string& value);
    bool unset_variable(const std::string& var_name);
    bool is_variable_set(const std::string& var_name) const;
    static bool is_falsy(const std::string& val);

    void print_message(const std::string& mode, const std::string& message, bool is_error = false);

    std::expected<void, InterpreterError> include_file(const std::string& file_path, bool optional = false);

    int get_loop_depth() const { return loop_depth_; }
    void set_loop_control(LoopControl control) { loop_control_ = control; }
    void clear_loop_control() { loop_control_ = LoopControl::NONE; }

    // Access to targets (for testing and build system)
    std::map<std::string, std::shared_ptr<Target>>& get_targets() { return get_root()->targets_; }
    Toolchain& get_toolchain() { return get_root()->toolchain_; }

    std::vector<TestDefinition>& get_tests() { return get_root()->tests_; }
    void enable_testing_globally() { get_root()->testing_enabled_ = true; }
    bool is_testing_enabled() const { return get_root()->get_root()->testing_enabled_; }

    // Friend registration functions
    friend void register_message_builtins(Interpreter& interp);
    friend void register_variable_builtins(Interpreter& interp);
    friend void register_list_builtins(Interpreter& interp);
    friend void register_target_builtins(Interpreter& interp);
    friend void register_project_builtins(Interpreter& interp);
    friend void register_file_builtins(Interpreter& interp);
    friend void register_process_builtins(Interpreter& interp);

    CMakeList from_arguments(const std::vector<std::string>& args);


private:
    std::expected<void, InterpreterError> execute_command(const CommandInvocation& cmd);
    std::expected<void, InterpreterError> execute_if_block(const IfBlock& if_block);
    std::expected<void, InterpreterError> execute_function_block(const FunctionBlock& function_block);
    std::expected<void, InterpreterError> execute_macro_block(const MacroBlock& macro_block);
    std::expected<void, InterpreterError> execute_foreach_block(const ForeachBlock& foreach_block);
    std::expected<void, InterpreterError> invoke_user_function(const UserFunction& func, const std::vector<std::string>& args);
    std::expected<void, InterpreterError> invoke_user_macro(const UserMacro& macro, const std::vector<std::string>& args);
    std::expected<bool, InterpreterError> evaluate_condition(const std::vector<Argument>& condition, size_t row, size_t col, size_t offset, size_t length);
    std::string evaluate_variable_reference(const VariableReference& ref);

    std::optional<InterpreterError> get_fatal_error() const;
    void clear_fatal_error();

    Interpreter* get_root();
    const Interpreter* get_root() const;

    std::string build_dir_;
    std::ostream* out_;
    std::ostream* err_;

    // Global state (managed by root)
    std::map<std::string, BuiltinFunction> builtins_;
    std::map<std::string, std::shared_ptr<Target>> targets_;
    std::vector<TestDefinition> tests_;
    bool testing_enabled_ = false;
    Toolchain toolchain_;
    std::set<std::string> global_guarded_files_;

    // Directory-scoped accumulated directories (inherited by targets)
    std::vector<std::string> accumulated_include_directories_;
    std::vector<std::string> accumulated_link_directories_;
    std::set<std::string> directory_guarded_files_;

    // Scope-local state
    std::map<std::string, UserFunction> user_functions_;
    std::map<std::string, UserMacro> user_macros_;

    Interpreter* parent_ = nullptr;
    std::deque<CallFrame> call_stack_; // Variable scopes
    std::vector<CallLocation> trace_stack_; // For backtraces
    std::string current_file_;
    std::optional<InterpreterError> fatal_error_;
    size_t current_cmd_row_ = 0;
    size_t current_cmd_col_ = 0;

    // Loop control state (local to current script/function scope)
    int loop_depth_ = 0;
    LoopControl loop_control_ = LoopControl::NONE;
};

} // namespace dmake