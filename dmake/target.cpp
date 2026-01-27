#include "target.hpp"
#include "build_system.hpp"
#include "language.hpp"
#include "toolchain.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <algorithm>
#include <functional>
#include <set>

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

void Target::set_language_standard(Language lang, std::string standard) {
    standards_[lang] = std::move(standard);
}

const std::string& Target::get_language_standard(Language lang) const {
    static const std::string empty;
    auto it = standards_.find(lang);
    return (it != standards_.end()) ? it->second : empty;
    return standards_.at(lang);
}

void Target::set_property(const std::string& name, std::string value) {
    properties_[name] = std::move(value);
}

std::string Target::get_property(const std::string& name) const {
    if (properties_.contains(name)) {
        return properties_.at(name);
    }
    return "";
}

void Target::add_language_flags(Language lang, const std::vector<std::string>& flags) {
    auto& target_flags = language_flags_[lang];
    target_flags.insert(target_flags.end(), flags.begin(), flags.end());
}

const std::vector<std::string>& Target::get_language_flags(Language lang) const {
    static const std::vector<std::string> empty;
    auto it = language_flags_.find(lang);
    return (it != language_flags_.end()) ? it->second : empty;
}

std::string Target::get_output_path() const {
    if (is_imported_ && !imported_location_.empty()) {
        return imported_location_;
    }

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

void Target::resolve(const std::map<std::string, std::shared_ptr<Target>>& all_targets) {
    if (resolved_) return;
    if (visiting_) throw std::runtime_error("Circular dependency detected involving target: " + name_);
    visiting_ = true;

    auto resolve_path = [&](const std::string& p) -> std::string {
        std::filesystem::path path(p);
        if (path.is_absolute()) return path.string();
        return (std::filesystem::path(source_dir_) / path).lexically_normal().string();
    };

    auto merge = [](std::vector<std::string>& a, const std::vector<std::string>& b) {
        a.insert(a.end(), b.begin(), b.end());
    };
    
    // Merge includes and resolve paths immediately
    auto merge_resolved_paths = [&](std::vector<std::string>& a, const std::vector<std::string>& b) {
        for(const auto& p : b) a.push_back(resolve_path(p));
    };

    // 1. Initialize with local properties
    
    // Includes (resolve to absolute)
    merge_resolved_paths(resolved_includes_, get_include_directories(PropertyVisibility::PRIVATE));
    merge_resolved_paths(resolved_includes_, get_include_directories(PropertyVisibility::PUBLIC));
    
    merge_resolved_paths(resolved_interface_includes_, get_include_directories(PropertyVisibility::PUBLIC));
    merge_resolved_paths(resolved_interface_includes_, get_include_directories(PropertyVisibility::INTERFACE));

    // Definitions
    merge(resolved_compile_definitions_, get_compile_definitions(PropertyVisibility::PRIVATE));
    merge(resolved_compile_definitions_, get_compile_definitions(PropertyVisibility::PUBLIC));
    
    merge(resolved_interface_compile_definitions_, get_compile_definitions(PropertyVisibility::PUBLIC));
    merge(resolved_interface_compile_definitions_, get_compile_definitions(PropertyVisibility::INTERFACE));

    // Options
    merge(resolved_compile_options_, get_compile_options(PropertyVisibility::PRIVATE));
    merge(resolved_compile_options_, get_compile_options(PropertyVisibility::PUBLIC));

    merge(resolved_interface_compile_options_, get_compile_options(PropertyVisibility::PUBLIC));
    merge(resolved_interface_compile_options_, get_compile_options(PropertyVisibility::INTERFACE));

    // Link Directories (resolve to absolute)
    merge_resolved_paths(resolved_link_directories_, get_link_directories(PropertyVisibility::PRIVATE));
    merge_resolved_paths(resolved_link_directories_, get_link_directories(PropertyVisibility::PUBLIC));
    
    merge_resolved_paths(resolved_interface_link_directories_, get_link_directories(PropertyVisibility::PUBLIC));
    merge_resolved_paths(resolved_interface_link_directories_, get_link_directories(PropertyVisibility::INTERFACE));

    // 2. Process Dependencies
    auto process_dependency = [&](const std::string& lib_name, bool is_public, bool is_interface_only) {
        if (all_targets.count(lib_name)) {
            auto dep = all_targets.at(lib_name);
            dep->resolve(all_targets);

            // Inherit for building THIS target
            if (!is_interface_only) {
                merge(resolved_includes_, dep->resolved_interface_includes_);
                merge(resolved_compile_definitions_, dep->resolved_interface_compile_definitions_);
                merge(resolved_compile_options_, dep->resolved_interface_compile_options_);
                merge(resolved_link_directories_, dep->resolved_interface_link_directories_);
                
                std::string dep_path = dep->get_output_path();
                if (!dep_path.empty()) resolved_link_libraries_.push_back(dep_path);
                merge(resolved_link_libraries_, dep->resolved_interface_link_libraries_);
            }

            // Propagate to Dependents
            if (is_public || is_interface_only) {
                merge(resolved_interface_includes_, dep->resolved_interface_includes_);
                merge(resolved_interface_compile_definitions_, dep->resolved_interface_compile_definitions_);
                merge(resolved_interface_compile_options_, dep->resolved_interface_compile_options_);
                merge(resolved_interface_link_directories_, dep->resolved_interface_link_directories_);
                
                std::string dep_path = dep->get_output_path();
                if (!dep_path.empty()) resolved_interface_link_libraries_.push_back(dep_path);
                merge(resolved_interface_link_libraries_, dep->resolved_interface_link_libraries_);
            }
        } else {
             if (!is_interface_only) resolved_link_libraries_.push_back(lib_name);
             if (is_public || is_interface_only) resolved_interface_link_libraries_.push_back(lib_name);
        }
    };

    for (const auto& lib : get_linked_libraries(PropertyVisibility::PUBLIC)) process_dependency(lib, true, false);
    for (const auto& lib : get_linked_libraries(PropertyVisibility::PRIVATE)) process_dependency(lib, false, false);
    for (const auto& lib : get_linked_libraries(PropertyVisibility::INTERFACE)) process_dependency(lib, false, true);

    visiting_ = false;
    resolved_ = true;
}

void Target::generate_object_tasks(BuildGraph& graph, const Toolchain& toolchain, std::vector<std::string>& obj_files,
                                      const std::string& pch_gch_path, const std::string& pch_include_arg,
                                      bool is_shared) {
    for (const auto& src : get_sources(PropertyVisibility::PRIVATE)) {
        auto lang_info = LanguageClassifier::from_path(src);
        if (lang_info.is_header) continue;
        if (lang_info.lang == Language::UNKNOWN) {
            throw std::runtime_error("No compiler registered for file '" + src + "' in target '" + name_ + "'");
        }

        const Compiler* compiler = toolchain.get_compiler_ptr(lang_info.lang);
        if (!compiler) {
            throw std::runtime_error("No compiler available for language " + std::string(lang_info.name) + " in target '" + name_ + "'");
        }

        std::filesystem::path src_abs = std::filesystem::path(source_dir_) / src;
        std::string obj = get_obj_path(binary_dir_, src);
        obj_files.push_back(obj);

        CompileContext ctx;
        ctx.source = src_abs.string();
        ctx.output = obj;
        ctx.is_shared = is_shared;
        ctx.pch_include = pch_include_arg;
        ctx.standard = get_language_standard(lang_info.lang);
        ctx.color_diagnostics = isatty(STDOUT_FILENO);

        // Language-specific global flags (from CMAKE_<LANG>_FLAGS)
        for (const auto& opt : get_language_flags(lang_info.lang)) ctx.options.push_back(opt);

        // Source directory must be in include path for PCH wrapper resolution
        ctx.includes.push_back(source_dir_);
        // Use resolved includes (which are absolute)
        for (const auto& dir : resolved_includes_) ctx.includes.push_back(dir);

        for (const auto& def : resolved_compile_definitions_) ctx.definitions.push_back(def);
        for (const auto& opt : resolved_compile_options_) ctx.options.push_back(opt);

        BuildTask task;
        task.id = obj;
        task.parent_target = this;
        task.commands.push_back(compiler->get_compile_command(ctx));
        task.inputs.push_back(src_abs.string());
        task.outputs.push_back(obj);
        task.outputs.push_back(obj + ".d");
        task.is_compilation = true;
        task.source_file = src_abs.string();

        if (!pch_gch_path.empty()) {
            task.dependencies.insert(pch_gch_path);
            // PCH must also be an input so changes to PCH trigger recompilation
            task.inputs.push_back(pch_gch_path);
        }

        graph.add_task(std::move(task));
    }
}

// Returns (pch_gch_path, pch_include_arg). Both empty if no PCH needed.
static std::pair<std::string, std::string> generate_pch_task(
    BuildGraph& graph, 
    const Toolchain& toolchain, 
    const Target* target, 
    bool is_shared,
    const std::vector<std::string>& includes,
    const std::vector<std::string>& definitions,
    const std::vector<std::string>& options) {

    auto private_pchs = target->get_precompiled_headers(PropertyVisibility::PRIVATE);
    auto public_pchs = target->get_precompiled_headers(PropertyVisibility::PUBLIC);

    if (private_pchs.empty() && public_pchs.empty()) {
        return {"", ""};
    }

    // PCH is usually C++ in dmake for now, but we should eventually detect language
    Language pch_lang = Language::CXX; 
    const Compiler* compiler = toolchain.get_compiler_ptr(pch_lang);
    if (!compiler) {
        throw std::runtime_error("No compiler available for PCH generation in target '" + target->get_name() + "'");
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

    CompileContext ctx;
    ctx.source = pch_wrapper;
    ctx.output = pch_gch_path;
    ctx.is_shared = is_shared;
    ctx.standard = target->get_language_standard(pch_lang);
    ctx.color_diagnostics = isatty(STDOUT_FILENO);
    ctx.options.push_back("-x"); // Force header type
    ctx.options.push_back("c++-header");

    // PCH should also get global CXX flags
    for (const auto& opt : target->get_language_flags(pch_lang)) ctx.options.push_back(opt);
    
    // Use resolved properties
    for (const auto& opt : options) ctx.options.push_back(opt);
    for (const auto& def : definitions) ctx.definitions.push_back(def);

    ctx.includes.push_back(target->get_source_dir());
    for (const auto& dir : includes)
        ctx.includes.push_back(dir);

    pch_task.commands.push_back(compiler->get_compile_command(ctx));
    pch_task.inputs.push_back(pch_wrapper);
    pch_task.is_compilation = true;
    pch_task.source_file = pch_wrapper;

    // Add the actual header files as inputs so PCH rebuilds when they change
    for (const auto& hdr : private_pchs) {
        auto hdr_abs = std::filesystem::path(hdr).is_absolute() ?
            std::filesystem::path(hdr) :
            std::filesystem::path(target->get_source_dir()) / hdr;
        pch_task.inputs.push_back(hdr_abs.lexically_normal().string());
    }
    for (const auto& hdr : public_pchs) {
        auto hdr_abs = std::filesystem::path(hdr).is_absolute() ?
            std::filesystem::path(hdr) :
            std::filesystem::path(target->get_source_dir()) / hdr;
        pch_task.inputs.push_back(hdr_abs.lexically_normal().string());
    }

    pch_task.outputs.push_back(pch_gch_path);

    graph.add_task(std::move(pch_task));

    return {pch_gch_path, pch_include_arg};
}

void Target::generate_tasks(BuildGraph& graph, const Toolchain& toolchain, const std::map<std::string, std::shared_ptr<Target>>& all_targets, const std::vector<std::string>& exe_linker_flags, const std::vector<std::string>& shared_linker_flags) {
    if (type_ == TargetType::INTERFACE_LIBRARY || is_imported_) return;

    resolve(all_targets);

    std::vector<std::string> obj_files;
    bool is_shared = (type_ == TargetType::SHARED_LIBRARY);

    auto [pch_gch_path, pch_include_arg] = generate_pch_task(graph, toolchain, this, is_shared, resolved_includes_, resolved_compile_definitions_, resolved_compile_options_);
    generate_object_tasks(graph, toolchain, obj_files, pch_gch_path, pch_include_arg, is_shared);

    if (type_ == TargetType::OBJECT_LIBRARY) return;

    // Determine linker language (CXX if any CXX sources, otherwise C)
    Language linker_lang = Language::C;
    for (const auto& src : get_sources(PropertyVisibility::PRIVATE)) {
        if (LanguageClassifier::from_path(src).lang == Language::CXX) {
            linker_lang = Language::CXX;
            break;
        }
    }

    const Compiler* linker = toolchain.get_compiler_ptr(linker_lang);
    if (!linker) {
        throw std::runtime_error("No linker available for language in target '" + name_ + "'");
    }

    std::string output_path = get_output_path();
    BuildTask link;
    link.id = output_path;
    link.parent_target = this;

    if (type_ == TargetType::STATIC_LIBRARY) {
        link.commands.push_back(linker->get_archive_command(output_path, obj_files));
    } else {
        LinkContext ctx;
        ctx.output = output_path;
        ctx.objects = obj_files;
        ctx.is_shared = is_shared;
        ctx.standard = get_language_standard(linker_lang);
        ctx.color_diagnostics = isatty(STDOUT_FILENO);
        ctx.linker_flags = is_shared ? shared_linker_flags : exe_linker_flags;

        // Use pre-resolved link libraries
        for (const auto& lib : resolved_link_libraries_) {
             // Heuristic to detect file paths vs system libs
             if (lib.starts_with("/") || lib.starts_with("./") || lib.starts_with("../") ||
                 lib.find(".so") != std::string::npos ||
                 lib.find(".a") != std::string::npos) {
                 ctx.objects.push_back(lib);
             } else {
                 ctx.libs.push_back(lib);
             }
        }

        // Add explicit link directories
        for (const auto& dir : resolved_link_directories_) {
            ctx.lib_dirs.push_back(dir);
        }

        link.commands.push_back(linker->get_link_command(ctx));
    }

    for (const auto& obj : obj_files) {
        link.inputs.push_back(obj);
        link.dependencies.insert(obj);
    }
    
    // Add dependencies on linked library files
    for (const auto& lib : resolved_link_libraries_) {
         if (lib.starts_with("/") || lib.starts_with("./") || lib.starts_with("../")) {
             link.inputs.push_back(lib);
             link.dependencies.insert(lib);
         }
    }

    link.outputs.push_back(output_path);
    graph.add_task(std::move(link));
}

void CustomTarget::generate_tasks(BuildGraph& graph, const Toolchain&, const std::map<std::string, std::shared_ptr<Target>>& all_targets, const std::vector<std::string>&, const std::vector<std::string>&) {
    BuildTask task;
    task.id = name_; // Target name is the task ID
    task.parent_target = this;
    task.always_run = true; // add_custom_target without outputs always runs
    task.working_dir = binary_dir_;

    for (const auto& custom_cmd : custom_commands_) {
        task.commands.push_back(custom_cmd.command);
        if (!custom_cmd.working_dir.empty()) {
            task.working_dir = custom_cmd.working_dir;
        }
    }

    // Dependencies
    for (const auto& dep_name : custom_depends_) {
        if (all_targets.count(dep_name)) {
            auto dep_target = all_targets.at(dep_name);
            std::string dep_out = dep_target->get_output_path();
            if (!dep_out.empty()) {
                task.dependencies.insert(dep_out);
                task.inputs.push_back(dep_out);
            } else {
                // Dependency on another custom target (utility)
                task.dependencies.insert(dep_name);
            }
        } else {
            // Assume it's a file dependency
            std::filesystem::path p(dep_name);
            if (!p.is_absolute()) p = std::filesystem::path(source_dir_) / p;
            task.inputs.push_back(p.string());
        }
    }

    graph.add_task(std::move(task));
}

} // namespace dmake
