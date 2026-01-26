#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include "language.hpp"

namespace dmake {

enum class TargetType { EXECUTABLE, SHARED_LIBRARY, STATIC_LIBRARY, OBJECT_LIBRARY, INTERFACE_LIBRARY };
enum class PropertyVisibility { PRIVATE, INTERFACE, PUBLIC };

class BuildGraph;

class Toolchain;

class Target {
public:
    Target(std::string name, TargetType type, std::string source_dir, std::string binary_dir)
        : name_(std::move(name)), type_(type), source_dir_(std::move(source_dir)), binary_dir_(std::move(binary_dir)) {}
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

    void set_language_standard(Language lang, std::string standard);
    const std::string& get_language_standard(Language lang) const;

    void add_language_flags(Language lang, const std::vector<std::string>& flags);
    const std::vector<std::string>& get_language_flags(Language lang) const;

    // Deprecated helpers for C++
    void set_cxx_standard(const std::string& standard) { set_language_standard(Language::CXX, standard); }
    const std::string& get_cxx_standard() const { return get_language_standard(Language::CXX); }

    std::string get_output_path() const;

    // The core task generation logic
    void generate_tasks(BuildGraph& graph, const Toolchain& toolchain, const std::map<std::string, std::shared_ptr<Target>>& all_targets);

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
    
    std::map<Language, std::string> standards_;
    std::map<Language, std::vector<std::string>> language_flags_;

    std::map<PropertyVisibility, std::vector<std::string>> sources_;
    std::map<PropertyVisibility, std::vector<std::string>> linked_libraries_;
    std::map<PropertyVisibility, std::vector<std::string>> include_directories_;
    std::map<PropertyVisibility, std::vector<std::string>> link_directories_;
    std::map<PropertyVisibility, std::vector<std::string>> compile_definitions_;
    std::map<PropertyVisibility, std::vector<std::string>> compile_options_;
    std::map<PropertyVisibility, std::vector<std::string>> precompiled_headers_;
};

} // namespace dmake
