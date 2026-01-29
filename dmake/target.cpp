#include "target.hpp"
#include "build_system.hpp"
#include "language.hpp"
#include "toolchain.hpp"
#include "module_scanner.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <algorithm>
#include <functional>
#include <set>

namespace dmake {

// --- Generic Property Implementation ---

void Target::set_property(const std::string& name, const std::string& value) {
    properties_[name] = value;
}

std::string Target::get_property(const std::string& name) const {
    if (properties_.contains(name)) {
        return properties_.at(name);
    }
    return "";
}

void Target::append_property(const std::string& name, const std::vector<std::string>& values, PropertyVisibility visibility) {
    auto& list = list_properties_[name][visibility];
    list.insert(list.end(), values.begin(), values.end());
}

const std::vector<std::string>& Target::get_property_list(const std::string& name, PropertyVisibility visibility) const {
    static const std::vector<std::string> empty;
    auto prop_it = list_properties_.find(name);
    if (prop_it == list_properties_.end()) return empty;

    auto vis_it = prop_it->second.find(visibility);
    return (vis_it != prop_it->second.end()) ? vis_it->second : empty;
}

const std::vector<std::string>& Target::get_resolved_property(const std::string& name) const {
    static const std::vector<std::string> empty;
    auto it = resolved_properties_.find(name);
    return (it != resolved_properties_.end()) ? it->second : empty;
}

const std::vector<std::string>& Target::get_resolved_interface_property(const std::string& name) const {
    static const std::vector<std::string> empty;
    auto it = resolved_interface_properties_.find(name);
    return (it != resolved_interface_properties_.end()) ? it->second : empty;
}

// --- File Set Support ---

void Target::add_file_set(FileSet file_set) {
    file_sets_.push_back(std::move(file_set));
}

bool Target::is_in_cxx_modules_file_set(const std::string& source) const {
    for (const auto& fs : file_sets_) {
        if (fs.type == "CXX_MODULES") {
            for (const auto& file : fs.files) {
                // Check if source matches (could be absolute or relative)
                std::filesystem::path src_path(source);
                std::filesystem::path fs_path(file);

                // Try direct match
                if (src_path == fs_path) return true;

                // Try filename match
                if (src_path.filename() == fs_path.filename()) return true;

                // Try resolved paths match
                std::filesystem::path src_abs = src_path.is_absolute() ?
                    src_path : (std::filesystem::path(source_dir_) / src_path);
                std::filesystem::path fs_abs = fs_path.is_absolute() ?
                    fs_path : (std::filesystem::path(source_dir_) / fs_path);

                if (src_abs.lexically_normal() == fs_abs.lexically_normal()) return true;
            }
        }
    }
    return false;
}

// --- Specific Property Helpers (Legacy Wrappers) ---

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

// --- Resolution Logic ---

void Target::resolve(const std::map<std::string, std::shared_ptr<Target>>& all_targets) {
    if (resolved_) return;
    if (visiting_) throw std::runtime_error("Circular dependency detected involving target: " + name_);
    visiting_ = true;

    auto resolve_path = [&](std::string p) -> std::string {
        if(p.starts_with("-I")) {
            p = p.substr(2);
        }
        std::filesystem::path path(p);
        if (path.is_absolute()) return path.string();
        return (std::filesystem::path(source_dir_) / path).lexically_normal().string();
    };

    auto merge = [](std::vector<std::string>& target, const std::vector<std::string>& source) {
        target.insert(target.end(), source.begin(), source.end());
    };

    // Properties to resolve
    // "SOURCES" is purposefully excluded as it is not transitive in the same way
    struct PropInfo {
        std::string name;
        bool is_path;
    };
    std::vector<PropInfo> props_to_resolve = {
        {"INCLUDE_DIRECTORIES", true},
        {"COMPILE_DEFINITIONS", false},
        {"COMPILE_OPTIONS", false},
        {"LINK_DIRECTORIES", true},
        {"PRECOMPILE_HEADERS", false} // Note: PCH logic might need specialized handling, but we carry it for now
    };

    // 1. Initialize with local properties
    for (const auto& info : props_to_resolve) {
        auto& res = resolved_properties_[info.name];
        auto& res_iface = resolved_interface_properties_[info.name];

        auto process_local = [&](PropertyVisibility vis, std::vector<std::string>& out) {
            const auto& val = get_property_list(info.name, vis);
            if (info.is_path) {
                for(const auto& p : val) out.push_back(resolve_path(p));
            } else {
                merge(out, val);
            }
        };

        // Self properties: PRIVATE + PUBLIC
        process_local(PropertyVisibility::PRIVATE, res);
        process_local(PropertyVisibility::PUBLIC, res);

        // Interface properties: PUBLIC + INTERFACE
        process_local(PropertyVisibility::PUBLIC, res_iface);
        process_local(PropertyVisibility::INTERFACE, res_iface);
    }

    // Special handling for LINK_LIBRARIES (order matters, so we do it manually/carefully)
    auto& res_libs = resolved_properties_["LINK_LIBRARIES"];
    auto& res_iface_libs = resolved_interface_properties_["LINK_LIBRARIES"];

    // 2. Process Dependencies
    auto process_dependency = [&](const std::string& lib_name, bool is_public, bool is_interface_only) {
        if (all_targets.count(lib_name)) {
            auto dep = all_targets.at(lib_name);
            dep->resolve(all_targets);

            // Inherit for building THIS target (from dep's INTERFACE)
            if (!is_interface_only) {
                for (const auto& info : props_to_resolve) {
                    merge(resolved_properties_[info.name], dep->get_resolved_interface_property(info.name));
                }

                std::string dep_path = dep->get_output_path();
                if (!dep_path.empty()) res_libs.push_back(dep_path);
                merge(res_libs, dep->get_resolved_interface_property("LINK_LIBRARIES"));
            }

            // Propagate to Dependents (from dep's INTERFACE)
            if (is_public || is_interface_only) {
                for (const auto& info : props_to_resolve) {
                    merge(resolved_interface_properties_[info.name], dep->get_resolved_interface_property(info.name));
                }

                std::string dep_path = dep->get_output_path();
                if (!dep_path.empty()) res_iface_libs.push_back(dep_path);
                merge(res_iface_libs, dep->get_resolved_interface_property("LINK_LIBRARIES"));
            }
        } else {
             // System library or raw file
             if (!is_interface_only) res_libs.push_back(lib_name);
             if (is_public || is_interface_only) res_iface_libs.push_back(lib_name);
        }
    };

    // Note: LINK_LIBRARIES is stored in the generic map too
    for (const auto& lib : get_property_list("LINK_LIBRARIES", PropertyVisibility::PUBLIC)) process_dependency(lib, true, false);
    for (const auto& lib : get_property_list("LINK_LIBRARIES", PropertyVisibility::PRIVATE)) process_dependency(lib, false, false);
    for (const auto& lib : get_property_list("LINK_LIBRARIES", PropertyVisibility::INTERFACE)) process_dependency(lib, false, true);

    visiting_ = false;
    resolved_ = true;
}

// --- Task Generation ---

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
        return "";
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

void Target::generate_object_tasks(BuildGraph& graph, const Toolchain& toolchain, std::vector<std::string>& obj_files,
                                      const std::string& pch_gch_path, const std::string& pch_include_arg,
                                      bool is_shared, const std::map<std::string, std::shared_ptr<Target>>& all_targets) {
    // Check if this target has any module sources (for module mapper path)
    bool target_has_modules = has_module_sources();

    // SOURCES are essentially PRIVATE
    for (const auto& src : get_property_list("SOURCES", PropertyVisibility::PRIVATE)) {
        auto lang_info = LanguageClassifier::from_path(src);
        if (lang_info.is_header) continue;
        if (lang_info.lang == Language::UNKNOWN) {
            // throw std::runtime_error("No compiler registered for file '" + src + "' in target '" + name_ + "'");
            // CMake ignores unknown files
            continue;
        }

        // Override module interface detection if file is in CXX_MODULES file set
        if (is_in_cxx_modules_file_set(src)) {
            lang_info.is_module_interface = true;
        }

        const Compiler* compiler = toolchain.get_compiler_ptr(lang_info.lang);
        if (!compiler) {
            throw std::runtime_error("No compiler available for language " + std::string(lang_info.name) + " in target '" + name_ + "'");
            continue;
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

        // C++20 modules: if this is a module interface or target has modules,
        // enable module support in compilation
        if (lang_info.is_module_interface || target_has_modules) {
            ctx.is_module_source = true;
            // Module mapper will be written by the collator task
            ctx.module_mapper_file = get_module_mapper_path();
        }

        for (const auto& opt : get_language_flags(lang_info.lang)) ctx.options.push_back(opt);

        ctx.includes.push_back(source_dir_);
        for (const auto& dir : get_resolved_property("INCLUDE_DIRECTORIES")) ctx.includes.push_back(dir);

        for (const auto& def : get_resolved_property("COMPILE_DEFINITIONS")) ctx.definitions.push_back(def);
        for (const auto& opt : get_resolved_property("COMPILE_OPTIONS")) ctx.options.push_back(opt);

        BuildTask task;
        task.id = obj;
        task.parent_target = this;
        task.commands.push_back(compiler->get_compile_command(ctx));
        task.inputs.push_back(src_abs.string());
        task.outputs.push_back(obj);
        task.outputs.push_back(obj + ".d");
        task.is_compilation = true;
        task.source_file = src_abs.string();

        // Mark as module source if it's a module interface file
        if (lang_info.is_module_interface) {
            task.is_module_source = true;
        }

        if (!pch_gch_path.empty()) {
            task.dependencies.insert(pch_gch_path);
            task.inputs.push_back(pch_gch_path);
        }

        // Add manually added dependencies to compile tasks
        // This ensures custom targets that generate headers run before compilation
        for (const auto& dep_name : manually_added_dependencies_) {
            if (all_targets.count(dep_name)) {
                auto dep_target = all_targets.at(dep_name);
                std::string dep_out = dep_target->get_output_path();
                if (!dep_out.empty()) {
                    task.dependencies.insert(dep_out);
                } else {
                    // Utility target - depend on the target name itself
                    task.dependencies.insert(dep_name);
                }
            }
        }

        graph.add_task(std::move(task));
    }
}

static std::pair<std::string, std::string> generate_pch_task(
    BuildGraph& graph,
    const Toolchain& toolchain,
    const Target* target,
    bool is_shared,
    const std::vector<std::string>& includes,
    const std::vector<std::string>& definitions,
    const std::vector<std::string>& options) {

    // Using PCH property name "PRECOMPILE_HEADERS"
    auto private_pchs = target->get_property_list("PRECOMPILE_HEADERS", PropertyVisibility::PRIVATE);
    auto public_pchs = target->get_property_list("PRECOMPILE_HEADERS", PropertyVisibility::PUBLIC);

    if (private_pchs.empty() && public_pchs.empty()) {
        return {"", ""};
    }

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
    ctx.options.push_back("-x");
    ctx.options.push_back("c++-header");

    for (const auto& opt : target->get_language_flags(pch_lang)) ctx.options.push_back(opt);
    for (const auto& opt : options) ctx.options.push_back(opt);
    for (const auto& def : definitions) ctx.definitions.push_back(def);

    ctx.includes.push_back(target->get_source_dir());
    for (const auto& dir : includes)
        ctx.includes.push_back(dir);

    pch_task.commands.push_back(compiler->get_compile_command(ctx));
    pch_task.inputs.push_back(pch_wrapper);
    pch_task.is_compilation = true;
    pch_task.source_file = pch_wrapper;

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

    // C++20 modules: generate scanner tasks first (they have no dependencies)
    bool has_modules = generate_module_scanner_tasks(graph, toolchain);

    auto [pch_gch_path, pch_include_arg] = generate_pch_task(graph, toolchain, this, is_shared,
        get_resolved_property("INCLUDE_DIRECTORIES"),
        get_resolved_property("COMPILE_DEFINITIONS"),
        get_resolved_property("COMPILE_OPTIONS"));

    generate_object_tasks(graph, toolchain, obj_files, pch_gch_path, pch_include_arg, is_shared, all_targets);

    // If we have modules, compile tasks need to depend on the collator
    if (has_modules) {
        std::string mapper_path = get_module_mapper_path();
        for (const auto& obj : obj_files) {
            if (graph.has_task(obj)) {
                auto& task = graph.get_task(obj);
                task.dependencies.insert(mapper_path);
                task.inputs.push_back(mapper_path);
            }
        }
    }

    if (type_ == TargetType::OBJECT_LIBRARY) return;

    Language linker_lang = Language::C;
    for (const auto& src : get_property_list("SOURCES", PropertyVisibility::PRIVATE)) {
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
        for (const auto& lib : get_resolved_property("LINK_LIBRARIES")) {
             if (lib.starts_with("/") || lib.starts_with("./") || lib.starts_with("../") ||
                 lib.find(".so") != std::string::npos ||
                 lib.find(".a") != std::string::npos) {
                 ctx.objects.push_back(lib);
             } else {
                 ctx.libs.push_back(lib);
             }
        }

        for (const auto& dir : get_resolved_property("LINK_DIRECTORIES")) {
            ctx.lib_dirs.push_back(dir);
        }

        link.commands.push_back(linker->get_link_command(ctx));
    }

    for (const auto& obj : obj_files) {
        link.inputs.push_back(obj);
        link.dependencies.insert(obj);
    }

    for (const auto& lib : get_resolved_property("LINK_LIBRARIES")) {
         if (lib.starts_with("/") || lib.starts_with("./") || lib.starts_with("../")) {
             link.inputs.push_back(lib);
             link.dependencies.insert(lib);
         }
    }

    // Add manually added dependencies (from add_dependencies command)
    for (const auto& dep_name : manually_added_dependencies_) {
        if (all_targets.count(dep_name)) {
            auto dep_target = all_targets.at(dep_name);
            std::string dep_out = dep_target->get_output_path();
            if (!dep_out.empty()) {
                link.dependencies.insert(dep_out);
            } else {
                // Utility target - depend on the target name itself
                link.dependencies.insert(dep_name);
            }
        }
    }

    link.outputs.push_back(output_path);
    graph.add_task(std::move(link));
}

void CustomTarget::generate_tasks(BuildGraph& graph, const Toolchain&, const std::map<std::string, std::shared_ptr<Target>>& all_targets, const std::vector<std::string>&, const std::vector<std::string>&) {
    BuildTask task;
    task.id = name_;
    task.parent_target = this;
    task.always_run = true;
    task.working_dir = binary_dir_;

    for (const auto& custom_cmd : custom_commands_) {
        task.commands.push_back(custom_cmd.command);
        if (!custom_cmd.working_dir.empty()) {
            task.working_dir = custom_cmd.working_dir;
        }
    }

    // Handle DEPENDS from add_custom_target
    for (const auto& dep_name : custom_depends_) {
        if (all_targets.count(dep_name)) {
            auto dep_target = all_targets.at(dep_name);
            std::string dep_out = dep_target->get_output_path();
            if (!dep_out.empty()) {
                task.dependencies.insert(dep_out);
                task.inputs.push_back(dep_out);
            } else {
                task.dependencies.insert(dep_name);
            }
        } else {
            std::filesystem::path p(dep_name);
            if (!p.is_absolute()) p = std::filesystem::path(source_dir_) / p;
            task.inputs.push_back(p.string());
        }
    }

    // Handle manually added dependencies (from add_dependencies command)
    for (const auto& dep_name : manually_added_dependencies_) {
        if (all_targets.count(dep_name)) {
            auto dep_target = all_targets.at(dep_name);
            std::string dep_out = dep_target->get_output_path();
            if (!dep_out.empty()) {
                task.dependencies.insert(dep_out);
            } else {
                task.dependencies.insert(dep_name);
            }
        }
    }

    // Support SOURCES in custom targets just in case
    for (const auto& src : get_property_list("SOURCES", PropertyVisibility::PRIVATE)) {
         std::filesystem::path p(src);
         if (!p.is_absolute()) p = std::filesystem::path(source_dir_) / p;
         task.inputs.push_back(p.string());
    }

    graph.add_task(std::move(task));
}

// --- C++20 Modules Support ---

std::string Target::get_module_mapper_path() const {
    return (std::filesystem::path(binary_dir_) / (name_ + ".module-mapper")).lexically_normal().string();
}

bool Target::has_module_sources() const {
    if (modules_detected_) return has_modules_;

    modules_detected_ = true;
    has_modules_ = false;

    // Check regular SOURCES for module interface files by extension
    for (const auto& src : get_property_list("SOURCES", PropertyVisibility::PRIVATE)) {
        auto lang_info = LanguageClassifier::from_path(src);
        if (lang_info.is_module_interface) {
            has_modules_ = true;
            return true;
        }
    }

    // Check CXX_MODULES file sets
    for (const auto& fs : file_sets_) {
        if (fs.type == "CXX_MODULES" && !fs.files.empty()) {
            has_modules_ = true;
            return true;
        }
    }

    return false;
}

bool Target::generate_module_scanner_tasks(BuildGraph& graph, const Toolchain& toolchain) {
    std::vector<std::string> scanner_ids;

    for (const auto& src : get_property_list("SOURCES", PropertyVisibility::PRIVATE)) {
        auto lang_info = LanguageClassifier::from_path(src);

        // Override module interface detection if file is in CXX_MODULES file set
        if (is_in_cxx_modules_file_set(src)) {
            lang_info.is_module_interface = true;
        }

        // Only scan module interface files (*.ixx, *.cppm, etc.)
        // Regular .cpp files that might import modules will have their
        // dependencies resolved through the collator
        if (!lang_info.is_module_interface) continue;
        if (lang_info.lang != Language::CXX) continue;

        const Compiler* compiler = toolchain.get_compiler_ptr(lang_info.lang);
        if (!compiler) continue;

        std::filesystem::path src_abs = std::filesystem::path(source_dir_) / src;
        std::string ddi_path = get_ddi_path(binary_dir_, src);

        ModuleScanContext ctx;
        ctx.source = src_abs.string();
        ctx.output = ddi_path;
        ctx.standard = get_language_standard(lang_info.lang);
        ctx.color_diagnostics = isatty(STDOUT_FILENO);

        ctx.includes.push_back(source_dir_);
        for (const auto& dir : get_resolved_property("INCLUDE_DIRECTORIES")) {
            ctx.includes.push_back(dir);
        }
        for (const auto& def : get_resolved_property("COMPILE_DEFINITIONS")) {
            ctx.definitions.push_back(def);
        }

        BuildTask scanner;
        scanner.id = ddi_path;
        scanner.parent_target = this;
        scanner.commands.push_back(compiler->get_module_scan_command(ctx));
        scanner.inputs.push_back(src_abs.string());
        scanner.outputs.push_back(ddi_path);
        scanner.is_module_scanner = true;
        scanner.source_file = src_abs.string();

        graph.add_task(std::move(scanner));
        scanner_ids.push_back(ddi_path);
    }

    if (!scanner_ids.empty()) {
        generate_module_collator_task(graph, scanner_ids);
        return true;
    }

    return false;
}

void Target::generate_module_collator_task(BuildGraph& graph, const std::vector<std::string>& scanner_task_ids) {
    std::string mapper_path = get_module_mapper_path();

    BuildTask collator;
    collator.id = mapper_path;
    collator.parent_target = this;
    collator.is_module_collator = true;

    // Collator depends on all scanner tasks
    for (const auto& scanner_id : scanner_task_ids) {
        collator.dependencies.insert(scanner_id);
        collator.inputs.push_back(scanner_id);
    }

    collator.outputs.push_back(mapper_path);

    // Collator has no commands - it's executed in-process by the build graph
    // The actual work happens in BuildGraph::execute() when it detects a collator task

    graph.add_task(std::move(collator));
}

} // namespace dmake
