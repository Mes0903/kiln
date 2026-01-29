#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include "language.hpp"

namespace dmake {

enum class TargetType { EXECUTABLE, SHARED_LIBRARY, STATIC_LIBRARY, OBJECT_LIBRARY, INTERFACE_LIBRARY, CUSTOM };
enum class PropertyVisibility { PRIVATE, INTERFACE, PUBLIC };

struct FileSet {
    std::string name;
    std::string type;  // "HEADERS" or "CXX_MODULES"
    PropertyVisibility visibility;
    std::vector<std::string> base_dirs;
    std::vector<std::string> files;
};

struct CustomCommand {
    std::vector<std::string> command;
    std::string comment;
    std::string working_dir;
};

class BuildGraph;

class Toolchain;

class Target {
public:
    Target(std::string name, TargetType type, std::string source_dir, std::string binary_dir)
        : name_(std::move(name)), type_(type), source_dir_(std::move(source_dir)), binary_dir_(std::move(binary_dir)), is_imported_(false) {}
    virtual ~Target() = default;

    const std::string& get_name() const { return name_; }
    TargetType get_type() const { return type_; }
    const std::string& get_source_dir() const { return source_dir_; }
    const std::string& get_binary_dir() const { return binary_dir_; }

    void set_output_name(std::string output_name);
    const std::string& get_output_name() const;

    void set_imported(bool imported) { is_imported_ = imported; }
    bool is_imported() const { return is_imported_; }

    void set_imported_location(std::string location) { imported_location_ = std::move(location); }
    const std::string& get_imported_location() const { return imported_location_; }

    void set_language_standard(Language lang, std::string standard);
    const std::string& get_language_standard(Language lang) const;

    void set_language_extensions(Language lang, bool enabled);
    bool get_language_extensions(Language lang) const;

    void add_language_flags(Language lang, const std::vector<std::string>& flags);
    const std::vector<std::string>& get_language_flags(Language lang) const;

    // Generic Property Access
    void set_property(const std::string& name, const std::string& value);
    std::string get_property(const std::string& name) const;

    // Generic List Property Access (for properties that accumulate like SOURCES, DEFINITIONS)
    void append_property(const std::string& name, const std::vector<std::string>& values, PropertyVisibility visibility);
    const std::vector<std::string>& get_property_list(const std::string& name, PropertyVisibility visibility) const;

    // File Set Support
    void add_file_set(FileSet file_set);
    const std::vector<FileSet>& get_file_sets() const { return file_sets_; }
    bool is_in_cxx_modules_file_set(const std::string& source) const;

    // Manually added dependencies (add_dependencies command)
    void add_dependency(const std::string& dep) { manually_added_dependencies_.push_back(dep); }
    const std::vector<std::string>& get_manually_added_dependencies() const { return manually_added_dependencies_; }

    // Deprecated helpers for C++ (mapped to generic properties)
    void set_cxx_standard(const std::string& standard) { set_language_standard(Language::CXX, standard); }
    const std::string& get_cxx_standard() const { return get_language_standard(Language::CXX); }

    virtual std::string get_output_path() const;

    // The core task generation logic
    virtual void generate_tasks(BuildGraph& graph, const Toolchain& toolchain, const std::map<std::string, std::shared_ptr<Target>>& all_targets, const class Interpreter& interp, const std::vector<std::string>& exe_linker_flags = {}, const std::vector<std::string>& shared_linker_flags = {});

    // Resolves transitive properties (includes, definitions, options, libraries).
    // Must be called before generating tasks. Safe to call multiple times (cached).
    void resolve(const std::map<std::string, std::shared_ptr<Target>>& all_targets, const class Interpreter& interp);
    
    // Access resolved properties (after resolve() is called)
    const std::vector<std::string>& get_resolved_property(const std::string& name) const;
    const std::vector<std::string>& get_resolved_interface_property(const std::string& name) const;

    // Get the module mapper file path for this target
    std::string get_module_mapper_path() const;

protected:
    // Helper methods for task generation
    void generate_object_tasks(BuildGraph& graph, const Toolchain& toolchain, std::vector<std::string>& obj_files,
                               const std::string& pch_gch_path, const std::string& pch_include_arg,
                               bool is_shared, const std::map<std::string, std::shared_ptr<Target>>& all_targets);

    // C++20 modules task generation
    // Returns true if any module sources were detected
    bool generate_module_scanner_tasks(BuildGraph& graph, const Toolchain& toolchain);

    // Generate the collator task that depends on all scanner tasks
    // The collator parses DDI files and injects module dependencies into compile tasks
    void generate_module_collator_task(BuildGraph& graph, const std::vector<std::string>& scanner_task_ids);

    // Check if target has any module sources
    bool has_module_sources() const;

    std::string name_;
    std::string output_name_;
    TargetType type_;
    std::string source_dir_;
    std::string binary_dir_;
    
    bool is_imported_;
    std::string imported_location_;

    std::map<Language, std::string> standards_;
    std::map<Language, bool> extensions_enabled_;  // Whether GNU extensions are enabled (gnu11 vs c11)
    std::map<Language, std::vector<std::string>> language_flags_;

    // Generic Storage
    // Scalar properties (e.g., OUTPUT_NAME, CXX_STANDARD)
    std::map<std::string, std::string> properties_;
    
    // List properties with visibility (e.g., INCLUDE_DIRECTORIES[PUBLIC])
    // Structure: map<PropertyName, map<Visibility, vector<Value>>>
    std::map<std::string, std::map<PropertyVisibility, std::vector<std::string>>> list_properties_;

    // File Sets (CMake 3.23+)
    std::vector<FileSet> file_sets_;

    // Resolution State
    bool resolved_ = false;
    bool visiting_ = false;

    // Resolved Properties Cache (effective build requirements)
    // Map<PropertyName, ListOfValues>
    std::map<std::string, std::vector<std::string>> resolved_properties_;

    // Resolved Interface Properties Cache (usage requirements propagated to dependents)
    std::map<std::string, std::vector<std::string>> resolved_interface_properties_;

    // C++20 modules state
    mutable bool modules_detected_ = false;
    mutable bool has_modules_ = false;

    // Manually added dependencies (from add_dependencies command)
    std::vector<std::string> manually_added_dependencies_;
};

class CustomTarget : public Target {
public:
    CustomTarget(std::string name, std::string source_dir, std::string binary_dir)
        : Target(std::move(name), TargetType::CUSTOM, std::move(source_dir), std::move(binary_dir)) {}

    void add_custom_command(CustomCommand cmd) { custom_commands_.push_back(std::move(cmd)); }
    void add_custom_dependency(std::string dep) { custom_depends_.push_back(std::move(dep)); }
    const std::vector<std::string>& get_custom_dependencies() const { return custom_depends_; }
    void set_build_by_default(bool b) { build_by_default_ = b; }
    bool is_build_by_default() const { return build_by_default_; }

    void generate_tasks(BuildGraph& graph, const Toolchain& toolchain, const std::map<std::string, std::shared_ptr<Target>>& all_targets, const class Interpreter& interp, const std::vector<std::string>& exe_linker_flags = {}, const std::vector<std::string>& shared_linker_flags = {}) override;

private:
    std::vector<CustomCommand> custom_commands_;
    std::vector<std::string> custom_depends_;
    bool build_by_default_ = false;
};

} // namespace dmake
