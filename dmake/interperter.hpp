#pragma once

#include "cmake-language.hpp"
#include <map>
#include <functional>
#include <iostream>
#include <unistd.h> // For isatty
#include <vector>
#include <memory> // For std::shared_ptr

namespace dmake {

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


class Interpreter {
public:
    using BuiltinFunction = std::function<void(const std::vector<Argument>&)>;

    explicit Interpreter(std::string script_dir, std::ostream* out = &std::cout, std::ostream* err = &std::cerr);

    void interpret(const std::vector<AstNode>& ast);
    void run_build();
    void add_builtin(const std::string& name, BuiltinFunction func);
    std::string evaluate_argument(const Argument& arg);

private:
    void execute_command(const CommandInvocation& cmd);
    void print_message(const std::string& mode, const std::string& message, bool is_error = false);

    std::string script_dir_;
    std::string build_dir_;
    std::ostream* out_;
    std::ostream* err_;
    std::map<std::string, std::string> variables_;
    std::map<std::string, BuiltinFunction> builtins_;
    std::map<std::string, std::shared_ptr<Target>> targets_;
};

} // namespace dmake
