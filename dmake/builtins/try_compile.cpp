#include "../interperter.hpp"
#include "../command_parser.hpp"
#include "../target.hpp"
#include "../build_system.hpp"
#include "../cache_store.hpp"
#include "../utils.hpp"
#include "../CMakeList.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>

namespace dmake {

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
std::expected<std::string, std::string> compute_signature(
    const std::string& compiler_path,
    const std::string& compiler_version,
    const std::string& language,
    const std::string& standard,
    const std::vector<std::string>& source_files,
    const std::map<std::string, std::string>& inline_sources,  // name -> content
    const std::vector<std::string>& compile_defs,
    const std::vector<std::string>& link_libs,
    const std::vector<std::string>& link_opts
) {
    std::ostringstream oss;

    // Compiler info
    oss << "compiler:" << compiler_path << "|";
    oss << "version:" << compiler_version << "|";
    oss << "lang:" << language << "|";
    oss << "std:" << standard << "|";

    // Source files with mtimes
    for (const auto& src : source_files) {
        auto mtime_result = get_file_mtime(src);
        if (!mtime_result) {
            return std::unexpected(mtime_result.error());
        }
        oss << "src:" << src << ":" << *mtime_result << "|";
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

// Helper: Add header mtimes to signature
std::expected<std::string, std::string> add_header_deps_to_signature(
    const std::string& base_signature,
    const std::map<std::string, int64_t>& header_mtimes
) {
    std::ostringstream oss;
    oss << base_signature;

    // Sort headers by path for consistency
    std::vector<std::pair<std::string, int64_t>> sorted_headers(header_mtimes.begin(), header_mtimes.end());
    std::sort(sorted_headers.begin(), sorted_headers.end());

    for (const auto& [header, mtime] : sorted_headers) {
        oss << "dep:" << header << ":" << mtime << "|";
    }

    return oss.str();
}

// Helper: Validate cache entry (check all header mtimes)
bool validate_cache_entry(const TryCompileCacheEntry& entry) {
    for (const auto& [header, cached_mtime] : entry.header_mtimes) {
        auto current_mtime = get_file_mtime(header);
        if (!current_mtime) {
            return false;  // Header deleted or inaccessible
        }
        if (*current_mtime != cached_mtime) {
            return false;  // Header modified
        }
    }
    return true;
}

} // anonymous namespace

void register_try_compile_builtins(Interpreter& interp) {
    interp.add_builtin("try_compile", [](Interpreter& interp, const std::vector<std::string>& args) {
        CommandParser parser("try_compile");

        std::string result_var;
        std::string bindir;
        std::vector<std::string> sources;
        std::vector<std::string> source_from_content;  // Pairs: name, content, name, content...
        std::vector<std::string> source_from_var;      // Pairs: name, varname, name, varname...
        std::vector<std::string> source_from_file;     // Pairs: name, filepath, name, filepath...
        std::vector<std::string> compile_definitions;
        std::vector<std::string> link_libraries;
        std::vector<std::string> link_options;
        std::string cxx_standard;
        std::string c_standard;
        std::string output_variable;
        std::vector<std::string> cmake_flags;

        parser.add_positional(result_var, "result variable");
        parser.add_positional(bindir, "binary directory", false);  // Optional
        parser.add_list("SOURCES", sources);
        parser.add_list("SOURCE_FROM_CONTENT", source_from_content);
        parser.add_list("SOURCE_FROM_VAR", source_from_var);
        parser.add_list("SOURCE_FROM_FILE", source_from_file);
        parser.add_list("COMPILE_DEFINITIONS", compile_definitions);
        parser.add_list("LINK_LIBRARIES", link_libraries);
        parser.add_list("LINK_OPTIONS", link_options);
        parser.add_list("CMAKE_FLAGS", cmake_flags);
        parser.add_value("CXX_STANDARD", cxx_standard);
        parser.add_value("C_STANDARD", c_standard);
        parser.add_value("OUTPUT_VARIABLE", output_variable);

        PARSE_OR_RETURN(parser, interp, args);

        // Auto-generate bindir if not specified (CMake 3.25+ behavior)
        // We use dmake_scratch_area instead of CMake's CMakeFiles/CMakeScratch
        if (bindir.empty()) {
            std::string cmake_binary_dir = interp.get_variable("CMAKE_BINARY_DIR");
            if (cmake_binary_dir.empty()) {
                interp.set_fatal_error("try_compile requires CMAKE_BINARY_DIR to be set");
                return;
            }
            bindir = (std::filesystem::path(cmake_binary_dir) / "dmake_scratch_area").string();
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

            // Strip type annotation if present (e.g., VAR:STRING -> VAR)
            size_t colon = var_name.find(':');
            if (colon != std::string::npos) {
                var_name = var_name.substr(0, colon);
            }

            // Apply based on variable name
            if (var_name == "COMPILE_DEFINITIONS") {
                // Value might be multiple definitions separated by semicolons
                // Each definition might start with -D which we need to strip
                CMakeList defs(value);
                for (const auto& d : defs) {
                    if (d.size() >= 2 && d.substr(0, 2) == "-D") {
                        compile_definitions.push_back(d.substr(2));
                    } else {
                        compile_definitions.push_back(d);
                    }
                }
            } else if (var_name == "LINK_LIBRARIES") {
                CMakeList libs(value);
                for (const auto& lib : libs) {
                    link_libraries.push_back(lib);
                }
            } else if (var_name == "LINK_DIRECTORIES") {
                // Add to link options as -L flags
                CMakeList dirs(value);
                for (const auto& dir : dirs) {
                    if (!dir.empty()) {
                        link_options.push_back("-L" + dir);
                    }
                }
            } else if (var_name == "LINK_OPTIONS") {
                CMakeList opts(value);
                for (const auto& opt : opts) {
                    link_options.push_back(opt);
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

        // Get cache store
        CacheStore& cache = interp.get_cache_store();

        // Detect language from first source file
        std::string language = "CXX";
        if (!sources.empty()) {
            language = detect_language(sources[0]);
        } else if (!source_from_content.empty()) {
            language = detect_language(source_from_content[0]);
        } else if (!source_from_var.empty()) {
            language = detect_language(source_from_var[0]);
        } else if (!source_from_file.empty()) {
            language = detect_language(source_from_file[0]);
        }

        // Get compiler
        std::string compiler_var = (language == "C") ? "CMAKE_C_COMPILER" : "CMAKE_CXX_COMPILER";
        std::string compiler_path = interp.get_variable(compiler_var);
        if (compiler_path.empty()) {
            compiler_path = (language == "C") ? "gcc" : "g++";
        }

        // Get compiler version directly
        CommandResult version_cmd = run_command(compiler_path + " --version 2>/dev/null | head -n 1", "");
        if (version_cmd.exit_code != 0 || version_cmd.output.empty()) {
            interp.set_fatal_error("Failed to get compiler version from " + compiler_path);
            return;
        }
        std::string compiler_version = version_cmd.output;
        if (!compiler_version.empty() && compiler_version.back() == '\n') {
            compiler_version.pop_back();
        }

        // Get standard
        std::string standard;
        if (language == "C") {
            standard = c_standard.empty() ? interp.get_variable("CMAKE_C_STANDARD") : c_standard;
            if (standard.empty()) standard = "11";
        } else {
            standard = cxx_standard.empty() ? interp.get_variable("CMAKE_CXX_STANDARD") : cxx_standard;
            if (standard.empty()) standard = "17";
        }

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

        // Resolve LINK_LIBRARIES (convert target names to paths)
        std::vector<std::string> resolved_link_libs;
        auto& targets = interp.get_root()->targets_;
        for (const auto& lib : link_libraries) {
            if (targets.count(lib)) {
                // It's a target - use its output path
                resolved_link_libs.push_back(targets[lib]->get_output_path());
            } else {
                // System library or path
                resolved_link_libs.push_back(lib);
            }
        }

        // Compute initial signature (without header deps)
        auto sig_result = compute_signature(
            compiler_path, compiler_version, language, standard,
            sources, inline_sources_map, compile_definitions,
            resolved_link_libs, link_options
        );
        if (!sig_result) {
            interp.set_fatal_error("Failed to compute signature: " + sig_result.error());
            return;
        }
        std::string base_signature = *sig_result;

        // Check cache (without header deps first)
        auto cached = cache.lookup<CacheSubsystem::TryCompile>(base_signature);
        if (cached) {
            // Validate header mtimes
            if (validate_cache_entry(*cached)) {
                // Cache hit!
                interp.set_variable(result_var, cached->success ? "TRUE" : "FALSE");
                if (!output_variable.empty()) {
                    interp.set_variable(output_variable, cached->output);
                }
                return;
            }
        }

        // Cache miss - need to compile
        // Create temporary directory
        std::filesystem::path temp_dir = std::filesystem::path(bindir) / ".dmake_try_compile" /
                                         std::to_string(std::hash<std::string>{}(base_signature));
        std::error_code ec;
        std::filesystem::create_directories(temp_dir, ec);
        if (ec) {
            interp.set_fatal_error("Failed to create temp directory: " + ec.message());
            return;
        }

        // Write inline sources to files
        std::vector<std::string> all_sources = sources;
        for (const auto& [name, content] : inline_sources_map) {
            std::filesystem::path src_path = temp_dir / name;
            std::ofstream file(src_path);
            if (!file) {
                interp.set_fatal_error("Failed to write source file: " + src_path.string());
                return;
            }
            file << content;
            file.close();
            all_sources.push_back(src_path.string());
        }

        // Build compile command
        std::vector<std::string> compile_cmd = {compiler_path};

        // Standard (only if specified - otherwise use compiler default)
        if (!standard.empty()) {
            if (language == "C") {
                compile_cmd.push_back("-std=c" + standard);
            } else {
                compile_cmd.push_back("-std=c++" + standard);
            }
        }

        // Definitions
        for (const auto& def : compile_definitions) {
            compile_cmd.push_back("-D" + def);
        }

        // Generate dependency file
        std::string obj_file = (temp_dir / "test.o").string();
        std::string deps_file = (temp_dir / "test.d").string();
        compile_cmd.push_back("-MD");
        compile_cmd.push_back("-MF");
        compile_cmd.push_back(deps_file);
        compile_cmd.push_back("-c");
        compile_cmd.push_back("-o");
        compile_cmd.push_back(obj_file);

        // Source files
        for (const auto& src : all_sources) {
            compile_cmd.push_back(src);
        }

        // Execute compile command
        CommandResult compile_result = run_command(compile_cmd, temp_dir.string());
        bool compile_success = (compile_result.exit_code == 0);
        std::string output = compile_result.output;

        // Parse header dependencies if compilation succeeded
        std::map<std::string, int64_t> header_mtimes;
        if (compile_success && std::filesystem::exists(deps_file)) {
            auto headers_result = parse_deps_file(deps_file);
            if (headers_result) {
                for (const auto& header : *headers_result) {
                    auto mtime = get_file_mtime(header);
                    if (mtime) {
                        header_mtimes[header] = *mtime;
                    }
                }
            }
        }

        // If linking is needed and compile succeeded, link
        if (compile_success && (!resolved_link_libs.empty() || !link_options.empty())) {
            std::string exe_file = (temp_dir / "test").string();
            std::vector<std::string> link_cmd = {compiler_path};
            link_cmd.push_back(obj_file);
            link_cmd.push_back("-o");
            link_cmd.push_back(exe_file);

            for (const auto& lib : resolved_link_libs) {
                link_cmd.push_back(lib);
            }

            for (const auto& opt : link_options) {
                link_cmd.push_back(opt);
            }

            CommandResult link_result = run_command(link_cmd, temp_dir.string());
            if (link_result.exit_code != 0) {
                compile_success = false;
                output += "\n" + link_result.output;
            }
        }

        // Compute final signature with header deps
        auto final_sig_result = add_header_deps_to_signature(base_signature, header_mtimes);
        if (!final_sig_result) {
            interp.set_fatal_error("Failed to compute final signature: " + final_sig_result.error());
            return;
        }
        std::string final_signature = *final_sig_result;

        // Store in cache
        TryCompileCacheEntry entry;
        entry.success = compile_success;
        entry.output = output;
        entry.header_mtimes = header_mtimes;
        cache.insert<CacheSubsystem::TryCompile>(final_signature, entry);

        // Set result variables
        interp.set_variable(result_var, compile_success ? "TRUE" : "FALSE");
        if (!output_variable.empty()) {
            interp.set_variable(output_variable, output);
        }

        // Clean up temp directory on success (keep on failure for debugging)
        if (compile_success) {
            std::filesystem::remove_all(temp_dir, ec);
        }
    });
}

} // namespace dmake
