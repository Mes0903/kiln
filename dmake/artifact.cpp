#include "artifact.hpp"
#include "build_system.hpp"
#include <filesystem>
#include <fstream>
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

void Artifact::add_precompiled_headers(const std::vector<std::string>& headers, PropertyVisibility visibility) {
    precompiled_headers_[visibility].insert(precompiled_headers_[visibility].end(), headers.begin(), headers.end());
}

const std::vector<std::string>& Artifact::get_precompiled_headers(PropertyVisibility visibility) const {
    static const std::vector<std::string> empty;
    auto it = precompiled_headers_.find(visibility);
    return (it != precompiled_headers_.end()) ? it->second : empty;
}

void Artifact::set_output_name(std::string output_name) {
    output_name_ = std::move(output_name);
}

const std::string& Artifact::get_output_name() const {
    return output_name_.empty() ? name_ : output_name_;
}

void Artifact::set_cxx_standard(std::string standard) {
    cxx_standard_ = std::move(standard);
}

const std::string& Artifact::get_cxx_standard() const {
    return cxx_standard_;
}

std::string ExecutableArtifact::get_output_path() const {
    auto path = std::filesystem::path(binary_dir_) / get_output_name();
    return binary_dir_.empty() ? path.string() : path.lexically_normal().string();
}

std::string LibraryArtifact::get_output_path() const {
    bool is_shared = (type_ == ArtifactType::SHARED_LIBRARY);
    std::string lib_name = "lib" + get_output_name() + (is_shared ? ".so" : ".a");
    auto path = std::filesystem::path(binary_dir_) / lib_name;
    return binary_dir_.empty() ? path.string() : path.lexically_normal().string();
}

static std::string get_obj_path(const std::string& binary_dir, const std::string& source_file) {
    std::filesystem::path src(source_file);
    std::filesystem::path obj = std::filesystem::path(binary_dir) / "objs" / (src.filename().string() + ".o");
    return binary_dir.empty() ? obj.string() : obj.lexically_normal().string();
}

std::string Artifact::build_compile_command(const std::string& source, const std::string& output,
                                             const std::string& pch_include_arg, bool is_shared) const {
    std::ostringstream cmd;
    cmd << "g++";

    // Add C++ standard
    if (!cxx_standard_.empty()) {
        cmd << " -std=c++" << cxx_standard_;
    }

    // Add color diagnostics
    if (isatty(STDOUT_FILENO)) cmd << " -fdiagnostics-color=always";

    // Add compile options
    for (const auto& opt : get_compile_options(PropertyVisibility::PRIVATE)) cmd << " " << opt;
    for (const auto& opt : get_compile_options(PropertyVisibility::PUBLIC)) cmd << " " << opt;

    // Add compile definitions
    for (const auto& def : get_compile_definitions(PropertyVisibility::PRIVATE)) cmd << " -D" << def;
    for (const auto& def : get_compile_definitions(PropertyVisibility::PUBLIC)) cmd << " -D" << def;

    // Dependency file generation
    cmd << " -MMD -MF " << output << ".d";

    // Position independent code for shared libraries
    if (is_shared) cmd << " -fPIC";

    // Output specification
    cmd << " -c -o " << output;

    // Include directories
    for (const auto& dir : get_include_directories(PropertyVisibility::PRIVATE))
        cmd << " -I" << (std::filesystem::path(source_dir_) / dir).string();
    for (const auto& dir : get_include_directories(PropertyVisibility::PUBLIC))
        cmd << " -I" << (std::filesystem::path(source_dir_) / dir).string();

    // Add source directory to include path if using PCH (so headers referenced in PCH can be found)
    if (!pch_include_arg.empty()) {
        cmd << " -I" << source_dir_;
    }

    // PCH include
    if (!pch_include_arg.empty()) cmd << pch_include_arg;

    // Source file
    cmd << " " << source;

    return cmd.str();
}

void Artifact::generate_object_tasks(BuildGraph& graph, std::vector<std::string>& obj_files,
                                      const std::string& pch_gch_path, const std::string& pch_include_arg,
                                      bool is_shared) {
    for (const auto& src : get_sources(PropertyVisibility::PRIVATE)) {
        std::filesystem::path src_abs = std::filesystem::path(source_dir_) / src;
        std::string obj = get_obj_path(binary_dir_, src_abs.string());
        obj_files.push_back(obj);

        BuildTask task;
        task.id = obj;
        task.parent_artifact = this;
        task.command = build_compile_command(src_abs.string(), obj, pch_include_arg, is_shared);
        task.inputs.push_back(src_abs.string());
        task.outputs.push_back(obj);
        task.outputs.push_back(obj + ".d");

        // Add PCH dependency if present
        if (!pch_gch_path.empty()) {
            task.dependencies.insert(pch_gch_path);
        }

        graph.add_task(std::move(task));
    }
}

