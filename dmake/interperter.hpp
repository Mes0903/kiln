#pragma once

#include "cmake-language.hpp"
#include "target.hpp"
#include "build_system.hpp"
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <iostream>
#include <unistd.h>
#include <vector>
#include <memory>
#include <deque>
#include <expected>
#include <optional>
#include <filesystem>
#include "CMakeList.hpp"
#include "toolchain.hpp"
#include "cache_store.hpp"
#include "shadow_map.hpp"

namespace dmake {

struct InterpreterError {
    std::string file;
    size_t row;
    size_t col;
    size_t offset;
    size_t length;
    std::string message;
    std::vector<CallLocation> backtrace;
    std::optional<std::string> source_content = std::nullopt;
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
    std::map<std::string, std::string> properties; // Test properties
};

// Custom command rule for OUTPUT form of add_custom_command
// Maps output files to the commands that generate them
struct CustomCommandRule {
    std::vector<std::string> outputs;           // Files this command generates
    std::vector<std::vector<std::string>> commands;  // Commands to run (in order)
    std::vector<std::string> depends;           // Input files/targets
    std::string working_dir;                    // Working directory for commands
    std::string comment;                        // Display comment during build
    std::string source_dir;                     // Source directory where command was defined
    std::string binary_dir;                     // Binary directory where command was defined
};

// ANSI escape codes for colors
namespace colors {
    const std::string RESET = "\033[0m";
    const std::string RED = "\033[31m";
    const std::string BRIGHT_RED = "\033[91m";
    const std::string BOLD_RED = "\033[1;31m";
    const std::string YELLOW = "\033[33m";
    const std::string GREEN = "\033[32m";
    const std::string CYAN = "\033[36m";
    const std::string WHITE = "\033[37m";
    const std::string DIM = "\033[2m";
    const std::string DIM_CYAN = "\033[2;36m";
    const std::string MAGENTA = "\033[35m"; // Legacy, not used in new scheme
} // namespace colors

// Forward declaration
class Interpreter;

// Property system
enum class PropertyScope {
    GLOBAL,
    DIRECTORY,
    TARGET,
    SOURCE,
    TEST,
    VARIABLE,
    CACHED_VARIABLE,
    INSTALL
};

struct PropertyDefinition {
    PropertyScope scope;
    std::string name;
    bool inherited = false;
    std::string brief_docs;
    std::string full_docs;
    std::string initialize_from_variable; // For TARGET properties, optional
};

struct FrameMetadata {
    std::string script_dir;
    const FunctionBlock* function_block = nullptr;  // Pointer to FunctionBlock if this is a function frame
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
    void set_cache_variable(const std::string& var_name, const std::string& value);

    void print_message(const std::string& mode, const std::string& message, bool is_error = false);

    // CHECK_* message support
    void check_start(const std::string& message);
    void check_pass(const std::string& result_message);
    void check_fail(const std::string& result_message);

    // SEND_ERROR support - accumulates errors
    void accumulate_error(const std::string& error);
    bool has_accumulated_errors() const { return has_send_errors_; }

    std::expected<void, InterpreterError> include_file(const std::string& file_path, bool optional = false);

    // Cached file existence check - caching handled internally
    bool cached_file_exists(const std::filesystem::path& full_path);
    bool cached_file_exists(const std::filesystem::path& dir, const std::string& filename);

    int get_loop_depth() const { return loop_depth_; }
    void set_loop_control(LoopControl control) { loop_control_ = control; }
    void clear_loop_control() { loop_control_ = LoopControl::NONE; }

    // Access to targets (for testing and build system)
    std::map<std::string, std::shared_ptr<Target>>& get_targets() { return get_root()->targets_; }
    std::map<std::string, std::string>& get_target_aliases() { return get_root()->target_aliases_; }

    // Resolve alias to real target name (returns input if not an alias)
    std::string resolve_target_alias(const std::string& name) const {
        auto& aliases = get_root()->target_aliases_;
        auto it = aliases.find(name);
        return (it != aliases.end()) ? it->second : name;
    }

    // Check if a name is an alias
    bool is_target_alias(const std::string& name) const {
        return get_root()->target_aliases_.find(name) != get_root()->target_aliases_.end();
    }

    Toolchain& get_toolchain() { return get_root()->toolchain_; }
    CacheStore& get_cache_store() { return *get_root()->cache_store_; }

    // Directory mtime caching for find_xxx performance
    // Returns mtime of directory (nullopt if doesn't exist)
    // Uses session cache for paths outside source/binary dirs
    std::optional<int64_t> get_dir_mtime_cached(const std::string& path);

