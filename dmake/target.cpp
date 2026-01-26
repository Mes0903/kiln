#include "target.hpp"
#include "build_system.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <algorithm>

namespace dmake {

void Target::add_sources(const std::vector<std::string>& sources, PropertyVisibility visibility) {
    sources_[visibility].insert(sources_[visibility].end(), sources.begin(), sources.end());
}

const std::vector<std::string>& Target::get_sources(PropertyVisibility visibility) const {
    static const std::vector<std::string> empty;
    auto it = sources_.find(visibility);
    return (it != sources_.end()) ? it->second : empty;
}

void Target::add_linked_libraries(const std::vector<std::string>& libs, PropertyVisibility visibility) {
    linked_libraries_[visibility].insert(linked_libraries_[visibility].end(), libs.begin(), libs.end());
}

const std::vector<std::string>& Target::get_linked_libraries(PropertyVisibility visibility) const {
    static const std::vector<std::string> empty;
    auto it = linked_libraries_.find(visibility);
    return (it != linked_libraries_.end()) ? it->second : empty;
}

void Target::add_include_directories(const std::vector<std::string>& dirs, PropertyVisibility visibility) {
    include_directories_[visibility].insert(include_directories_[visibility].end(), dirs.begin(), dirs.end());
}

const std::vector<std::string>& Target::get_include_directories(PropertyVisibility visibility) const {
    static const std::vector<std::string> empty;
    auto it = include_directories_.find(visibility);
    return (it != include_directories_.end()) ? it->second : empty;
}

void Target::add_link_directories(const std::vector<std::string>& dirs, PropertyVisibility visibility) {
    link_directories_[visibility].insert(link_directories_[visibility].end(), dirs.begin(), dirs.end());
}

const std::vector<std::string>& Target::get_link_directories(PropertyVisibility visibility) const {
    static const std::vector<std::string> empty;
    auto it = link_directories_.find(visibility);
    return (it != link_directories_.end()) ? it->second : empty;
}

void Target::add_compile_definitions(const std::vector<std::string>& defs, PropertyVisibility visibility) {
    compile_definitions_[visibility].insert(compile_definitions_[visibility].end(), defs.begin(), defs.end());
}

const std::vector<std::string>& Target::get_compile_definitions(PropertyVisibility visibility) const {
    static const std::vector<std::string> empty;
    auto it = compile_definitions_.find(visibility);
    return (it != compile_definitions_.end()) ? it->second : empty;
}

void Target::add_compile_options(const std::vector<std::string>& opts, PropertyVisibility visibility) {
    compile_options_[visibility].insert(compile_options_[visibility].end(), opts.begin(), opts.end());
}

const std::vector<std::string>& Target::get_compile_options(PropertyVisibility visibility) const {
    static const std::vector<std::string> empty;
    auto it = compile_options_.find(visibility);
    return (it != compile_options_.end()) ? it->second : empty;
}

void Target::add_precompiled_headers(const std::vector<std::string>& headers, PropertyVisibility visibility) {
    precompiled_headers_[visibility].insert(precompiled_headers_[visibility].end(), headers.begin(), headers.end());
}

const std::vector<std::string>& Target::get_precompiled_headers(PropertyVisibility visibility) const {
    static const std::vector<std::string> empty;
    auto it = precompiled_headers_.find(visibility);
    return (it != precompiled_headers_.end()) ? it->second : empty;
}

void Target::set_output_name(std::string output_name) {
    output_name_ = std::move(output_name);
}

const std::string& Target::get_output_name() const {
    return output_name_.empty() ? name_ : output_name_;
}

void Target::set_cxx_standard(std::string standard) {
    cxx_standard_ = std::move(standard);
}

const std::string& Target::get_cxx_standard() const {
    return cxx_standard_;
}

std::string Target::get_output_path() const {
    std::string out_name = get_output_name();
    std::filesystem::path path;

    if (type_ == TargetType::EXECUTABLE) {
        path = std::filesystem::path(binary_dir_) / out_name;
    } else if (type_ == TargetType::SHARED_LIBRARY) {
        path = std::filesystem::path(binary_dir_) / ("lib" + out_name + ".so");
    } else if (type_ == TargetType::STATIC_LIBRARY) {
        path = std::filesystem::path(binary_dir_) / ("lib" + out_name + ".a");
    } else {
        return ""; // OBJECT or INTERFACE might not have a single output path
    }

    return binary_dir_.empty() ? path.string() : path.lexically_normal().string();
}

static std::string get_obj_path(const std::string& binary_dir, const std::string& source_path) {
    std::filesystem::path src(source_path);
    std::filesystem::path obj_suffix;

    if (src.is_absolute()) {
        obj_suffix = src.filename();
    } else {
        obj_suffix = src;
    }

    std::filesystem::path obj = std::filesystem::path(binary_dir) / "objs" / obj_suffix;
    obj += ".o";
    return binary_dir.empty() ? obj.string() : obj.lexically_normal().string();
}

std::string Target::build_compile_command(const std::string& source, const std::string& output,
                                             const std::string& pch_include_arg, bool is_shared) const {
    std::ostringstream cmd;
    cmd << "g++";

    if (!cxx_standard_.empty()) {
        cmd << " -std=c++" << cxx_standard_;
    }

    if (isatty(STDOUT_FILENO)) cmd << " -fdiagnostics-color=always";

    for (const auto& opt : get_compile_options(PropertyVisibility::PRIVATE)) cmd << " " << opt;
    for (const auto& opt : get_compile_options(PropertyVisibility::PUBLIC)) cmd << " " << opt;

    for (const auto& def : get_compile_definitions(PropertyVisibility::PRIVATE)) cmd << " -D" << def;
    for (const auto& def : get_compile_definitions(PropertyVisibility::PUBLIC)) cmd << " -D" << def;

    cmd << " -MMD -MF " << output << ".d";

    if (is_shared) cmd << " -fPIC";

    cmd << " -c -o " << output;

    for (const auto& dir : get_include_directories(PropertyVisibility::PRIVATE))
        cmd << " -I" << (std::filesystem::path(source_dir_) / dir).string();
    for (const auto& dir : get_include_directories(PropertyVisibility::PUBLIC))
        cmd << " -I" << (std::filesystem::path(source_dir_) / dir).string();

    if (!pch_include_arg.empty()) {
        cmd << " -I" << source_dir_;
        cmd << pch_include_arg;
    }

    cmd << " " << source;

    return cmd.str();
}

void Target::generate_object_tasks(BuildGraph& graph, std::vector<std::string>& obj_files,
                                      const std::string& pch_gch_path, const std::string& pch_include_arg,
                                      bool is_shared) {
    for (const auto& src : get_sources(PropertyVisibility::PRIVATE)) {
        std::filesystem::path src_abs = std::filesystem::path(source_dir_) / src;
        std::string obj = get_obj_path(binary_dir_, src);
        obj_files.push_back(obj);

        BuildTask task;
        task.id = obj;
        task.parent_target = this;
        task.command = build_compile_command(src_abs.string(), obj, pch_include_arg, is_shared);
        task.inputs.push_back(src_abs.string());
        task.outputs.push_back(obj);
        task.outputs.push_back(obj + ".d");

        if (!pch_gch_path.empty()) {
            task.dependencies.insert(pch_gch_path);
        }

        graph.add_task(std::move(task));
    }
}

// Returns (pch_gch_path, pch_include_arg). Both empty if no PCH needed.
static std::pair<std::string, std::string> generate_pch_task(BuildGraph& graph, const Target* target, bool is_shared) {
    auto private_pchs = target->get_precompiled_headers(PropertyVisibility::PRIVATE);
    auto public_pchs = target->get_precompiled_headers(PropertyVisibility::PUBLIC);

    if (private_pchs.empty() && public_pchs.empty()) {
        return {"", ""};
    }

    auto pch_path = std::filesystem::path(target->get_binary_dir()) / "objs" / (target->get_name() + "_pch.hpp");
    std::string pch_wrapper = target->get_binary_dir().empty() ? pch_path.string() : pch_path.lexically_normal().string();
    std::string pch_gch_path = pch_wrapper + ".gch";
    std::string pch_include_arg = " -include " + pch_wrapper;

    std::ostringstream wrapper_content;
    for (const auto& hdr : private_pchs) wrapper_content << "#include \"" << hdr << "\"\n";
    for (const auto& hdr : public_pchs) wrapper_content << "#include \"" << hdr << "\"\n";
    std::string content = wrapper_content.str();

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

    BuildTask pch_task;
    pch_task.id = pch_gch_path;
    pch_task.parent_target = const_cast<Target*>(target);

    std::ostringstream cmd;
    cmd << "g++";

    if (!target->get_cxx_standard().empty()) {
        cmd << " -std=c++" << target->get_cxx_standard();
    }

    if (isatty(STDOUT_FILENO)) cmd << " -fdiagnostics-color=always";

    for (const auto& opt : target->get_compile_options(PropertyVisibility::PRIVATE)) cmd << " " << opt;
    for (const auto& opt : target->get_compile_options(PropertyVisibility::PUBLIC)) cmd << " " << opt;
    for (const auto& def : target->get_compile_definitions(PropertyVisibility::PRIVATE)) cmd << " -D" << def;
    for (const auto& def : target->get_compile_definitions(PropertyVisibility::PUBLIC)) cmd << " -D" << def;

    cmd << " -I" << target->get_source_dir();

    for (const auto& dir : target->get_include_directories(PropertyVisibility::PRIVATE))
        cmd << " -I" << (std::filesystem::path(target->get_source_dir()) / dir).string();
    for (const auto& dir : target->get_include_directories(PropertyVisibility::PUBLIC))
        cmd << " -I" << (std::filesystem::path(target->get_source_dir()) / dir).string();

    if (is_shared) cmd << " -fPIC";
    cmd << " -x c++-header -o " << pch_gch_path << " " << pch_wrapper;

    pch_task.command = cmd.str();
    pch_task.inputs.push_back(pch_wrapper);
    pch_task.outputs.push_back(pch_gch_path);

    graph.add_task(std::move(pch_task));

    return {pch_gch_path, pch_include_arg};
}

void Target::generate_tasks(BuildGraph& graph) {
    if (type_ == TargetType::INTERFACE_LIBRARY) return;

    std::vector<std::string> obj_files;
    bool is_shared = (type_ == TargetType::SHARED_LIBRARY);

    auto [pch_gch_path, pch_include_arg] = generate_pch_task(graph, this, is_shared);
    generate_object_tasks(graph, obj_files, pch_gch_path, pch_include_arg, is_shared);

    if (type_ == TargetType::OBJECT_LIBRARY) return;

    std::string output_path = get_output_path();
    BuildTask link;
    link.id = output_path;
    link.parent_target = this;

    std::ostringstream cmd;
    if (type_ == TargetType::STATIC_LIBRARY) {
        cmd << "ar rcs " << output_path;
    } else {
        cmd << "g++";
        if (!cxx_standard_.empty()) cmd << " -std=c++" << cxx_standard_;
        if (isatty(STDOUT_FILENO)) cmd << " -fdiagnostics-color=always";
        cmd << " -Wl,-rpath,'$ORIGIN'";
        if (is_shared) cmd << " -shared";
        cmd << " -o " << output_path;
    }

    for (const auto& obj : obj_files) {
        cmd << " " << obj;
        link.inputs.push_back(obj);
        link.dependencies.insert(obj);
    }
    
    if (type_ != TargetType::STATIC_LIBRARY) {
        cmd << " -L" << binary_dir_;
        for (const auto& dir : get_link_directories(PropertyVisibility::PRIVATE)) cmd << " -L" << dir;
        for (const auto& lib : get_linked_libraries(PropertyVisibility::PRIVATE)) cmd << " -l" << lib;
    }
    
    link.command = cmd.str();
    link.outputs.push_back(output_path);
    graph.add_task(std::move(link));
}

} // namespace dmake