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
    Artifact(std::string name, ArtifactType type) : name_(std::move(name)), type_(type) {}
    virtual ~Artifact() = default;

    const std::string& get_name() const { return name_; }
    ArtifactType get_type() const { return type_; }

    void add_sources(const std::vector<std::string>& sources, PropertyVisibility visibility);
    const std::vector<std::string>& get_sources(PropertyVisibility visibility) const;

    void add_linked_libraries(const std::vector<std::string>& libs, PropertyVisibility visibility);
    const std::vector<std::string>& get_linked_libraries(PropertyVisibility visibility) const;

    void add_include_directories(const std::vector<std::string>& dirs, PropertyVisibility visibility);
    const std::vector<std::string>& get_include_directories(PropertyVisibility visibility) const;

    void set_output_name(std::string output_name);
    const std::string& get_output_name() const;
    
    virtual std::string get_output_path(const std::string& build_dir, const std::string& script_dir) const = 0;

    // The core task generation logic
    virtual void generate_tasks(BuildGraph& graph, const std::string& build_dir, const std::string& script_dir) = 0;

protected:
    std::string name_;
    std::string output_name_;
    ArtifactType type_;
    std::map<PropertyVisibility, std::vector<std::string>> sources_;
    std::map<PropertyVisibility, std::vector<std::string>> linked_libraries_;
    std::map<PropertyVisibility, std::vector<std::string>> include_directories_;
};

class ExecutableArtifact : public Artifact {
public:
    explicit ExecutableArtifact(std::string name) : Artifact(std::move(name), ArtifactType::EXECUTABLE) {}
    std::string get_output_path(const std::string& build_dir, const std::string& script_dir) const override;
    void generate_tasks(BuildGraph& graph, const std::string& build_dir, const std::string& script_dir) override;
};

class LibraryArtifact : public Artifact {
public:
    LibraryArtifact(std::string name, ArtifactType type) : Artifact(std::move(name), type) {}
    std::string get_output_path(const std::string& build_dir, const std::string& script_dir) const override;
    void generate_tasks(BuildGraph& graph, const std::string& build_dir, const std::string& script_dir) override;
};

} // namespace dmake