    // Check if path is under our source or binary directory (skip mtime caching for these)
    bool is_project_path(const std::string& path) const;

    // Apply accumulated directory properties to all owned targets (retroactive application)
    void finalize_directory_targets();

    std::vector<TestDefinition>& get_tests() { return get_root()->tests_; }
    void enable_testing_globally() { get_root()->testing_enabled_ = true; }
    bool is_testing_enabled() const { return get_root()->get_root()->testing_enabled_; }

    // Custom command rules (OUTPUT form of add_custom_command)
    std::map<std::string, std::shared_ptr<CustomCommandRule>>& get_custom_command_rules() {
        return get_root()->custom_command_rules_;
    }
    const std::map<std::string, std::shared_ptr<CustomCommandRule>>& get_custom_command_rules() const {
        return get_root()->custom_command_rules_;
    }

    // Property system accessors
    std::map<PropertyScope, std::map<std::string, PropertyDefinition>>& get_property_definitions() {
        return get_root()->property_definitions_;
    }
    std::map<std::string, std::string>& get_global_properties() {
        return get_root()->global_properties_;
    }
    std::map<std::string, std::map<std::string, std::string>>& get_source_properties() {
        return get_root()->source_properties_;
    }
    std::map<std::string, std::string>& get_cache_variables() {
        return get_root()->cache_variables_;
    }

    // Variable map accessor for builtins (needed for PARENT_SCOPE)
    ShadowMap& get_variables() { return variables_; }
    const ShadowMap& get_variables() const { return variables_; }
    std::map<std::string, std::string>& get_directory_properties() {
        return directory_properties_;
    }

    // Directory-to-interpreter registry for explicit DIRECTORY scope operations
    std::unordered_map<std::string, Interpreter*>& get_directory_interpreters() {
        return get_root()->directory_interpreters_;
    }
    Interpreter* get_interpreter_for_directory(const std::string& dir);

    // Install properties: installed_path -> property_name -> value
    std::map<std::string, std::map<std::string, std::string>>& get_install_properties() {
        return get_root()->install_properties_;
    }

    // Friend registration functions
    friend void register_message_builtins(Interpreter& interp);
    friend void register_variable_builtins(Interpreter& interp);
    friend void register_list_builtins(Interpreter& interp);
    friend void register_target_builtins(Interpreter& interp);
    friend void register_project_builtins(Interpreter& interp);
    friend void register_file_builtins(Interpreter& interp);
    friend void register_find_commands_builtins(Interpreter& interp);
    friend void register_process_builtins(Interpreter& interp);
    friend void register_math_builtins(Interpreter& interp);
    friend void register_string_builtins(Interpreter& interp);
    friend void register_property_builtins(Interpreter& interp);
    friend void register_try_compile_builtins(Interpreter& interp);
    friend void register_path_builtins(Interpreter& interp);

    CMakeList from_arguments(const std::vector<std::string>& args);

    void request_return() { return_requested_ = true; }
    bool is_return_requested() const { return return_requested_; }
    void clear_return_request() { return_requested_ = false; }

    // Debug/validation helpers
    void check_invariants() const;
    void safe_pop_trace_stack(const std::string& context);


private:
    std::expected<void, InterpreterError> execute_command(const CommandInvocation& cmd);
    std::expected<void, InterpreterError> execute_command_with_args(const std::string& identifier, const std::vector<std::string>& args);
    std::expected<void, InterpreterError> execute_if_block(const IfBlock& if_block);
    std::expected<void, InterpreterError> execute_function_block(const FunctionBlock& function_block);
    std::expected<void, InterpreterError> execute_macro_block(const MacroBlock& macro_block);
    std::expected<void, InterpreterError> execute_foreach_block(const ForeachBlock& foreach_block);
    std::expected<void, InterpreterError> execute_while_block(const WhileBlock& while_block);
    std::expected<void, InterpreterError> execute_block_block(const BlockBlock& block_block);
    std::expected<void, InterpreterError> invoke_user_function(const FunctionBlock& func, const std::vector<std::string>& args);
    std::expected<void, InterpreterError> invoke_user_macro(const MacroBlock& macro, const std::vector<std::string>& args);
    std::expected<bool, InterpreterError> evaluate_condition(const std::vector<Argument>& condition, size_t row, size_t col, size_t offset, size_t length);
    std::string evaluate_variable_reference(const VariableReference& ref);

    std::optional<InterpreterError> get_fatal_error() const;
    void clear_fatal_error();