// Returns (pch_gch_path, pch_include_arg). Both empty if no PCH needed.
static std::pair<std::string, std::string> generate_pch_task(BuildGraph& graph, const Artifact* artifact, bool is_shared) {
    auto private_pchs = artifact->get_precompiled_headers(PropertyVisibility::PRIVATE);
    auto public_pchs = artifact->get_precompiled_headers(PropertyVisibility::PUBLIC);

    if (private_pchs.empty() && public_pchs.empty()) {
        return {"", ""};
    }

    // Create PCH wrapper header
    auto pch_path = std::filesystem::path(artifact->get_binary_dir()) / "objs" / (artifact->get_name() + "_pch.hpp");
    std::string pch_wrapper = artifact->get_binary_dir().empty() ? pch_path.string() : pch_path.lexically_normal().string();
    std::string pch_gch_path = pch_wrapper + ".gch";
    std::string pch_include_arg = " -include " + pch_wrapper;

    // Write wrapper header content
    std::ostringstream wrapper_content;
    for (const auto& hdr : private_pchs) wrapper_content << "#include \"" << hdr << "\"\n";
    for (const auto& hdr : public_pchs) wrapper_content << "#include \"" << hdr << "\"\n";
    std::string content = wrapper_content.str();

    // Only write if file doesn't exist or content changed (to preserve timestamp for incremental builds)
    bool needs_write = true;
    if (std::filesystem::exists(pch_wrapper)) {
        std::ifstream existing(pch_wrapper);
        if (existing) {
            std::string existing_content((std::istreambuf_iterator<char>(existing)), std::istreambuf_iterator<char>());
            if (existing_content == content) {
                needs_write = false;
            }
        }
    }

    if (needs_write) {
        std::filesystem::create_directories(std::filesystem::path(pch_wrapper).parent_path());
        std::ofstream wrapper_file(pch_wrapper);
        if (!wrapper_file) {
            throw std::runtime_error("Failed to create PCH wrapper file: " + pch_wrapper);
        }
        wrapper_file << content;
    }

    // Create PCH compile task
    BuildTask pch_task;
    pch_task.id = pch_gch_path;
    pch_task.parent_artifact = const_cast<Artifact*>(artifact);

    std::ostringstream cmd;
    cmd << "g++";

    // Add C++ standard
    if (!artifact->get_cxx_standard().empty()) {
        cmd << " -std=c++" << artifact->get_cxx_standard();
    }

    if (isatty(STDOUT_FILENO)) cmd << " -fdiagnostics-color=always";

    // Add compile options
    for (const auto& opt : artifact->get_compile_options(PropertyVisibility::PRIVATE)) cmd << " " << opt;
    for (const auto& opt : artifact->get_compile_options(PropertyVisibility::PUBLIC)) cmd << " " << opt;

    // Add compile definitions
    for (const auto& def : artifact->get_compile_definitions(PropertyVisibility::PRIVATE)) cmd << " -D" << def;
    for (const auto& def : artifact->get_compile_definitions(PropertyVisibility::PUBLIC)) cmd << " -D" << def;

    // Add source directory as include path (for header resolution)
    cmd << " -I" << artifact->get_source_dir();

    // Add include directories
    for (const auto& dir : artifact->get_include_directories(PropertyVisibility::PRIVATE))
        cmd << " -I" << (std::filesystem::path(artifact->get_source_dir()) / dir).string();
    for (const auto& dir : artifact->get_include_directories(PropertyVisibility::PUBLIC))
        cmd << " -I" << (std::filesystem::path(artifact->get_source_dir()) / dir).string();

    if (is_shared) cmd << " -fPIC";
    cmd << " -x c++-header -o " << pch_gch_path << " " << pch_wrapper;

    pch_task.command = cmd.str();
    pch_task.inputs.push_back(pch_wrapper);
    pch_task.outputs.push_back(pch_gch_path);

    graph.add_task(std::move(pch_task));

    return {pch_gch_path, pch_include_arg};
}

void ExecutableArtifact::generate_tasks(BuildGraph& graph) {
    std::vector<std::string> obj_files;

    // 1. Generate PCH task if needed
    auto [pch_gch_path, pch_include_arg] = generate_pch_task(graph, this, false);

    // 2. Generate object file tasks
    generate_object_tasks(graph, obj_files, pch_gch_path, pch_include_arg, false);

    // 3. Create Link Task
    std::string output_path = get_output_path();
    BuildTask link;
    link.id = output_path;
    link.parent_artifact = this;

    std::ostringstream cmd;
    cmd << "g++";
    if (!cxx_standard_.empty()) {
        cmd << " -std=c++" << cxx_standard_;
    }
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

    // 1. Generate PCH task if needed
    auto [pch_gch_path, pch_include_arg] = generate_pch_task(graph, this, is_shared);

    // 2. Generate object file tasks
    generate_object_tasks(graph, obj_files, pch_gch_path, pch_include_arg, is_shared);

    // 3. Create link/archive task
    std::string output_path = get_output_path();
    
    BuildTask link;
    link.id = output_path;
    link.parent_artifact = this;

    std::ostringstream cmd;
    if (is_shared) {
        cmd << "g++";
        if (!cxx_standard_.empty()) {
            cmd << " -std=c++" << cxx_standard_;
        }
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
