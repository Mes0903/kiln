#include "artifact.hpp"
#include "build_system.hpp"
#include <filesystem>
#include <sstream>

namespace dmake {

void Artifact::add_sources(const std::vector<std::string>& sources, PropertyVisibility visibility) {
    sources_[visibility].insert(sources_[visibility].end(), sources.begin(), sources.end());
}

const std::vector<std::string>& Artifact::get_sources(PropertyVisibility visibility) const {
    static const std::vector<std::string> empty;
    auto it = sources_.find(visibility);
    return (it != sources_.end()) ? it->second : empty;
}

void Artifact::add_linked_libraries(const std::vector<std::string>& libs, PropertyVisibility visibility) {
    linked_libraries_[visibility].insert(linked_libraries_[visibility].end(), libs.begin(), libs.end());
}

const std::vector<std::string>& Artifact::get_linked_libraries(PropertyVisibility visibility) const {
    static const std::vector<std::string> empty;
    auto it = linked_libraries_.find(visibility);
    return (it != linked_libraries_.end()) ? it->second : empty;
}

void Artifact::add_include_directories(const std::vector<std::string>& dirs, PropertyVisibility visibility) {
    include_directories_[visibility].insert(include_directories_[visibility].end(), dirs.begin(), dirs.end());
}

const std::vector<std::string>& Artifact::get_include_directories(PropertyVisibility visibility) const {
    static const std::vector<std::string> empty;
    auto it = include_directories_.find(visibility);
    return (it != include_directories_.end()) ? it->second : empty;
}

void Artifact::set_output_name(std::string output_name) {
    output_name_ = std::move(output_name);
}

const std::string& Artifact::get_output_name() const {
    return output_name_.empty() ? name_ : output_name_;
}

static std::string get_obj_path(const std::string& build_dir, const std::string& script_dir, const std::string& source_file) {
    std::filesystem::path src(source_file);
    std::filesystem::path obj = std::filesystem::path(script_dir) / build_dir / "objs" / (src.filename().string() + ".o");
    return obj.string();
}

void ExecutableArtifact::generate_tasks(BuildGraph& graph, const std::string& build_dir, const std::string& script_dir) {
    std::vector<std::string> obj_files;
    
    // 1. Create Compile Tasks
    for (const auto& src : get_sources(PropertyVisibility::PRIVATE)) {
        std::string obj = get_obj_path(build_dir, script_dir, src);
        obj_files.push_back(obj);
        
        BuildTask task;
        task.id = obj;
        task.parent_artifact = this;
        
        std::ostringstream cmd;
        cmd << "g++ -std=c++23 -MMD -MF " << obj << ".d -c -o " << obj;
        
        for (const auto& dir : get_include_directories(PropertyVisibility::PRIVATE))
            cmd << " -I" << (std::filesystem::path(script_dir) / dir).string();
        for (const auto& dir : get_include_directories(PropertyVisibility::PUBLIC))
            cmd << " -I" << (std::filesystem::path(script_dir) / dir).string();
            
        cmd << " " << (std::filesystem::path(script_dir) / src).string();
        
        task.command = cmd.str();
        task.inputs.push_back((std::filesystem::path(script_dir) / src).string());
        task.outputs.push_back(obj);
        task.outputs.push_back(obj + ".d"); // Header dependency file
        
        graph.add_task(std::move(task));
    }
    
    // 2. Create Link Task
    std::filesystem::path output_path = std::filesystem::path(script_dir) / build_dir / get_output_name();
    BuildTask link;
    link.id = output_path.string();
    link.parent_artifact = this;
    
    std::ostringstream cmd;
    cmd << "g++ -std=c++23 -o " << link.id;
    for (const auto& obj : obj_files) {
        cmd << " " << obj;
        link.inputs.push_back(obj);
        link.dependencies.insert(obj);
    }
    
    cmd << " -L" << (std::filesystem::path(script_dir) / build_dir).string();
    for (const auto& lib : get_linked_libraries(PropertyVisibility::PRIVATE)) {
        cmd << " -l" << lib;
        // The artifact for this lib might produce its own link task. 
        // We'll handle cross-artifact task dependencies in the graph construction phase.
    }
    
    link.command = cmd.str();
    link.outputs.push_back(link.id);
    graph.add_task(std::move(link));
}

void LibraryArtifact::generate_tasks(BuildGraph& graph, const std::string& build_dir, const std::string& script_dir) {
    std::vector<std::string> obj_files;
    bool is_shared = (type_ == ArtifactType::SHARED_LIBRARY);
    
    for (const auto& src : get_sources(PropertyVisibility::PRIVATE)) {
        std::string obj = get_obj_path(build_dir, script_dir, src);
        obj_files.push_back(obj);
        
        BuildTask task;
        task.id = obj;
        task.parent_artifact = this;
        
        std::ostringstream cmd;
        cmd << "g++ -std=c++23 -MMD -MF " << obj << ".d";
        if (is_shared) cmd << " -fPIC";
        cmd << " -c -o " << obj;
        
        for (const auto& dir : get_include_directories(PropertyVisibility::PRIVATE))
            cmd << " -I" << (std::filesystem::path(script_dir) / dir).string();
        for (const auto& dir : get_include_directories(PropertyVisibility::PUBLIC))
            cmd << " -I" << (std::filesystem::path(script_dir) / dir).string();
            
        cmd << " " << (std::filesystem::path(script_dir) / src).string();
        
        task.command = cmd.str();
        task.inputs.push_back((std::filesystem::path(script_dir) / src).string());
        task.outputs.push_back(obj);
        task.outputs.push_back(obj + ".d");
        
        graph.add_task(std::move(task));
    }
    
    std::string lib_name = "lib" + get_output_name() + (is_shared ? ".so" : ".a");
    std::filesystem::path output_path = std::filesystem::path(script_dir) / build_dir / lib_name;
    
    BuildTask link;
    link.id = output_path.string();
    link.parent_artifact = this;
    
    std::ostringstream cmd;
    if (is_shared) {
        cmd << "g++ -std=c++23 -shared -o " << link.id;
    } else {
        cmd << "ar rcs " << link.id;
    }
    
    for (const auto& obj : obj_files) {
        cmd << " " << obj;
        link.inputs.push_back(obj);
        link.dependencies.insert(obj);
    }
    
    link.command = cmd.str();
    link.outputs.push_back(link.id);
    graph.add_task(std::move(link));
}

} // namespace dmake
