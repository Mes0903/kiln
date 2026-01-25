#pragma once

#include "cmake-language.hpp"
#include <map>
#include <functional>
#include <iostream>
#include <unistd.h> // For isatty
#include <vector>
#include <memory> // For std::shared_ptr
#include <stack>
#include <expected>
#include <optional>

namespace dmake {

struct InterpreterError {
    std::string file;
    size_t row;
    size_t col;
    std::string message;
};

// ANSI escape codes for colors
namespace colors {
    const std::string RESET = "\033[0m";
    const std::string RED = "\033[31m";
    const std::string YELLOW = "\033[33m";
    const std::string CYAN = "\033[36m";
    const std::string MAGENTA = "\033[35m";
} // namespace colors

enum class TargetType { EXECUTABLE, SHARED_LIBRARY, STATIC_LIBRARY, INTERFACE_LIBRARY };
enum class PropertyVisibility { PRIVATE, INTERFACE, PUBLIC };

class Target {
public:
    Target(std::string name, TargetType type) : name_(std::move(name)), type_(type) {}
    virtual ~Target() = default;

    const std::string& get_name() const { return name_; }
    TargetType get_type() const { return type_; }

    void add_sources(const std::vector<std::string>& sources, PropertyVisibility visibility);
    const std::vector<std::string>& get_sources(PropertyVisibility visibility) const;

    void add_linked_libraries(const std::vector<std::string>& libs, PropertyVisibility visibility);
    const std::vector<std::string>& get_linked_libraries(PropertyVisibility visibility) const;

    void add_include_directories(const std::vector<std::string>& dirs, PropertyVisibility visibility);
    const std::vector<std::string>& get_include_directories(PropertyVisibility visibility) const;

    void set_output_name(std::string output_name);
    const std::string& get_output_name() const;

    virtual std::string get_build_command(const std::string& build_dir, const std::string& script_dir) const = 0;

protected:
    std::string name_;
    std::string output_name_;
    TargetType type_;
    std::map<PropertyVisibility, std::vector<std::string>> sources_;
    std::map<PropertyVisibility, std::vector<std::string>> linked_libraries_;
    std::map<PropertyVisibility, std::vector<std::string>> include_directories_;
};

class ExecutableTarget : public Target {
public:
    explicit ExecutableTarget(std::string name) : Target(std::move(name), TargetType::EXECUTABLE) {}
    std::string get_build_command(const std::string& build_dir, const std::string& script_dir) const override;
};

class LibraryTarget : public Target {
public:
    LibraryTarget(std::string name, TargetType type) : Target(std::move(name), type) {}
    std::string get_build_command(const std::string& build_dir, const std::string& script_dir) const override;
};

// Forward declaration
class Interpreter;

// Helper class for CMake list operations
// In CMake, lists are semicolon-separated strings
class CMakeList {
public:
    // Constructors
    CMakeList() = default;
    explicit CMakeList(const std::string& semicolon_separated);
    explicit CMakeList(const std::vector<std::string>& items);
    CMakeList(std::initializer_list<std::string> items);

    // Factory methods
    static CMakeList from_arguments(const std::vector<Argument>& args, Interpreter* interp);

    // Conversion
    std::string to_string() const;
    std::vector<std::string> to_vector() const;

    // Access
    size_t size() const { return items_.size(); }
    bool empty() const { return items_.empty(); }
    const std::string& operator[](size_t idx) const { return items_[idx]; }
    const std::string& at(size_t idx) const { return items_.at(idx); }

    // Iteration
    auto begin() const { return items_.begin(); }
    auto end() const { return items_.end(); }
    auto begin() { return items_.begin(); }
    auto end() { return items_.end(); }

    // Modification
    void append(const std::string& item);
    void append(const CMakeList& other);
    void push_back(const std::string& item) { append(item); }

    // List operations (like CMake's list command)
    void reverse();
    void sort();
    void remove_duplicates();
    CMakeList sublist(size_t begin_idx, size_t length) const;

private:
    std::vector<std::string> items_;

    // Helper to split semicolon-separated string
    static std::vector<std::string> split_by_semicolon(const std::string& str);
};

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
    using BuiltinFunction = std::function<void(const std::vector<Argument>&)>;

    explicit Interpreter(std::string script_dir, std::ostream* out = &std::cout, std::ostream* err = &std::cerr, Interpreter* parent = nullptr);

    std::expected<void, InterpreterError> interpret(const std::vector<AstNode>& ast);
    std::expected<void, InterpreterError> run_build();
    void add_builtin(const std::string& name, BuiltinFunction func);
    std::string evaluate_argument(const Argument& arg);

    void set_current_file(const std::string& file) { current_file_ = file; }

protected:
    void set_fatal_error(const std::string& message);
    void set_fatal_error(const InterpreterError& error);
    std::optional<InterpreterError> get_fatal_error() const;
    void clear_fatal_error();

private:
    std::expected<void, InterpreterError> execute_command(const CommandInvocation& cmd);
    std::expected<void, InterpreterError> execute_if_block(const IfBlock& if_block);
    std::expected<void, InterpreterError> execute_function_block(const FunctionBlock& function_block);
    std::expected<void, InterpreterError> execute_macro_block(const MacroBlock& macro_block);
    std::expected<void, InterpreterError> invoke_user_function(const std::string& name, const std::vector<Argument>& args);
    std::expected<void, InterpreterError> invoke_user_macro(const std::string& name, const std::vector<Argument>& args);
    bool evaluate_condition(const std::vector<Argument>& condition);
    void print_message(const std::string& mode, const std::string& message, bool is_error = false);

    std::string get_variable(const std::string& var_name) const;
    void set_variable(const std::string& var_name, const std::string& value);


    std::string build_dir_;
    std::ostream* out_;
    std::ostream* err_;
    std::map<std::string, BuiltinFunction> builtins_;
    std::map<std::string, std::shared_ptr<Target>> targets_;
    std::map<std::string, UserFunction> user_functions_;
    std::map<std::string, UserMacro> user_macros_;
    Interpreter* parent_ = nullptr;
    std::stack<CallFrame> call_stack_;
    std::string current_file_;
    std::optional<InterpreterError> fatal_error_;
    size_t current_cmd_row_ = 0;
    size_t current_cmd_col_ = 0;
};

} // namespace dmake
