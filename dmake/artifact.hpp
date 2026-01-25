#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>

namespace dmake {

enum class ArtifactType { EXECUTABLE, SHARED_LIBRARY, STATIC_LIBRARY };
enum class PropertyVisibility { PRIVATE, INTERFACE, PUBLIC };

class BuildGraph;

class Artifact {
public:
    Artifact(std::string name, ArtifactType type, std::string source_dir, std::string binary_dir) 
        : name_(std::move(name)), type_(type), source_dir_(std::move(source_dir)), binary_dir_(std::move(binary_dir)) {}
    virtual ~Artifact() = default;

    const std::string& get_name() const { return name_; }
    ArtifactType get_type() const { return type_; }
    const std::string& get_source_dir() const { return source_dir_; }
    const std::string& get_binary_dir() const { return binary_dir_; }

    void add_sources(const std::vector<std::string>& sources, PropertyVisibility visibility);
    const std::vector<std::string>& get_sources(PropertyVisibility visibility) const;

    void add_linked_libraries(const std::vector<std::string>& libs, PropertyVisibility visibility);
    const std::vector<std::string>& get_linked_libraries(PropertyVisibility visibility) const;

    void add_include_directories(const std::vector<std::string>& dirs, PropertyVisibility visibility);
    const std::vector<std::string>& get_include_directories(PropertyVisibility visibility) const;

    void add_compile_definitions(const std::vector<std::string>& defs, PropertyVisibility visibility);
    const std::vector<std::string>& get_compile_definitions(PropertyVisibility visibility) const;

    void add_compile_options(const std::vector<std::string>& opts, PropertyVisibility visibility);
    const std::vector<std::string>& get_compile_options(PropertyVisibility visibility) const;

    void add_precompiled_headers(const std::vector<std::string>& headers, PropertyVisibility visibility);
    const std::vector<std::string>& get_precompiled_headers(PropertyVisibility visibility) const;

    void set_output_name(std::string output_name);
    const std::string& get_output_name() const;

    void set_cxx_standard(std::string standard);
    const std::string& get_cxx_standard() const;

    virtual std::string get_output_path() const = 0;

    // The core task generation logic
    virtual void generate_tasks(BuildGraph& graph) = 0;

protected:
    // Helper methods for task generation
    void generate_object_tasks(BuildGraph& graph, std::vector<std::string>& obj_files,
                               const std::string& pch_gch_path, const std::string& pch_include_arg,
                               bool is_shared);
    std::string build_compile_command(const std::string& source, const std::string& output,
                                      const std::string& pch_include_arg, bool is_shared) const;

    std::string name_;
    std::string output_name_;
    ArtifactType type_;
    std::string source_dir_;
    std::string binary_dir_;
    std::string cxx_standard_;
    std::map<PropertyVisibility, std::vector<std::string>> sources_;
    std::map<PropertyVisibility, std::vector<std::string>> linked_libraries_;
    std::map<PropertyVisibility, std::vector<std::string>> include_directories_;
    std::map<PropertyVisibility, std::vector<std::string>> compile_definitions_;
    std::map<PropertyVisibility, std::vector<std::string>> compile_options_;
    std::map<PropertyVisibility, std::vector<std::string>> precompiled_headers_;
};

class ExecutableArtifact : public Artifact {
public:
    explicit ExecutableArtifact(std::string name, std::string source_dir, std::string binary_dir) 
        : Artifact(std::move(name), ArtifactType::EXECUTABLE, std::move(source_dir), std::move(binary_dir)) {}
    std::string get_output_path() const override;
    void generate_tasks(BuildGraph& graph) override;
};

class LibraryArtifact : public Artifact {
public:
    LibraryArtifact(std::string name, ArtifactType type, std::string source_dir, std::string binary_dir) 
        : Artifact(std::move(name), type, std::move(source_dir), std::move(binary_dir)) {}
    std::string get_output_path() const override;
    void generate_tasks(BuildGraph& graph) override;
};

} // namespace dmake
