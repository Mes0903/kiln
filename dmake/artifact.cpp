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

void Artifact::add_compile_definitions(const std::vector<std::string>& defs, PropertyVisibility visibility) {
    compile_definitions_[visibility].insert(compile_definitions_[visibility].end(), defs.begin(), defs.end());
}

const std::vector<std::string>& Artifact::get_compile_definitions(PropertyVisibility visibility) const {
    static const std::vector<std::string> empty;
    auto it = compile_definitions_.find(visibility);
    return (it != compile_definitions_.end()) ? it->second : empty;
}

void Artifact::add_compile_options(const std::vector<std::string>& opts, PropertyVisibility visibility) {
    compile_options_[visibility].insert(compile_options_[visibility].end(), opts.begin(), opts.end());
}

const std::vector<std::string>& Artifact::get_compile_options(PropertyVisibility visibility) const {
    static const std::vector<std::string> empty;
    auto it = compile_options_.find(visibility);
    return (it != compile_options_.end()) ? it->second : empty;
}

void Artifact::set_output_name(std::string output_name) {
    output_name_ = std::move(output_name);
}

const std::string& Artifact::get_output_name() const {
    return output_name_.empty() ? name_ : output_name_;
}

std::string ExecutableArtifact::get_output_path() const {
    return (std::filesystem::path(binary_dir_) / get_output_name()).string();
}

std::string LibraryArtifact::get_output_path() const {
    bool is_shared = (type_ == ArtifactType::SHARED_LIBRARY);
    std::string lib_name = "lib" + get_output_name() + (is_shared ? ".so" : ".a");
    return (std::filesystem::path(binary_dir_) / lib_name).string();
}

static std::string get_obj_path(const std::string& binary_dir, const std::string& source_file) {
    std::filesystem::path src(source_file);
    std::filesystem::path obj = std::filesystem::path(binary_dir) / "objs" / (src.filename().string() + ".o");
    return obj.string();
}

void ExecutableArtifact::generate_tasks(BuildGraph& graph) {
    std::vector<std::string> obj_files;
    
    // 1. Create Compile Tasks
    for (const auto& src : get_sources(PropertyVisibility::PRIVATE)) {
        std::filesystem::path src_abs = std::filesystem::path(source_dir_) / src;
        std::string obj = get_obj_path(binary_dir_, src_abs.string());
        obj_files.push_back(obj);
        
        BuildTask task;
        task.id = obj;
        task.parent_artifact = this;
        
        std::ostringstream cmd;
        cmd << "g++ -std=c++23";
        if (isatty(STDOUT_FILENO)) cmd << " -fdiagnostics-color=always";
        
        // Add compile options
        for (const auto& opt : get_compile_options(PropertyVisibility::PRIVATE)) cmd << " " << opt;
        for (const auto& opt : get_compile_options(PropertyVisibility::PUBLIC)) cmd << " " << opt;
        
        // Add compile definitions
        for (const auto& def : get_compile_definitions(PropertyVisibility::PRIVATE)) cmd << " -D" << def;
        for (const auto& def : get_compile_definitions(PropertyVisibility::PUBLIC)) cmd << " -D" << def;
        
        cmd << " -MMD -MF " << obj << ".d -c -o " << obj;
        
        for (const auto& dir : get_include_directories(PropertyVisibility::PRIVATE))
            cmd << " -I" << (std::filesystem::path(source_dir_) / dir).string();
        for (const auto& dir : get_include_directories(PropertyVisibility::PUBLIC))
            cmd << " -I" << (std::filesystem::path(source_dir_) / dir).string();
            
        cmd << " " << src_abs.string();
        
        task.command = cmd.str();
        task.inputs.push_back(src_abs.string());
        task.outputs.push_back(obj);
        task.outputs.push_back(obj + ".d");
        
        graph.add_task(std::move(task));
    }
    
    // 2. Create Link Task
    std::string output_path = get_output_path();
    BuildTask link;
    link.id = output_path;
    link.parent_artifact = this;
    
    std::ostringstream cmd;
    cmd << "g++ -std=c++23";
    if (isatty(STDOUT_FILENO)) cmd << " -fdiagnostics-color=always";
    cmd << " -Wl,-rpath,'$ORIGIN'";
    cmd << " -o " << link.id;
    for (const auto& obj : obj_files) {
        cmd << " " << obj;
        link.inputs.push_back(obj);
        link.dependencies.insert(obj);
    }
    
    cmd << " -L" << binary_dir_;
    for (const auto& lib : get_linked_libraries(PropertyVisibility::PRIVATE)) {
        cmd << " -l" << lib;
    }
    
    link.command = cmd.str();
    link.outputs.push_back(link.id);
    graph.add_task(std::move(link));
}

void LibraryArtifact::generate_tasks(BuildGraph& graph) {
    std::vector<std::string> obj_files;
    bool is_shared = (type_ == ArtifactType::SHARED_LIBRARY);
    
    for (const auto& src : get_sources(PropertyVisibility::PRIVATE)) {
        std::filesystem::path src_abs = std::filesystem::path(source_dir_) / src;
        std::string obj = get_obj_path(binary_dir_, src_abs.string());
        obj_files.push_back(obj);
        
        BuildTask task;
        task.id = obj;
        task.parent_artifact = this;
        
        std::ostringstream cmd;
        cmd << "g++ -std=c++23";
        if (isatty(STDOUT_FILENO)) cmd << " -fdiagnostics-color=always";
        
        // Add compile options
        for (const auto& opt : get_compile_options(PropertyVisibility::PRIVATE)) cmd << " " << opt;
        for (const auto& opt : get_compile_options(PropertyVisibility::PUBLIC)) cmd << " " << opt;
        
        // Add compile definitions
        for (const auto& def : get_compile_definitions(PropertyVisibility::PRIVATE)) cmd << " -D" << def;
        for (const auto& def : get_compile_definitions(PropertyVisibility::PUBLIC)) cmd << " -D" << def;
        
        cmd << " -MMD -MF " << obj << ".d";
        if (is_shared) cmd << " -fPIC";
        cmd << " -c -o " << obj;
        
        for (const auto& dir : get_include_directories(PropertyVisibility::PRIVATE))
            cmd << " -I" << (std::filesystem::path(source_dir_) / dir).string();
        for (const auto& dir : get_include_directories(PropertyVisibility::PUBLIC))
            cmd << " -I" << (std::filesystem::path(source_dir_) / dir).string();
            
        cmd << " " << src_abs.string();
        
        task.command = cmd.str();
        task.inputs.push_back(src_abs.string());
        task.outputs.push_back(obj);
        task.outputs.push_back(obj + ".d");
        
        graph.add_task(std::move(task));
    }
    
    std::string output_path = get_output_path();
    
    BuildTask link;
    link.id = output_path;
    link.parent_artifact = this;
    
    std::ostringstream cmd;
    if (is_shared) {
        cmd << "g++ -std=c++23";
        if (isatty(STDOUT_FILENO)) cmd << " -fdiagnostics-color=always";
        cmd << " -Wl,-rpath,'$ORIGIN'";
        cmd << " -shared -o " << link.id;
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
