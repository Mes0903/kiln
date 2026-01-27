#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include "language.hpp"

namespace dmake {

enum class TargetType { EXECUTABLE, SHARED_LIBRARY, STATIC_LIBRARY, OBJECT_LIBRARY, INTERFACE_LIBRARY, CUSTOM };
enum class PropertyVisibility { PRIVATE, INTERFACE, PUBLIC };

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

    void add_sources(const std::vector<std::string>& sources, PropertyVisibility visibility);
    const std::vector<std::string>& get_sources(PropertyVisibility visibility) const;

    void add_linked_libraries(const std::vector<std::string>& libs, PropertyVisibility visibility);
    const std::vector<std::string>& get_linked_libraries(PropertyVisibility visibility) const;

    void add_include_directories(const std::vector<std::string>& dirs, PropertyVisibility visibility);
    const std::vector<std::string>& get_include_directories(PropertyVisibility visibility) const;

    void add_link_directories(const std::vector<std::string>& dirs, PropertyVisibility visibility);
    const std::vector<std::string>& get_link_directories(PropertyVisibility visibility) const;

    void add_compile_definitions(const std::vector<std::string>& defs, PropertyVisibility visibility);
    const std::vector<std::string>& get_compile_definitions(PropertyVisibility visibility) const;

    void add_compile_options(const std::vector<std::string>& opts, PropertyVisibility visibility);
    const std::vector<std::string>& get_compile_options(PropertyVisibility visibility) const;

    void add_precompiled_headers(const std::vector<std::string>& headers, PropertyVisibility visibility);
    const std::vector<std::string>& get_precompiled_headers(PropertyVisibility visibility) const;

    void set_output_name(std::string output_name);
    const std::string& get_output_name() const;

    void set_imported(bool imported) { is_imported_ = imported; }
    bool is_imported() const { return is_imported_; }

    void set_imported_location(std::string location) { imported_location_ = std::move(location); }
    const std::string& get_imported_location() const { return imported_location_; }

    void set_language_standard(Language lang, std::string standard);
    const std::string& get_language_standard(Language lang) const;

    void add_language_flags(Language lang, const std::vector<std::string>& flags);
    const std::vector<std::string>& get_language_flags(Language lang) const;

    void set_property(const std::string& name, std::string value);
    std::string get_property(const std::string& name) const;

    // Deprecated helpers for C++
    void set_cxx_standard(const std::string& standard) { set_language_standard(Language::CXX, standard); }
    const std::string& get_cxx_standard() const { return get_language_standard(Language::CXX); }

    virtual std::string get_output_path() const;

    // The core task generation logic
    virtual void generate_tasks(BuildGraph& graph, const Toolchain& toolchain, const std::map<std::string, std::shared_ptr<Target>>& all_targets, const std::vector<std::string>& exe_linker_flags = {}, const std::vector<std::string>& shared_linker_flags = {});

    // Resolves transitive properties (includes, definitions, options, libraries).
    // Must be called before generating tasks. Safe to call multiple times (cached).
    void resolve(const std::map<std::string, std::shared_ptr<Target>>& all_targets);

protected:
    // Helper methods for task generation
    void generate_object_tasks(BuildGraph& graph, const Toolchain& toolchain, std::vector<std::string>& obj_files,
                               const std::string& pch_gch_path, const std::string& pch_include_arg,
                               bool is_shared);

    std::string name_;
    std::string output_name_;
    TargetType type_;
    std::string source_dir_;
    std::string binary_dir_;
    
    bool is_imported_;
    std::string imported_location_;

    std::map<Language, std::string> standards_;
    std::map<Language, std::vector<std::string>> language_flags_;

    std::map<std::string, std::string> properties_;

    std::map<PropertyVisibility, std::vector<std::string>> sources_;
    std::map<PropertyVisibility, std::vector<std::string>> linked_libraries_;
    std::map<PropertyVisibility, std::vector<std::string>> include_directories_;
    std::map<PropertyVisibility, std::vector<std::string>> link_directories_;
    std::map<PropertyVisibility, std::vector<std::string>> compile_definitions_;
    std::map<PropertyVisibility, std::vector<std::string>> compile_options_;
    std::map<PropertyVisibility, std::vector<std::string>> precompiled_headers_;

    // Resolution State
    bool resolved_ = false;
    bool visiting_ = false;

    // Resolved Properties (effective build requirements)
    std::vector<std::string> resolved_includes_;
    std::vector<std::string> resolved_compile_definitions_;
    std::vector<std::string> resolved_compile_options_;
    std::vector<std::string> resolved_link_directories_;
    // Flattened list of libraries to link against (full paths or -l names)
    std::vector<std::string> resolved_link_libraries_;

    // Resolved Interface Properties (usage requirements propagated to dependents)
    std::vector<std::string> resolved_interface_includes_;
    std::vector<std::string> resolved_interface_compile_definitions_;
    std::vector<std::string> resolved_interface_compile_options_;
    std::vector<std::string> resolved_interface_link_directories_;
    std::vector<std::string> resolved_interface_link_libraries_;
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

    void generate_tasks(BuildGraph& graph, const Toolchain& toolchain, const std::map<std::string, std::shared_ptr<Target>>& all_targets, const std::vector<std::string>& exe_linker_flags = {}, const std::vector<std::string>& shared_linker_flags = {}) override;

private:
    std::vector<CustomCommand> custom_commands_;
    std::vector<std::string> custom_depends_;
    bool build_by_default_ = false;
};

} // namespace dmake
