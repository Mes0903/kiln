#include "target.hpp"
#include "build_system.hpp"
#include "language.hpp"
#include "toolchain.hpp"
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
        for (const auto& dir : get_include_directories(PropertyVisibility::PRIVATE))
            ctx.includes.push_back((std::filesystem::path(source_dir_) / dir).string());
        for (const auto& dir : get_include_directories(PropertyVisibility::PUBLIC))
            ctx.includes.push_back((std::filesystem::path(source_dir_) / dir).string());

        for (const auto& def : get_compile_definitions(PropertyVisibility::PRIVATE)) ctx.definitions.push_back(def);
        for (const auto& def : get_compile_definitions(PropertyVisibility::PUBLIC)) ctx.definitions.push_back(def);

        for (const auto& opt : get_compile_options(PropertyVisibility::PRIVATE)) ctx.options.push_back(opt);
        for (const auto& opt : get_compile_options(PropertyVisibility::PUBLIC)) ctx.options.push_back(opt);

        BuildTask task;
        task.id = obj;
        task.parent_target = this;
        task.command = compiler->get_compile_command(ctx);
        task.inputs.push_back(src_abs.string());
        task.outputs.push_back(obj);
        task.outputs.push_back(obj + ".d");

        if (!pch_gch_path.empty()) {
            task.dependencies.insert(pch_gch_path);
            // PCH must also be an input so changes to PCH trigger recompilation
            task.inputs.push_back(pch_gch_path);
        }

        graph.add_task(std::move(task));
    }
}

// Returns (pch_gch_path, pch_include_arg). Both empty if no PCH needed.
static std::pair<std::string, std::string> generate_pch_task(BuildGraph& graph, const Toolchain& toolchain, const Target* target, bool is_shared) {
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
    ctx.options.push_back("-x c++-header"); // Force header type

    // PCH should also get global CXX flags
    for (const auto& opt : target->get_language_flags(pch_lang)) ctx.options.push_back(opt);
    for (const auto& opt : target->get_compile_options(PropertyVisibility::PUBLIC)) ctx.options.push_back(opt);
    for (const auto& def : target->get_compile_definitions(PropertyVisibility::PRIVATE)) ctx.definitions.push_back(def);
    for (const auto& def : target->get_compile_definitions(PropertyVisibility::PUBLIC)) ctx.definitions.push_back(def);

    ctx.includes.push_back(target->get_source_dir());
    for (const auto& dir : target->get_include_directories(PropertyVisibility::PRIVATE))
        ctx.includes.push_back((std::filesystem::path(target->get_source_dir()) / dir).string());
    for (const auto& dir : target->get_include_directories(PropertyVisibility::PUBLIC))
        ctx.includes.push_back((std::filesystem::path(target->get_source_dir()) / dir).string());

    pch_task.command = compiler->get_compile_command(ctx);
    pch_task.inputs.push_back(pch_wrapper);

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

void Target::generate_tasks(BuildGraph& graph, const Toolchain& toolchain, const std::map<std::string, std::shared_ptr<Target>>& all_targets) {
    if (type_ == TargetType::INTERFACE_LIBRARY || is_imported_) return;

    std::vector<std::string> obj_files;
    bool is_shared = (type_ == TargetType::SHARED_LIBRARY);

    auto [pch_gch_path, pch_include_arg] = generate_pch_task(graph, toolchain, this, is_shared);
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
        link.command = linker->get_archive_command(output_path, obj_files);
    } else {
        LinkContext ctx;
        ctx.output = output_path;
        ctx.objects = obj_files;
        ctx.is_shared = is_shared;
        ctx.standard = get_language_standard(linker_lang);
        ctx.color_diagnostics = isatty(STDOUT_FILENO);

        // Resolve library names: if it's a known target, use direct path; otherwise use -l
        for (const auto& lib : get_linked_libraries(PropertyVisibility::PRIVATE)) {
            auto it = all_targets.find(lib);
            if (it != all_targets.end()) {
                // It's a target - link directly to the library file
                ctx.objects.push_back(it->second->get_output_path());
            } else {
                // It's a system library or explicit -l name
                ctx.libs.push_back(lib);
            }
        }

        // Add explicit link directories
        for (const auto& dir : get_link_directories(PropertyVisibility::PRIVATE)) {
            ctx.lib_dirs.push_back(dir);
        }

        link.command = linker->get_link_command(ctx);
    }

    for (const auto& obj : obj_files) {
        link.inputs.push_back(obj);
        link.dependencies.insert(obj);
    }

    link.outputs.push_back(output_path);
    graph.add_task(std::move(link));
}

} // namespace dmake
