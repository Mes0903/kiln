#include "../interperter.hpp"
#include "../command_parser.hpp"
#include "../target.hpp"
#include "../build_system.hpp"
#include "../cache_store.hpp"
#include "../profiler.hpp"
#include "../utils.hpp"
#include "../CMakeArray.hpp"
#include "../compiler.hpp"
#include "../language.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>

namespace kiln {

namespace {

// Helper: Get file modification time as int64_t (nanoseconds since epoch)
std::expected<int64_t, std::string> get_file_mtime(const std::string& path) {
    std::error_code ec;
    auto ftime = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return std::unexpected("Failed to get mtime for " + path + ": " + ec.message());
    }
    return ftime.time_since_epoch().count();
}

// Helper: Compute BLAKE2b hash of string content
std::string blake2b_hash_string(const std::string& content) {
    return blake2b(content).to_string();
}

// Helper: Detect language from file extension
std::string detect_language(const std::string& path) {
    std::filesystem::path p(path);
    std::string ext = p.extension().string();
    if (ext == ".c") return "C";
    if (ext == ".cpp" || ext == ".cxx" || ext == ".cc" || ext == ".C") return "CXX";
    return "CXX";  // Default to C++
}

// Use kiln::shell_split from utils.hpp
using kiln::shell_split;

// Helper: Parse .d file to extract header dependencies
std::expected<std::vector<std::string>, std::string> parse_deps_file(const std::string& deps_file) {
    std::ifstream file(deps_file);
    if (!file) {
        return std::unexpected("Failed to open .d file: " + deps_file);
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    // Parse Make-style dependencies: "target: dep1 dep2 \ \n dep3"
    std::vector<std::string> headers;
    size_t colon_pos = content.find(':');
    if (colon_pos == std::string::npos) {
        return headers;  // No dependencies
    }

    std::string deps_part = content.substr(colon_pos + 1);

    // Remove backslashes and newlines
    std::string cleaned;
    for (size_t i = 0; i < deps_part.size(); ++i) {
        if (deps_part[i] == '\\' && i + 1 < deps_part.size() && deps_part[i + 1] == '\n') {
            i++;  // Skip backslash-newline
            continue;
        }
        if (deps_part[i] != '\n') {
            cleaned += deps_part[i];
        } else {
            cleaned += ' ';
        }
    }

    // Split by whitespace
    std::istringstream iss(cleaned);
    std::string dep;
    while (iss >> dep) {
        if (!dep.empty()) {
            headers.push_back(dep);
        }
    }

    return headers;
}

// Helper: Compute transparent signature (human-readable, not hashed)
// When use_content_hash is false (default), source files are keyed by mtime for speed.
// When true, source files are keyed by content hash — used as a fallback when the
// mtime-based lookup misses because a file was rewritten with identical content
// (e.g. DetermineGflagsNamespace.cmake rewrites the same .cxx with file(WRITE) each run).
std::expected<std::string, std::string> compute_signature(
    const std::string& compiler_path,
    const std::string& compiler_version,
    const std::string& language,
    const std::string& standard,
    const std::vector<std::string>& source_files,
    const std::map<std::string, std::string>& inline_sources,  // name -> content
    const std::vector<std::string>& compile_defs,
    const std::vector<std::string>& link_libs,
    const std::vector<std::string>& link_opts,
    bool use_content_hash = false
) {
    std::ostringstream oss;

    // Compiler info
    oss << "compiler:" << compiler_path << "|";
    oss << "version:" << compiler_version << "|";
    oss << "lang:" << language << "|";
    oss << "std:" << standard << "|";

    // Source files: mtime (fast path) or content hash (fallback for rewritten files)
    for (const auto& src : source_files) {
        if (use_content_hash) {
            std::ifstream ifs(src, std::ios::binary);
            if (!ifs) return std::unexpected("Cannot read source file: " + src);
            std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            oss << "src:" << src << ":hash:" << blake2b(content).to_string() << "|";
        } else {
            auto mtime_result = get_file_mtime(src);
            if (!mtime_result) return std::unexpected(mtime_result.error());
            oss << "src:" << src << ":" << *mtime_result << "|";
        }
    }

    // Inline sources with content hashes
    for (const auto& [name, content] : inline_sources) {
        std::string hash = blake2b_hash_string(content);
        oss << "inline:" << name << ":" << hash << "|";
    }

    // Compile definitions (sorted for consistency)
    std::vector<std::string> sorted_defs = compile_defs;
    std::sort(sorted_defs.begin(), sorted_defs.end());
    for (const auto& def : sorted_defs) {
        oss << "def:" << def << "|";
    }

    // Link libraries (sorted)
    std::vector<std::string> sorted_libs = link_libs;
    std::sort(sorted_libs.begin(), sorted_libs.end());
    for (const auto& lib : sorted_libs) {
        oss << "lib:" << lib << "|";
    }

    // Link options (sorted)
    std::vector<std::string> sorted_opts = link_opts;
    std::sort(sorted_opts.begin(), sorted_opts.end());
    for (const auto& opt : sorted_opts) {
        oss << "opt:" << opt << "|";
    }

    return oss.str();
}


// Helper: Validate cached header deps.
// Check mtime first (cheap). If mtime changed, compare content hash.
// Returns empty string if all headers are unchanged, or a diagnostic reason string.
std::string validate_header_deps(const std::map<std::string, HeaderDep>& header_deps) {
    for (const auto& [header, dep] : header_deps) {
        auto current_mtime = get_file_mtime(header);
        if (!current_mtime) {
            return "header deleted: " + header;
        }
        if (*current_mtime != dep.mtime) {
            // Mtime changed — fall back to content hash
            std::ifstream ifs(header, std::ios::binary);
            if (!ifs) return "header unreadable: " + header;
            std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            if (blake2b(content).to_string() != dep.hash) {
                return "header content changed: " + header;
            }
            // Content identical despite mtime change — still valid
        }
    }
    return {};
}

} // anonymous namespace

// Shared structure for compilation parameters
struct CompileParams {
    const Compiler* compiler = nullptr;  // Compiler from toolchain (handles extensions, etc.)
    Language lang = Language::CXX;       // Language enum
    std::string compiler_path;           // Fallback if no compiler in toolchain
    std::string compiler_version;
    std::string standard;
    bool use_extensions = true;  // Whether to use gnu11 vs c11 (default ON like CMake)
    std::vector<std::string> sources;
    std::map<std::string, std::string> inline_sources_map;
    std::vector<std::string> include_dirs;     // Include directories
    std::vector<std::string> compile_definitions;
    std::vector<std::string> raw_compile_flags;  // Flags passed as-is (e.g., -Werror)
    std::vector<std::string> resolved_link_libs;
    std::vector<std::string> link_options;
    std::filesystem::path temp_dir;
};

// Shared compilation result
struct CompileResult {
    bool success = false;
    std::string output;
    std::map<std::string, HeaderDep> header_deps;
    std::string executable_path;  // For try_run
};

// Shared function: compile source files to executable
std::expected<CompileResult, std::string> compile_sources(
    const CompileParams& params,
    bool link_executable = false
) {
    CompileResult result;

    if (!params.compiler) {
        return std::unexpected("No compiler available for try_compile");
    }

    // Write inline sources to files
    std::vector<std::string> all_sources = params.sources;
    for (const auto& [name, content] : params.inline_sources_map) {
        std::filesystem::path src_path = params.temp_dir / name;
        std::ofstream file(src_path);
        if (!file) {
            return std::unexpected("Failed to write source file: " + src_path.string());
        }
        file << content;
        file.close();
        all_sources.push_back(src_path.string());
    }

    std::string deps_file = (params.temp_dir / "test.d").string();
    std::vector<std::string> obj_files;

    // Compile each source file to object
    for (const auto& src : all_sources) {
        std::filesystem::path src_path(src);
        std::string obj_file = (params.temp_dir / (src_path.stem().string() + ".o")).string();
        obj_files.push_back(obj_file);

        // Build CompileContext for the Compiler
        CompileContext ctx;
        ctx.source = src;
        ctx.output = obj_file;
        ctx.standard = params.standard;
        ctx.extensions_enabled = params.use_extensions;
        ctx.includes = params.include_dirs;
        // Filter empty definitions to avoid bare "-D" flags
        for (const auto& def : params.compile_definitions) {
            auto trimmed = strip(def);
            if (!trimmed.empty()) {
                ctx.definitions.emplace_back(trimmed);
            }
        }
        ctx.options = params.raw_compile_flags;

        std::vector<std::string> compile_cmd = params.compiler->get_compile_command(ctx);

        // Execute compile command
        CommandResult compile_result = run_command(compile_cmd, params.temp_dir.string());
        if (compile_result.exit_code != 0) {
            result.success = false;
            result.output = compile_result.output;
            return result;
        }
        result.output += compile_result.output;

        // Parse header dependencies from .d file
        std::string obj_deps = obj_file + ".d";
        if (std::filesystem::exists(obj_deps)) {
            auto headers_result = parse_deps_file(obj_deps);
            if (headers_result) {
                for (const auto& header : *headers_result) {
                    // Skip inline sources (already captured by signature content hash)
                    bool is_inline = false;
                    std::filesystem::path header_path(header);
                    for (const auto& [inline_name, _] : params.inline_sources_map) {
                        if (header_path.filename() == inline_name) {
                            is_inline = true;
                            break;
                        }
                    }
                    if (is_inline) continue;

                    auto mtime = get_file_mtime(header);
                    if (!mtime) continue;

                    std::ifstream ifs(header, std::ios::binary);
                    if (!ifs) continue;
                    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

                    result.header_deps[header] = HeaderDep{
                        .mtime = *mtime,
                        .hash = blake2b(content).to_string(),
                    };
                }
            }
        }
    }

    // Link if requested
    if (link_executable) {
        result.executable_path = (params.temp_dir / "test").string();

        LinkContext lctx;
        lctx.output = result.executable_path;
        lctx.objects = obj_files;
        lctx.libs = params.resolved_link_libs;
        // raw_compile_flags must also be passed to the linker — flags like -m32
        // affect both compilation and linking (CMake passes CMAKE_REQUIRED_FLAGS
        // to CMAKE_<LANG>_FLAGS which applies to both stages).
        lctx.linker_flags = params.link_options;
        lctx.linker_flags.insert(lctx.linker_flags.end(),
                                 params.raw_compile_flags.begin(),
                                 params.raw_compile_flags.end());
        lctx.standard = params.standard;
        lctx.extensions_enabled = params.use_extensions;

        std::vector<std::string> link_cmd = params.compiler->get_link_command(lctx);

        CommandResult link_result = run_command(link_cmd, params.temp_dir.string());
        if (link_result.exit_code != 0) {
            result.success = false;
            result.output += "\n" + link_result.output;
            return result;
        }
        result.output += link_result.output;
    }

    result.success = true;
    return result;
}

void register_try_compile_builtins(Interpreter& interp) {
    interp.add_builtin("try_compile", [](Interpreter& interp, const std::vector<std::string>& args) {
        CommandParser parser("try_compile");

        std::string result_var;
        std::string bindir;
        std::string srcdir_or_srcfile;  // Could be source file (old syntax) or source directory (project mode)
        std::string project_name;       // For project mode
        std::string target_name;        // For project mode (optional)
        std::vector<std::string> sources;
        std::vector<std::string> source_from_content;  // Pairs: name, content, name, content...
        std::vector<std::string> source_from_var;      // Pairs: name, varname, name, varname...
        std::vector<std::string> source_from_file;     // Pairs: name, filepath, name, filepath...
        std::vector<std::string> compile_definitions;
        std::vector<std::string> raw_compile_flags;    // Compiler flags passed as-is (from CMAKE_FLAGS)
        std::vector<std::string> link_libraries;
        std::vector<std::string> link_options;
        std::string cxx_standard;
        std::string c_standard;
        std::string cxx_standard_required;
        std::string c_standard_required;
        std::string output_variable;
        std::string copy_file;
        std::string copy_file_error;
        std::string log_description;  // Ignored but must be parsed to avoid polluting other keywords
        bool no_cache = false;
        bool no_log = false;
        std::vector<std::string> cmake_flags;

        parser.positional(result_var, "result variable");
        parser.positional(bindir, "binary directory", false);  // Optional
        parser.positional(srcdir_or_srcfile, "source dir/file", false);  // Optional
        parser.positional(project_name, "project name", false);  // Optional (project mode)
        parser.positional(target_name, "target name", false);  // Optional (project mode)
        parser.list("SOURCES", sources);
        parser.list("SOURCE_FROM_CONTENT", source_from_content);
        parser.list("SOURCE_FROM_VAR", source_from_var);
        parser.list("SOURCE_FROM_FILE", source_from_file);
        parser.list("COMPILE_DEFINITIONS", compile_definitions);
        parser.list("LINK_LIBRARIES", link_libraries);
        parser.list("LINK_OPTIONS", link_options);
        parser.list("CMAKE_FLAGS", cmake_flags);
        parser.value("CXX_STANDARD", cxx_standard);
        parser.value("C_STANDARD", c_standard);
        parser.value("CXX_STANDARD_REQUIRED", cxx_standard_required);
        parser.value("C_STANDARD_REQUIRED", c_standard_required);
        parser.value("OUTPUT_VARIABLE", output_variable);
        parser.value("COPY_FILE", copy_file);
        parser.value("COPY_FILE_ERROR", copy_file_error);
        parser.value("LOG_DESCRIPTION", log_description);
        parser.flag("NO_CACHE", no_cache);
        parser.flag("NO_LOG", no_log);

        PARSE_OR_RETURN(parser, interp, args);

        // Filter COMPILE_DEFINITIONS: items starting with '-' (but not '-D') are compiler flags
        // CMake modules like CheckCCompilerFlag pass -Wall etc. as COMPILE_DEFINITIONS
        {
            std::vector<std::string> filtered_defs;
            for (const auto& item : compile_definitions) {
                if (item.empty()) continue;
                if (item.size() >= 2 && item.substr(0, 2) == "-D") {
                    // -DFOO -> FOO (strip the -D prefix)
                    filtered_defs.push_back(item.substr(2));
                } else if (item[0] == '-') {
                    // Compiler flag(s) like -Wall, -Werror, etc.
                    // May contain multiple space-separated flags (e.g. "-Werror -fPIC")
                    // Use shell_split to respect quoting (e.g. -DFOO="BAR BAZ")
                    for (auto& flag : shell_split(item)) raw_compile_flags.push_back(std::move(flag));
                } else {
                    // Plain definition
                    filtered_defs.push_back(item);
                }
            }
            compile_definitions = std::move(filtered_defs);
        }

        // Detect project mode vs source mode
        bool project_mode = false;
        if (!srcdir_or_srcfile.empty() && !project_name.empty()) {
            // Project mode: try_compile(result bindir srcdir projectName [targetName])
            std::filesystem::path srcdir_path(srcdir_or_srcfile);
            if (std::filesystem::is_directory(srcdir_path)) {
                project_mode = true;
            }
        }

        if (project_mode) {
            // Project mode: run cmake and make on the source directory
            std::filesystem::path srcdir(srcdir_or_srcfile);
            std::filesystem::path build_dir(bindir);

            // Create build directory
            std::error_code ec;
            std::filesystem::create_directories(build_dir, ec);
            if (ec) {
                interp.set_fatal_error("Failed to create build directory: " + ec.message());
                return;
            }

            // Build cmake command
            std::vector<std::string> cmake_cmd = {"cmake"};
            cmake_cmd.push_back(srcdir.string());

            // Add CMAKE_FLAGS
            for (const auto& flag : cmake_flags) {
                cmake_cmd.push_back(flag);
            }

            // Run cmake
            CommandResult cmake_result = run_command(cmake_cmd, build_dir.string());
            std::string full_output = cmake_result.output;

            if (cmake_result.exit_code != 0) {
                interp.set_variable(result_var, "FALSE");
                interp.set_cache_variable(result_var, "FALSE");
                if (!output_variable.empty()) {
                    interp.set_variable(output_variable, full_output);
                }
                return;
            }

            // Build make command
            std::vector<std::string> make_cmd = {"make"};
            if (!target_name.empty()) {
                make_cmd.push_back(target_name);
            }

            // Run make
            CommandResult make_result = run_command(make_cmd, build_dir.string());
            full_output += make_result.output;

            bool success = (make_result.exit_code == 0);
            interp.set_variable(result_var, success ? "TRUE" : "FALSE");
            interp.set_cache_variable(result_var, success ? "TRUE" : "FALSE");
            if (!output_variable.empty()) {
                interp.set_variable(output_variable, full_output);
            }
            return;
        }

        // Source mode: handle old-style syntax try_compile(result bindir srcfile)
        if (!srcdir_or_srcfile.empty()) {
            if (bindir.empty()) {
                interp.set_fatal_error("try_compile with source file requires binary directory");
                return;
            }
            sources.push_back(srcdir_or_srcfile);
        }

        // Auto-generate bindir if not specified (CMake 3.25+ behavior)
        // We use kiln_scratch_area instead of CMake's CMakeFiles/CMakeScratch
        if (bindir.empty()) {
            std::string cmake_binary_dir = interp.get_variable("CMAKE_BINARY_DIR");
            if (cmake_binary_dir.empty()) {
                interp.set_fatal_error("try_compile requires CMAKE_BINARY_DIR to be set");
                return;
            }
            bindir = (std::filesystem::path(cmake_binary_dir) / "kiln_scratch_area").string();
        }

        // Process CMAKE_FLAGS (e.g., -DCOMPILE_DEFINITIONS:STRING=-DFOO)
        for (const auto& flag : cmake_flags) {
            if (flag.size() < 2 || flag.substr(0, 2) != "-D") {
                continue;  // Skip non-definition flags
            }

            std::string def = flag.substr(2);  // Remove "-D" prefix
            size_t eq = def.find('=');
            if (eq == std::string::npos) {
                continue;  // Skip flags without values
            }

            std::string var_name = def.substr(0, eq);
            std::string value = def.substr(eq + 1);

            // Trim leading/trailing whitespace from value
            // CMake modules often produce values with trailing spaces
            // (e.g., "-DCOMPILE_DEFINITIONS:STRING=-DFOO=bar ")
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.erase(value.begin());
            while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.pop_back();

            // Strip type annotation if present (e.g., VAR:STRING -> VAR)
            size_t colon = var_name.find(':');
            if (colon != std::string::npos) {
                var_name = var_name.substr(0, colon);
            }

            // Apply based on variable name
            if (var_name == "COMPILE_DEFINITIONS") {
                // CMAKE_FLAGS COMPILE_DEFINITIONS can be:
                // 1. A CMake list (semicolon-separated): FOO;BAR;BAZ
                // 2. Shell-style command line: -DFOO -DBAR "-DBAZ=hello world"
                // 3. A mix: items separated by semicolons, each potentially space-separated
                // First split on semicolons, then shell-split each item
                for (auto list_item : CMakeArrayIterator(value)) {
                    // Shell-split this item to handle spaces and quotes
                    for (const auto& item : shell_split(list_item)) {
                        if (item.empty()) continue;

                        if (item.size() >= 2 && item.substr(0, 2) == "-D") {
                            // It's a definition - strip -D and add to compile_definitions
                            compile_definitions.emplace_back(item.substr(2));
                        } else if (item[0] == '-') {
                            // It's a compiler flag (e.g., -Werror=..., -fPIC, etc.)
                            // Add to raw_compile_flags to be passed as-is
                            raw_compile_flags.emplace_back(item);
                        } else {
                            // Plain definition without -D prefix
                            compile_definitions.emplace_back(item);
                        }
                    }
                }
            } else if (var_name == "LINK_LIBRARIES") {
                for (auto lib : CMakeArrayIterator(value)) {
                    link_libraries.emplace_back(lib);
                }
            } else if (var_name == "LINK_DIRECTORIES") {
                // Add to link options as -L flags
                for (auto dir : CMakeArrayIterator(value)) {
                    if (!dir.empty()) {
                        link_options.push_back(std::string("-L").append(dir));
                    }
                }
            } else if (var_name == "LINK_OPTIONS") {
                for (auto opt : CMakeArrayIterator(value)) {
                    link_options.emplace_back(opt);
                }
            } else if (var_name == "INCLUDE_DIRECTORIES") {
                for (auto dir : CMakeArrayIterator(value)) {
                    if (!dir.empty()) {
                        raw_compile_flags.push_back("-I" + std::string(dir));
                    }
                }
            }
            // Ignore other CMAKE_FLAGS variables for now
        }

        // Validate: need at least one source
        if (sources.empty() && source_from_content.empty() &&
            source_from_var.empty() && source_from_file.empty()) {
            interp.set_fatal_error("try_compile requires at least one source");
            return;
        }

        // Profiling setup
        int64_t profile_start = 0;
        bool profiling = g_profiling_enabled.load(std::memory_order_relaxed);
        std::string profile_src;
        if (profiling) {
            profile_start = Profiler::instance().now_us();
            if (!sources.empty()) profile_src = std::filesystem::path(sources[0]).filename().string();
            else if (!source_from_content.empty()) profile_src = source_from_content[0];
            else if (!source_from_var.empty()) profile_src = source_from_var[0];
            else if (!source_from_file.empty()) profile_src = source_from_file[0];
        }

        // Get cache store
        CacheStore& cache = interp.get_cache_store();

        // Detect language from first source file
        std::string language_str = "CXX";
        if (!sources.empty()) {
            language_str = detect_language(sources[0]);
        } else if (!source_from_content.empty()) {
            language_str = detect_language(source_from_content[0]);
        } else if (!source_from_var.empty()) {
            language_str = detect_language(source_from_var[0]);
        } else if (!source_from_file.empty()) {
            language_str = detect_language(source_from_file[0]);
        }

        // Convert to Language enum and get compiler from toolchain
        Language lang = (language_str == "C") ? Language::C : Language::CXX;
        const Compiler* compiler = interp.get_toolchain().get_compiler(lang);
        if (!compiler) {
            interp.set_fatal_error("No compiler configured for language: " + language_str);
            return;
        }

        // Get compiler version for caching
        std::string version_var = (lang == Language::C) ? "CMAKE_C_COMPILER_VERSION" : "CMAKE_CXX_COMPILER_VERSION";
        std::string compiler_version = interp.get_variable(version_var);
        std::string compiler_path = interp.get_variable((lang == Language::C) ? "CMAKE_C_COMPILER" : "CMAKE_CXX_COMPILER");

        // Get standard
        std::string standard;
        if (lang == Language::C) {
            standard = c_standard.empty() ? interp.get_variable("CMAKE_C_STANDARD") : c_standard;
        } else {
            standard = cxx_standard.empty() ? interp.get_variable("CMAKE_CXX_STANDARD") : cxx_standard;
        }

        // Check if extensions are enabled (default ON)
        std::string ext_var = (lang == Language::C) ? "CMAKE_C_EXTENSIONS" : "CMAKE_CXX_EXTENSIONS";
        std::string ext_value = interp.get_variable(ext_var);
        bool use_extensions = ext_value.empty() || !Interpreter::is_falsy(ext_value);

        // Process inline sources
        std::map<std::string, std::string> inline_sources_map;

        // SOURCE_FROM_CONTENT: pairs of (name, content)
        if (source_from_content.size() % 2 != 0) {
            interp.set_fatal_error("SOURCE_FROM_CONTENT requires pairs of (name, content)");
            return;
        }
        for (size_t i = 0; i < source_from_content.size(); i += 2) {
            inline_sources_map[source_from_content[i]] = source_from_content[i + 1];
        }

        // SOURCE_FROM_VAR: pairs of (name, varname)
        if (source_from_var.size() % 2 != 0) {
            interp.set_fatal_error("SOURCE_FROM_VAR requires pairs of (name, varname)");
            return;
        }
        for (size_t i = 0; i < source_from_var.size(); i += 2) {
            std::string content = interp.get_variable(source_from_var[i + 1]);
            inline_sources_map[source_from_var[i]] = content;
        }

        // SOURCE_FROM_FILE: pairs of (name, filepath)
        if (source_from_file.size() % 2 != 0) {
            interp.set_fatal_error("SOURCE_FROM_FILE requires pairs of (name, filepath)");
            return;
        }
        for (size_t i = 0; i < source_from_file.size(); i += 2) {
            std::ifstream file(source_from_file[i + 1]);
            if (!file) {
                interp.set_fatal_error("Failed to read file: " + source_from_file[i + 1]);
                return;
            }
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            inline_sources_map[source_from_file[i]] = content;
        }

        // Resolve LINK_LIBRARIES (convert target names to paths + propagate properties)
        std::vector<std::string> resolved_link_libs;
        std::vector<std::string> propagated_includes;
        std::vector<std::string> propagated_system_includes;
        std::vector<std::string> propagated_definitions;
        std::vector<std::string> propagated_options;
        auto& targets = interp.get_root()->targets_;
        for (const auto& lib : link_libraries) {
            if (targets.count(lib)) {
                auto& target = targets[lib];
                // Resolve the target to get transitive properties
                target->resolve(targets, interp);

                // Propagate INTERFACE properties from the target
                const auto& iface_includes = target->get_resolved_interface_property("INCLUDE_DIRECTORIES");
                propagated_includes.insert(propagated_includes.end(), iface_includes.begin(), iface_includes.end());

                const auto& iface_system_includes = target->get_resolved_interface_property("SYSTEM_INCLUDE_DIRECTORIES");
                propagated_system_includes.insert(propagated_system_includes.end(), iface_system_includes.begin(), iface_system_includes.end());

                const auto& iface_defs = target->get_resolved_interface_property("COMPILE_DEFINITIONS");
                propagated_definitions.insert(propagated_definitions.end(), iface_defs.begin(), iface_defs.end());

                const auto& iface_opts = target->get_resolved_interface_property("COMPILE_OPTIONS");
                propagated_options.insert(propagated_options.end(), iface_opts.begin(), iface_opts.end());

                // Add the target's output path
                std::string output_path = target->get_output_path();
                if (!output_path.empty()) {
                    resolved_link_libs.push_back(output_path);
                }

                // Add transitive link libraries
                const auto& transitive_libs = target->get_resolved_interface_property("LINK_LIBRARIES");
                resolved_link_libs.insert(resolved_link_libs.end(), transitive_libs.begin(), transitive_libs.end());
            } else {
                // System library or path
                resolved_link_libs.push_back(lib);
            }
        }

        // Merge propagated properties
        for (const auto& inc : propagated_includes) {
            // Add as -I flags to raw_compile_flags
            raw_compile_flags.push_back("-I" + inc);
        }
        for (const auto& inc : propagated_system_includes) {
            // Add as -isystem flags to raw_compile_flags
            raw_compile_flags.push_back("-isystem" + inc);
        }
        for (const auto& def : propagated_definitions) {
            compile_definitions.push_back(def);
        }
        for (const auto& opt : propagated_options) {
            raw_compile_flags.push_back(opt);
        }

        // Compute initial signature (without header deps)
        auto sig_result = compute_signature(
            compiler_path, compiler_version, language_str, standard,
            sources, inline_sources_map, compile_definitions,
            resolved_link_libs, link_options
        );
        if (!sig_result) {
            interp.set_fatal_error("Failed to compute signature: " + sig_result.error());
            return;
        }
        std::string base_signature = *sig_result;

        // Check cache: first by mtime-based signature (fast), then by content-hash (handles rewritten files)
        // Track diagnostic info for profiling
        std::string cache_status;    // "cached", "miss", "invalidated"
        std::string cache_reason;    // details when invalidated or missed

        auto try_cache_hit = [&](const std::string& sig, const char* sig_type) -> bool {
            auto cached = cache.lookup<CacheSubsystem::TryCompile>(sig);
            if (!cached) {
                cache_status = "miss";
                cache_reason = std::string("no cache entry (") + sig_type + ")";
                return false;
            }
            auto reason = validate_header_deps(cached->header_deps);
            if (!reason.empty()) {
                cache_status = "invalidated";
                cache_reason = reason;
                return false;
            }
            cache_status = "cached";
            if (profiling) {
                auto dur = Profiler::instance().now_us() - profile_start;
                Profiler::Args pargs = std::map<std::string, std::string>{
                    {"status", "cached"},
                    {"sig_type", sig_type},
                    {"source", profile_src},
                };
                Profiler::instance().add_complete("try_compile " + profile_src + " (cached)", "configure", profile_start, dur, std::move(pargs));
            }
            interp.set_variable(result_var, cached->success ? "TRUE" : "FALSE");
            interp.set_cache_variable(result_var, cached->success ? "TRUE" : "FALSE");
            if (!output_variable.empty()) {
                interp.set_variable(output_variable, cached->output);
            }
            return true;
        };

        if (try_cache_hit(base_signature, "mtime")) return;

        // Mtime miss — try content-hash signature (source rewritten with same content?)
        if (!sources.empty()) {
            auto hash_sig = compute_signature(
                compiler_path, compiler_version, language_str, standard,
                sources, inline_sources_map, compile_definitions,
                resolved_link_libs, link_options, /*use_content_hash=*/true
            );
            if (hash_sig && try_cache_hit(*hash_sig, "content-hash")) return;
        }

        // Cache miss - need to compile
        // Create temporary directory
        std::filesystem::path temp_dir = std::filesystem::path(bindir) / ".kiln_try_compile" /
                                         std::to_string(std::hash<std::string>{}(base_signature));
        std::error_code ec;
        std::filesystem::create_directories(temp_dir, ec);
        if (ec) {
            interp.set_fatal_error("Failed to create temp directory: " + ec.message());
            return;
        }

        // Check CMAKE_TRY_COMPILE_TARGET_TYPE
        std::string target_type = interp.get_variable("CMAKE_TRY_COMPILE_TARGET_TYPE");
        bool build_static_lib = (target_type == "STATIC_LIBRARY");

        // Prepare compilation parameters
        CompileParams params;
        params.compiler = compiler;
        params.lang = lang;
        params.compiler_path = compiler_path;
        params.compiler_version = compiler_version;
        params.standard = standard;
        params.use_extensions = use_extensions;
        params.sources = sources;
        params.inline_sources_map = inline_sources_map;
        params.include_dirs = propagated_includes;
        params.compile_definitions = compile_definitions;
        params.raw_compile_flags = raw_compile_flags;
        params.resolved_link_libs = resolved_link_libs;
        params.link_options = link_options;
        params.temp_dir = temp_dir;

        // Compile
        // Default to linking to executable unless building static library
        // This is necessary for check_function_exists which needs to link against system libraries
        bool link_to_executable = !build_static_lib;

        auto compile_result = compile_sources(params, link_to_executable);
        if (!compile_result) {
            interp.set_fatal_error("Compilation failed: " + compile_result.error());
            return;
        }

        bool compile_success = compile_result->success;
        std::string output = compile_result->output;
        auto header_deps = compile_result->header_deps;
        std::string artifact_path;

        // For static library, create the .a file
        if (build_static_lib && compile_success) {
            // compile_sources with link_to_executable=false creates .o files
            // We need to find the object file(s) and create a static library
            // Object files are named after source file stems (e.g., src.c -> src.o)
            std::vector<std::string> obj_files;
            for (const auto& src : sources) {
                std::filesystem::path src_path(src);
                obj_files.push_back((temp_dir / (src_path.stem().string() + ".o")).string());
            }
            for (const auto& [name, _] : inline_sources_map) {
                std::filesystem::path src_path(name);
                obj_files.push_back((temp_dir / (src_path.stem().string() + ".o")).string());
            }

            std::string lib_file = (temp_dir / "libtest.a").string();
            std::vector<std::string> ar_cmd = {"ar", "rcs", lib_file};
            ar_cmd.insert(ar_cmd.end(), obj_files.begin(), obj_files.end());
            CommandResult ar_result = run_command(ar_cmd, temp_dir.string());

            if (ar_result.exit_code != 0) {
                compile_success = false;
                output += "\nArchive creation failed:\n" + ar_result.output;
            } else {
                artifact_path = lib_file;
            }
        } else if (compile_success) {
            // Executable was built
            artifact_path = compile_result->executable_path;
        }

        // Store in cache under both mtime and content-hash signatures so that
        // future lookups hit regardless of whether the source file was rewritten
        TryCompileCacheEntry entry;
        entry.success = compile_success;
        entry.output = output;
        entry.header_deps = header_deps;
        cache.insert<CacheSubsystem::TryCompile>(base_signature, entry);

        if (!sources.empty()) {
            auto hash_sig = compute_signature(
                compiler_path, compiler_version, language_str, standard,
                sources, inline_sources_map, compile_definitions,
                resolved_link_libs, link_options, /*use_content_hash=*/true
            );
            if (hash_sig && *hash_sig != base_signature) {
                cache.insert<CacheSubsystem::TryCompile>(*hash_sig, entry);
            }
        }

        // Handle COPY_FILE if specified
        if (!copy_file.empty() && compile_success && !artifact_path.empty()) {
            std::error_code copy_ec;

            // Create parent directories for copy destination
            std::filesystem::path copy_dest(copy_file);
            if (copy_dest.has_parent_path()) {
                std::filesystem::create_directories(copy_dest.parent_path(), copy_ec);
                if (copy_ec && !copy_file_error.empty()) {
                    interp.set_variable(copy_file_error, "Failed to create directory: " + copy_ec.message());
                    copy_ec.clear();
                }
            }

            // Copy the file
            if (!copy_ec) {
                std::filesystem::copy_file(artifact_path, copy_file,
                                          std::filesystem::copy_options::overwrite_existing,
                                          copy_ec);
                if (copy_ec && !copy_file_error.empty()) {
                    interp.set_variable(copy_file_error, "Failed to copy file: " + copy_ec.message());
                }
            }
        }

        // Set result variables (both local and cache, matching CMake behavior)
        interp.set_variable(result_var, compile_success ? "TRUE" : "FALSE");
        interp.set_cache_variable(result_var, compile_success ? "TRUE" : "FALSE");
        if (!output_variable.empty()) {
            interp.set_variable(output_variable, output);
        }

        if (profiling) {
            auto dur = Profiler::instance().now_us() - profile_start;
            Profiler::Args pargs = std::map<std::string, std::string>{
                {"status", cache_status},
                {"reason", cache_reason},
                {"source", profile_src},
                {"result", compile_success ? "TRUE" : "FALSE"},
                {"signature", base_signature},
            };
            Profiler::instance().add_complete("try_compile " + profile_src, "configure", profile_start, dur, std::move(pargs));
        }

        // Clean up temp directory on success (keep on failure for debugging)
        if (compile_success) {
            std::filesystem::remove_all(temp_dir, ec);
        }
    });

    interp.add_builtin("try_run", [](Interpreter& interp, const std::vector<std::string>& args) {
        CommandParser parser("try_run");

        std::string run_result_var;
        std::string compile_result_var;
        std::string bindir;
        std::string old_style_srcfile;
        std::vector<std::string> sources;
        std::vector<std::string> source_from_content;
        std::vector<std::string> source_from_var;
        std::vector<std::string> source_from_file;
        std::vector<std::string> compile_definitions;
        std::vector<std::string> raw_compile_flags;
        std::vector<std::string> link_libraries;
        std::vector<std::string> link_options;
        std::string cxx_standard;
        std::string c_standard;
        std::string cxx_standard_required;
        std::string c_standard_required;
        std::string compile_output_variable;
        std::string run_output_variable;
        std::string run_output_stdout_variable;
        std::string run_output_stderr_variable;
        std::string working_directory;
        std::vector<std::string> run_args;
        std::vector<std::string> cmake_flags;

        parser.positional(run_result_var, "run result variable");
        parser.positional(compile_result_var, "compile result variable");
        parser.positional(bindir, "binary directory", false);
        parser.positional(old_style_srcfile, "source file (old syntax)", false);
        parser.list("SOURCES", sources);
        parser.list("SOURCE_FROM_CONTENT", source_from_content);
        parser.list("SOURCE_FROM_VAR", source_from_var);
        parser.list("SOURCE_FROM_FILE", source_from_file);
        parser.list("COMPILE_DEFINITIONS", compile_definitions);
        parser.list("LINK_LIBRARIES", link_libraries);
        parser.list("LINK_OPTIONS", link_options);
        parser.list("CMAKE_FLAGS", cmake_flags);
        parser.list("ARGS", run_args);
        parser.value("CXX_STANDARD", cxx_standard);
        parser.value("C_STANDARD", c_standard);
        parser.value("CXX_STANDARD_REQUIRED", cxx_standard_required);
        parser.value("C_STANDARD_REQUIRED", c_standard_required);
        parser.value("COMPILE_OUTPUT_VARIABLE", compile_output_variable);
        parser.value("RUN_OUTPUT_VARIABLE", run_output_variable);
        parser.value("RUN_OUTPUT_STDOUT_VARIABLE", run_output_stdout_variable);
        parser.value("RUN_OUTPUT_STDERR_VARIABLE", run_output_stderr_variable);
        parser.value("WORKING_DIRECTORY", working_directory);

        PARSE_OR_RETURN(parser, interp, args);

        // Filter COMPILE_DEFINITIONS: items starting with '-' (but not '-D') are compiler flags
        // CMake modules like CheckCCompilerFlag pass -Wall etc. as COMPILE_DEFINITIONS
        {
            std::vector<std::string> filtered_defs;
            for (const auto& item : compile_definitions) {
                if (item.empty()) continue;
                if (item.size() >= 2 && item.substr(0, 2) == "-D") {
                    // -DFOO -> FOO (strip the -D prefix)
                    filtered_defs.push_back(item.substr(2));
                } else if (item[0] == '-') {
                    // Compiler flag(s) like -Wall, -Werror, etc.
                    // May contain multiple space-separated flags (e.g. "-Werror -fPIC")
                    // Use shell_split to respect quoting (e.g. -DFOO="BAR BAZ")
                    for (auto& flag : shell_split(item)) raw_compile_flags.push_back(std::move(flag));
                } else {
                    // Plain definition
                    filtered_defs.push_back(item);
                }
            }
            compile_definitions = std::move(filtered_defs);
        }

        // Handle old-style syntax
        if (!old_style_srcfile.empty()) {
            if (bindir.empty()) {
                interp.set_fatal_error("try_run with source file requires binary directory");
                return;
            }
            sources.push_back(old_style_srcfile);
        }

        // Profiling
        std::string profile_src;
        if (g_profiling_enabled.load(std::memory_order_relaxed)) {
            if (!sources.empty()) profile_src = std::filesystem::path(sources[0]).filename().string();
            else if (!source_from_content.empty()) profile_src = source_from_content[0];
            else if (!source_from_var.empty()) profile_src = source_from_var[0];
            else if (!source_from_file.empty()) profile_src = source_from_file[0];
        }
        ProfileScope try_run_profile("try_run " + profile_src, "configure");

        // Auto-generate bindir if not specified
        if (bindir.empty()) {
            std::string cmake_binary_dir = interp.get_variable("CMAKE_BINARY_DIR");
            if (cmake_binary_dir.empty()) {
                interp.set_fatal_error("try_run requires CMAKE_BINARY_DIR to be set");
                return;
            }
            bindir = (std::filesystem::path(cmake_binary_dir) / "kiln_scratch_area").string();
        }

        // Process CMAKE_FLAGS (same as try_compile)
        for (const auto& flag : cmake_flags) {
            if (flag.size() < 2 || flag.substr(0, 2) != "-D") {
                continue;
            }

            std::string def = flag.substr(2);
            size_t eq = def.find('=');
            if (eq == std::string::npos) {
                continue;
            }

            std::string var_name = def.substr(0, eq);
            std::string value = def.substr(eq + 1);

            // Trim leading/trailing whitespace from value
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.erase(value.begin());
            while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.pop_back();

            size_t colon = var_name.find(':');
            if (colon != std::string::npos) {
                var_name = var_name.substr(0, colon);
            }

            if (var_name == "COMPILE_DEFINITIONS") {
                // CMAKE_FLAGS COMPILE_DEFINITIONS can be:
                // 1. A CMake list (semicolon-separated): FOO;BAR;BAZ
                // 2. Shell-style command line: -DFOO -DBAR "-DBAZ=hello world"
                // 3. A mix: items separated by semicolons, each potentially space-separated
                // First split on semicolons, then shell-split each item
                for (auto list_item : CMakeArrayIterator(value)) {
                    // Shell-split this item to handle spaces and quotes
                    for (const auto& item : shell_split(list_item)) {
                        if (item.empty()) continue;

                        if (item.size() >= 2 && item.substr(0, 2) == "-D") {
                            // It's a definition - strip -D and add to compile_definitions
                            compile_definitions.emplace_back(item.substr(2));
                        } else if (item[0] == '-') {
                            // It's a compiler flag (e.g., -Werror=..., -fPIC, etc.)
                            raw_compile_flags.emplace_back(item);
                        } else {
                            // Plain definition without -D prefix
                            compile_definitions.emplace_back(item);
                        }
                    }
                }
            } else if (var_name == "LINK_LIBRARIES") {
                for (auto lib : CMakeArrayIterator(value)) {
                    link_libraries.emplace_back(lib);
                }
            } else if (var_name == "LINK_DIRECTORIES") {
                for (auto dir : CMakeArrayIterator(value)) {
                    if (!dir.empty()) {
                        link_options.push_back(std::string("-L").append(dir));
                    }
                }
            } else if (var_name == "LINK_OPTIONS") {
                for (auto opt : CMakeArrayIterator(value)) {
                    link_options.emplace_back(opt);
                }
            }
        }

        // Validate: need at least one source
        if (sources.empty() && source_from_content.empty() &&
            source_from_var.empty() && source_from_file.empty()) {
            interp.set_fatal_error("try_run requires at least one source");
            return;
        }

        // Detect language from first source file
        std::string language_str = "CXX";
        if (!sources.empty()) {
            language_str = detect_language(sources[0]);
        } else if (!source_from_content.empty()) {
            language_str = detect_language(source_from_content[0]);
        } else if (!source_from_var.empty()) {
            language_str = detect_language(source_from_var[0]);
        } else if (!source_from_file.empty()) {
            language_str = detect_language(source_from_file[0]);
        }

        // Convert to Language enum and get compiler from toolchain
        Language lang = (language_str == "C") ? Language::C : Language::CXX;
        const Compiler* compiler = interp.get_toolchain().get_compiler(lang);
        if (!compiler) {
            interp.set_fatal_error("No compiler configured for language: " + language_str);
            return;
        }

        // Get compiler version for caching
        std::string version_var = (lang == Language::C) ? "CMAKE_C_COMPILER_VERSION" : "CMAKE_CXX_COMPILER_VERSION";
        std::string compiler_version = interp.get_variable(version_var);
        std::string compiler_path = interp.get_variable((lang == Language::C) ? "CMAKE_C_COMPILER" : "CMAKE_CXX_COMPILER");

        // Get standard
        std::string standard;
        if (lang == Language::C) {
            standard = c_standard.empty() ? interp.get_variable("CMAKE_C_STANDARD") : c_standard;
        } else {
            standard = cxx_standard.empty() ? interp.get_variable("CMAKE_CXX_STANDARD") : cxx_standard;
        }

        // Check if extensions are enabled (default ON)
        std::string ext_var = (lang == Language::C) ? "CMAKE_C_EXTENSIONS" : "CMAKE_CXX_EXTENSIONS";
        std::string ext_value = interp.get_variable(ext_var);
        bool use_extensions = ext_value.empty() || !Interpreter::is_falsy(ext_value);

        // Process inline sources
        std::map<std::string, std::string> inline_sources_map;

        if (source_from_content.size() % 2 != 0) {
            interp.set_fatal_error("SOURCE_FROM_CONTENT requires pairs of (name, content)");
            return;
        }
        for (size_t i = 0; i < source_from_content.size(); i += 2) {
            inline_sources_map[source_from_content[i]] = source_from_content[i + 1];
        }

        if (source_from_var.size() % 2 != 0) {
            interp.set_fatal_error("SOURCE_FROM_VAR requires pairs of (name, varname)");
            return;
        }
        for (size_t i = 0; i < source_from_var.size(); i += 2) {
            std::string content = interp.get_variable(source_from_var[i + 1]);
            inline_sources_map[source_from_var[i]] = content;
        }

        if (source_from_file.size() % 2 != 0) {
            interp.set_fatal_error("SOURCE_FROM_FILE requires pairs of (name, filepath)");
            return;
        }
        for (size_t i = 0; i < source_from_file.size(); i += 2) {
            std::ifstream file(source_from_file[i + 1]);
            if (!file) {
                interp.set_fatal_error("Failed to read file: " + source_from_file[i + 1]);
                return;
            }
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            inline_sources_map[source_from_file[i]] = content;
        }

        // Resolve LINK_LIBRARIES (convert target names to paths + propagate properties)
        std::vector<std::string> resolved_link_libs;
        std::vector<std::string> propagated_includes;
        std::vector<std::string> propagated_system_includes;
        std::vector<std::string> propagated_definitions;
        std::vector<std::string> propagated_options;
        auto& targets = interp.get_root()->targets_;
        for (const auto& lib : link_libraries) {
            if (targets.count(lib)) {
                auto& target = targets[lib];
                // Resolve the target to get transitive properties
                target->resolve(targets, interp);

                // Propagate INTERFACE properties from the target
                const auto& iface_includes = target->get_resolved_interface_property("INCLUDE_DIRECTORIES");
                propagated_includes.insert(propagated_includes.end(), iface_includes.begin(), iface_includes.end());

                const auto& iface_system_includes = target->get_resolved_interface_property("SYSTEM_INCLUDE_DIRECTORIES");
                propagated_system_includes.insert(propagated_system_includes.end(), iface_system_includes.begin(), iface_system_includes.end());

                const auto& iface_defs = target->get_resolved_interface_property("COMPILE_DEFINITIONS");
                propagated_definitions.insert(propagated_definitions.end(), iface_defs.begin(), iface_defs.end());

                const auto& iface_opts = target->get_resolved_interface_property("COMPILE_OPTIONS");
                propagated_options.insert(propagated_options.end(), iface_opts.begin(), iface_opts.end());

                // Add the target's output path
                std::string output_path = target->get_output_path();
                if (!output_path.empty()) {
                    resolved_link_libs.push_back(output_path);
                }

                // Add transitive link libraries
                const auto& transitive_libs = target->get_resolved_interface_property("LINK_LIBRARIES");
                resolved_link_libs.insert(resolved_link_libs.end(), transitive_libs.begin(), transitive_libs.end());
            } else {
                // System library or path
                resolved_link_libs.push_back(lib);
            }
        }

        // Merge propagated properties
        for (const auto& inc : propagated_includes) {
            raw_compile_flags.push_back("-I" + inc);
        }
        for (const auto& inc : propagated_system_includes) {
            raw_compile_flags.push_back("-isystem" + inc);
        }
        for (const auto& def : propagated_definitions) {
            compile_definitions.push_back(def);
        }
        for (const auto& opt : propagated_options) {
            raw_compile_flags.push_back(opt);
        }

        // Compute signature for caching (compilation inputs + run args)
        auto sig_result = compute_signature(
            compiler_path, compiler_version, language_str, standard,
            sources, inline_sources_map, compile_definitions,
            resolved_link_libs, link_options
        );
        if (!sig_result) {
            interp.set_fatal_error("Failed to compute signature: " + sig_result.error());
            return;
        }
        // Extend with run-specific inputs
        std::ostringstream run_sig;
        run_sig << *sig_result;
        for (const auto& arg : run_args) {
            run_sig << "arg:" << arg << "|";
        }
        if (!working_directory.empty()) {
            run_sig << "workdir:" << working_directory << "|";
        }
        for (const auto& flag : raw_compile_flags) {
            run_sig << "cflag:" << flag << "|";
        }
        std::string base_signature = run_sig.str();

        // Check cache
        CacheStore& cache = interp.get_cache_store();

        // Helper to check cache and return early on hit
        auto try_cache_hit = [&](const std::string& sig) -> bool {
            auto cached = cache.lookup<CacheSubsystem::TryRun>(sig);
            if (!cached) return false;
            auto reason = validate_header_deps(cached->header_deps);
            if (!reason.empty()) return false;
            // Cache hit
            interp.set_variable(compile_result_var, cached->compile_success ? "TRUE" : "FALSE");
            interp.set_cache_variable(compile_result_var, cached->compile_success ? "TRUE" : "FALSE");
            if (!compile_output_variable.empty()) {
                interp.set_variable(compile_output_variable, cached->compile_output);
            }
            if (!cached->compile_success) {
                interp.set_variable(run_result_var, "FAILED_TO_RUN");
            } else {
                interp.set_variable(run_result_var, std::to_string(cached->exit_code));
                if (!run_output_variable.empty()) {
                    interp.set_variable(run_output_variable, cached->run_output);
                }
                if (!run_output_stdout_variable.empty()) {
                    interp.set_variable(run_output_stdout_variable, cached->run_output);
                }
                if (!run_output_stderr_variable.empty()) {
                    interp.set_variable(run_output_stderr_variable, cached->run_output);
                }
            }
            return true;
        };

        if (try_cache_hit(base_signature)) return;

        // Mtime miss — try content-hash signature (source rewritten with same content?)
        // Compute the content-hash extended signature
        std::string hash_extended_signature;
        if (!sources.empty()) {
            auto hash_sig = compute_signature(
                compiler_path, compiler_version, language_str, standard,
                sources, inline_sources_map, compile_definitions,
                resolved_link_libs, link_options, /*use_content_hash=*/true
            );
            if (hash_sig) {
                std::ostringstream hash_run_sig;
                hash_run_sig << *hash_sig;
                for (const auto& arg : run_args) {
                    hash_run_sig << "arg:" << arg << "|";
                }
                if (!working_directory.empty()) {
                    hash_run_sig << "workdir:" << working_directory << "|";
                }
                for (const auto& flag : raw_compile_flags) {
                    hash_run_sig << "cflag:" << flag << "|";
                }
                hash_extended_signature = hash_run_sig.str();
                if (try_cache_hit(hash_extended_signature)) return;
            }
        }

        // Check for cross-compilation
        bool is_cross_compiling = (interp.get_variable("CMAKE_CROSSCOMPILING") == "TRUE" ||
                                     interp.get_variable("CMAKE_CROSSCOMPILING") == "ON" ||
                                     interp.get_variable("CMAKE_CROSSCOMPILING") == "1");

        // Create temporary directory
        std::filesystem::path temp_dir = std::filesystem::path(bindir) / ".kiln_try_run" /
                                         std::to_string(std::hash<std::string>{}(base_signature));
        std::error_code ec;
        std::filesystem::create_directories(temp_dir, ec);
        if (ec) {
            interp.set_fatal_error("Failed to create temp directory: " + ec.message());
            return;
        }

        // Prepare compilation parameters
        CompileParams params;
        params.compiler = compiler;
        params.lang = lang;
        params.compiler_path = compiler_path;
        params.compiler_version = compiler_version;
        params.standard = standard;
        params.use_extensions = use_extensions;
        params.sources = sources;
        params.inline_sources_map = inline_sources_map;
        params.include_dirs = propagated_includes;
        params.compile_definitions = compile_definitions;
        params.raw_compile_flags = raw_compile_flags;
        params.resolved_link_libs = resolved_link_libs;
        params.link_options = link_options;
        params.temp_dir = temp_dir;

        // Compile to executable
        auto compile_result = compile_sources(params, true);
        if (!compile_result) {
            interp.set_fatal_error("Compilation failed: " + compile_result.error());
            return;
        }

        bool compile_success = compile_result->success;
        std::string compile_output = compile_result->output;

        // Set compile result (both local and cache, matching CMake behavior)
        interp.set_variable(compile_result_var, compile_success ? "TRUE" : "FALSE");
        interp.set_cache_variable(compile_result_var, compile_success ? "TRUE" : "FALSE");
        if (!compile_output_variable.empty()) {
            interp.set_variable(compile_output_variable, compile_output);
        }

        // If compilation failed, cache the failure and return
        if (!compile_success) {
            interp.set_variable(run_result_var, "FAILED_TO_RUN");
            TryRunCacheEntry entry;
            entry.compile_success = false;
            entry.compile_output = compile_output;
            entry.header_deps = compile_result->header_deps;
            cache.insert<CacheSubsystem::TryRun>(base_signature, entry);
            // Also insert under content-hash signature for rewritten source files
            if (!hash_extended_signature.empty() && hash_extended_signature != base_signature) {
                cache.insert<CacheSubsystem::TryRun>(hash_extended_signature, entry);
            }
            return;
        }

        // Handle cross-compilation case
        if (is_cross_compiling) {
            std::string emulator = interp.get_variable("CMAKE_CROSSCOMPILING_EMULATOR");
            if (emulator.empty()) {
                // Can't run - require manual cache variables
                // Check if cache variables exist
                std::string cached_exit_code = interp.get_variable(run_result_var);
                std::string cached_output = interp.get_variable(run_result_var + "__TRYRUN_OUTPUT");

                if (cached_exit_code.empty()) {
                    interp.set_fatal_error("Cross-compiling without emulator - please set " + run_result_var +
                                           " cache variable to expected exit code");
                    return;
                }

                // Use cached values
                if (!run_output_variable.empty() && !cached_output.empty()) {
                    interp.set_variable(run_output_variable, cached_output);
                }
                // Exit code already set via cache variable
                return;
            }
            // If emulator is set, we'll use it below
        }

        // Run the executable
        std::string exe_path = compile_result->executable_path;
        std::string run_dir = working_directory.empty() ? temp_dir.string() : working_directory;

        // Build run command
        std::vector<std::string> run_cmd;

        // Add emulator if cross-compiling
        if (is_cross_compiling) {
            std::string emulator = interp.get_variable("CMAKE_CROSSCOMPILING_EMULATOR");
            if (!emulator.empty()) {
                for (auto part : CMakeArrayIterator(emulator)) {
                    run_cmd.emplace_back(part);
                }
            }
        }

        run_cmd.push_back(exe_path);
        for (const auto& arg : run_args) {
            run_cmd.push_back(arg);
        }

        CommandResult run_result = run_command(run_cmd, run_dir);

        // Set run result (exit code)
        interp.set_variable(run_result_var, std::to_string(run_result.exit_code));

        // Set output variables
        if (!run_output_variable.empty()) {
            interp.set_variable(run_output_variable, run_result.output);
        }
        if (!run_output_stdout_variable.empty()) {
            interp.set_variable(run_output_stdout_variable, run_result.output);
        }
        if (!run_output_stderr_variable.empty()) {
            // Combined output goes to both for now
            interp.set_variable(run_output_stderr_variable, run_result.output);
        }

        // Cache the result under both mtime and content-hash signatures
        TryRunCacheEntry entry;
        entry.compile_success = true;
        entry.compile_output = compile_output;
        entry.exit_code = run_result.exit_code;
        entry.run_output = run_result.output;
        entry.header_deps = compile_result->header_deps;
        cache.insert<CacheSubsystem::TryRun>(base_signature, entry);
        if (!hash_extended_signature.empty() && hash_extended_signature != base_signature) {
            cache.insert<CacheSubsystem::TryRun>(hash_extended_signature, entry);
        }

        // Clean up temp directory on success
        if (run_result.exit_code == 0) {
            std::filesystem::remove_all(temp_dir, ec);
        }
    });
}

} // namespace kiln