    Interpreter* get_root();
    const Interpreter* get_root() const;

    // Internal cache helper - returns directory listing if cached and valid
    const std::unordered_set<std::string>* get_cached_directory_listing(const std::filesystem::path& dir);

    std::string build_dir_;
    std::ostream* out_;
    std::ostream* err_;

    // Global state (managed by root)
    std::map<std::string, BuiltinFunction> builtins_;
    std::map<std::string, std::shared_ptr<Target>> targets_;
    std::map<std::string, std::string> target_aliases_;  // alias_name -> real_target_name
    std::vector<TestDefinition> tests_;
    bool testing_enabled_ = false;

    // Custom command rules (OUTPUT form of add_custom_command)
    // Maps output file path -> rule that generates it
    std::map<std::string, std::shared_ptr<CustomCommandRule>> custom_command_rules_;
    Toolchain toolchain_;
    std::unique_ptr<CacheStore> cache_store_;
    std::set<std::string> global_guarded_files_;
    std::map<std::string, std::string> cache_variables_;  // Fake cache namespace (not persistent)

    // Session-wide directory mtime cache (for find_xxx performance)
    // Only caches directories outside source_dir and binary_dir
    // Key: absolute path, Value: mtime (or nullopt if doesn't exist)
    std::map<std::string, std::optional<int64_t>> dir_mtime_cache_;

    // Property system (managed by root)
    // Property definitions: scope -> property_name -> definition
    std::map<PropertyScope, std::map<std::string, PropertyDefinition>> property_definitions_;
    // Global property values: property_name -> value
    std::map<std::string, std::string> global_properties_;
    // Source property values: absolute_source_path -> property_name -> value
    std::map<std::string, std::map<std::string, std::string>> source_properties_;
    // Install property values: normalized_install_path -> property_name -> value
    std::map<std::string, std::map<std::string, std::string>> install_properties_;
    // Directory-to-interpreter registry for explicit DIRECTORY scope operations
    std::unordered_map<std::string, Interpreter*> directory_interpreters_;

    // Directory scan cache for optimizing file lookups
    struct DirectoryCacheEntry {
        std::filesystem::file_time_type mtime;           // Directory modification time
        std::unordered_set<std::string> entries;         // All entries (filenames only) - O(1) lookup
    };
    std::unordered_map<std::string, DirectoryCacheEntry> dir_scan_cache_;  // Key: absolute directory path

    // Generic directory-scoped accumulated properties (per interpreter, inherited by children)
    // Stores lists like: COMPILE_DEFINITIONS, COMPILE_OPTIONS, INCLUDE_DIRECTORIES, LINK_DIRECTORIES, etc.
    std::map<std::string, std::vector<std::string>> accumulated_directory_properties_;

    // Track targets created by this interpreter (for retroactive property application)
    std::vector<std::shared_ptr<Target>> owned_targets_;

    std::set<std::string> directory_guarded_files_;

    // Directory-scoped property values (per interpreter scope)
    std::map<std::string, std::string> directory_properties_;

    // Global functions and macros (stored at root, accessible everywhere)
    // CMake semantics: functions/macros are globally visible once defined
    std::unordered_map<std::string, std::unique_ptr<FunctionBlock>> user_functions_;
    std::unordered_map<std::string, std::unique_ptr<MacroBlock>> user_macros_;

    Interpreter* parent_ = nullptr;

    // Shadow Map-based variable scoping (O(1) access, automatic cleanup)
    ShadowMap variables_;  // Regular variables with scope tracking

    std::deque<FrameMetadata> frame_stack_; // Metadata only (no variables)
    std::vector<CallLocation> trace_stack_; // For backtraces
    std::string current_file_;
    std::optional<InterpreterError> fatal_error_;
    size_t current_cmd_row_ = 0;
    size_t current_cmd_col_ = 0;

    // Loop control state (local to current script/function scope)
    int loop_depth_ = 0;
    LoopControl loop_control_ = LoopControl::NONE;

    // Return control state (for return() command)
    bool return_requested_ = false;

    // Cache the root interpreter to avoid walking the parent chain
    Interpreter* root_ = nullptr;

    // Macro parameter substitution (for text-replacement in macros)
    // Checked before variable lookup to implement CMake macro semantics
    std::map<std::string, std::string> macro_substitutions_;

    // CHECK_* message state (stack for nested checks)
    std::vector<std::string> check_stack_;

    // SEND_ERROR accumulation
    bool has_send_errors_ = false;
};

} // namespace dmake
