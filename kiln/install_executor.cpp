#include "install_executor.hpp"
#include "interperter.hpp"
#include "target.hpp"
#include "genex_evaluator.hpp"
#include "printing.hpp"
#include "builtins/export_generator.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <algorithm>

namespace kiln {

namespace {

// Simple glob pattern matcher (*, ?, literal)
bool matches_glob(const std::string& text, const std::string& pattern) {
    size_t ti = 0, pi = 0;
    size_t star_p = std::string::npos, star_t = 0;

    while (ti < text.size()) {
        if (pi < pattern.size() && (pattern[pi] == '?' || pattern[pi] == text[ti])) {
            ++ti; ++pi;
        } else if (pi < pattern.size() && pattern[pi] == '*') {
            star_p = pi++; star_t = ti;
        } else if (star_p != std::string::npos) {
            pi = star_p + 1; ti = ++star_t;
        } else {
            return false;
        }
    }
    while (pi < pattern.size() && pattern[pi] == '*') ++pi;
    return pi == pattern.size();
}

// Convert CMake permissions to POSIX mode_t
mode_t parse_permissions(const std::vector<std::string>& cmake_perms, mode_t default_mode) {
    if (cmake_perms.empty()) {
        return default_mode;
    }

    mode_t mode = 0;
    for (const auto& perm : cmake_perms) {
        if (perm == "OWNER_READ") mode |= S_IRUSR;
        else if (perm == "OWNER_WRITE") mode |= S_IWUSR;
        else if (perm == "OWNER_EXECUTE") mode |= S_IXUSR;
        else if (perm == "GROUP_READ") mode |= S_IRGRP;
        else if (perm == "GROUP_WRITE") mode |= S_IWGRP;
        else if (perm == "GROUP_EXECUTE") mode |= S_IXGRP;
        else if (perm == "WORLD_READ") mode |= S_IROTH;
        else if (perm == "WORLD_WRITE") mode |= S_IWOTH;
        else if (perm == "WORLD_EXECUTE") mode |= S_IXOTH;
    }
    return mode;
}

// Install a single file with permissions
std::expected<void, std::string> install_file(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    mode_t permissions,
    bool optional,
    const OutputCtx& out
) {
    // Hard gate: any path leaving for the filesystem must have had its genex resolved.
    // Catches paths assembled from install_prefix / destination strings that bypassed
    // the build graph's evaluate_genex step.
    assert_no_genex(source.string(), "install source");
    assert_no_genex(destination.string(), "install destination");

    // Check source exists
    if (!std::filesystem::exists(source)) {
        if (optional) {
            return {};
        }
        return std::unexpected("Source file does not exist: " + source.string());
    }

    // Skip if source and destination are the same file
    std::error_code equiv_ec;
    if (std::filesystem::equivalent(source, destination, equiv_ec)) {
        print_action(out, "Up-to-date", destination.string());
        return {};
    }

    // Create destination directory
    std::filesystem::path dest_dir = destination.parent_path();
    if (!dest_dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(dest_dir, ec);
        if (ec) {
            return std::unexpected("Failed to create directory " + dest_dir.string() + ": " + ec.message());
        }
    }

    // Copy file
    std::error_code ec;
    std::filesystem::copy_file(source, destination,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return std::unexpected("Failed to copy " + source.string() + " to " + destination.string() + ": " + ec.message());
    }

    // Set permissions
    if (chmod(destination.string().c_str(), permissions) != 0) {
        return std::unexpected("Failed to set permissions on " + destination.string());
    }

    print_action(out, "Installing", destination.string());
    return {};
}

// Create symlinks for shared libraries
std::expected<void, std::string> create_library_symlinks(
    const std::filesystem::path& library_path,
    const std::string& soversion,
    const std::string& version,
    const OutputCtx& out
) {
    std::filesystem::path dir = library_path.parent_path();

    // Extract base library name from versioned filename
    // For libmylib.so.1.2.3, we want "libmylib"
    std::string filename = library_path.filename().string();
    size_t so_pos = filename.find(".so");
    if (so_pos == std::string::npos) {
        return std::unexpected("Invalid shared library filename: " + filename);
    }
    std::string basename = filename.substr(0, so_pos);
    std::string ext = ".so";

    // Create symlinks: libfoo.so -> libfoo.so.1 -> libfoo.so.1.2.3
    if (!version.empty()) {
        // Main file is libfoo.so.1.2.3
        std::filesystem::path versioned = dir / (basename + ext + "." + version);

        if (!soversion.empty()) {
            // Create libfoo.so.1 -> libfoo.so.1.2.3
            std::filesystem::path soversion_link = dir / (basename + ext + "." + soversion);
            std::error_code ec;
            std::filesystem::remove(soversion_link, ec);  // Remove if exists
            std::filesystem::create_symlink(versioned.filename(), soversion_link, ec);
            if (ec) {
                return std::unexpected("Failed to create symlink " + soversion_link.string() + ": " + ec.message());
            }
            print_action(out, "Installing", soversion_link.string());

            // Create libfoo.so -> libfoo.so.1
            std::filesystem::path unversioned = dir / (basename + ext);
            std::filesystem::remove(unversioned, ec);
            std::filesystem::create_symlink(soversion_link.filename(), unversioned, ec);
            if (ec) {
                return std::unexpected("Failed to create symlink " + unversioned.string() + ": " + ec.message());
            }
            print_action(out, "Installing", unversioned.string());
        } else {
            // Create libfoo.so -> libfoo.so.1.2.3
            std::filesystem::path unversioned = dir / (basename + ext);
            std::error_code ec;
            std::filesystem::remove(unversioned, ec);
            std::filesystem::create_symlink(versioned.filename(), unversioned, ec);
            if (ec) {
                return std::unexpected("Failed to create symlink " + unversioned.string() + ": " + ec.message());
            }
            print_action(out, "Installing", unversioned.string());
        }
    } else if (!soversion.empty()) {
        // Main file is libfoo.so.1
        std::filesystem::path soversioned = dir / (basename + ext + "." + soversion);

        // Create libfoo.so -> libfoo.so.1
        std::filesystem::path unversioned = dir / (basename + ext);
        std::error_code ec;
        std::filesystem::remove(unversioned, ec);
        std::filesystem::create_symlink(soversioned.filename(), unversioned, ec);
        if (ec) {
            return std::unexpected("Failed to create symlink " + unversioned.string() + ": " + ec.message());
        }
        print_action(out, "Installing", unversioned.string());
    }

    return {};
}

// Check if rule should be skipped based on configuration/component
bool should_skip_rule(
    const InstallDestination& dest,
    const std::string& current_config,
    const std::string& component_filter
) {
    // Check exclude_from_all
    if (dest.exclude_from_all && component_filter.empty()) {
        return true;
    }

    // Check component filter
    if (!component_filter.empty() && dest.component != component_filter) {
        return true;
    }

    // Check configuration filter
    if (!dest.configurations.empty()) {
        bool config_matches = false;
        for (const auto& config : dest.configurations) {
            std::string config_lower = kiln::to_lower(config);
            std::string current_lower = kiln::to_lower(current_config);
            if (config_lower == current_lower) {
                config_matches = true;
                break;
            }
        }
        if (!config_matches) {
            return true;
        }
    }

    return false;
}

// Execute install(TARGETS ...) rule
std::expected<void, std::string> execute_targets_rule(
    Interpreter* interp,
    const InstallTargetsRule& rule,
    const std::string& install_prefix,
    const std::string& current_config,
    const std::string& component_filter,
    const std::string& binary_dir,
    const OutputCtx& out
) {
    // TARGETS rules require interpreter to look up targets
    if (!interp) {
        // Skip - targets were already built to their final location via CMAKE_ARCHIVE_OUTPUT_DIRECTORY
        return {};
    }

    // Set up genex context for evaluating target properties (VERSION, SOVERSION, etc.)
    auto genex_ctx = GenexEvaluationContext::from_interpreter(*interp, interp->get_targets());
    genex_ctx.phase = GenexEvaluationContext::Phase::INSTALL;

    for (const auto& target_name : rule.targets) {
        auto* target = interp->find_target(target_name);
        if (!target) {
            return std::unexpected("Unknown target: " + target_name);
        }

        // Determine artifact path and destination based on target type
        std::filesystem::path artifact_path;
        const InstallDestination* dest = nullptr;
        mode_t default_perms = 0;

        if (target->get_type() == TargetType::EXECUTABLE) {
            artifact_path = target->get_output_path();
            dest = &rule.runtime_dest;
            default_perms = 0755;  // Executable
        } else if (target->get_type() == TargetType::SHARED_LIBRARY) {
            artifact_path = target->get_output_path();
            dest = &rule.library_dest;
            default_perms = 0755;  // Shared libraries are executable
        } else if (target->get_type() == TargetType::STATIC_LIBRARY) {
            artifact_path = target->get_output_path();
            dest = &rule.archive_dest;
            default_perms = 0644;  // Archive files
        } else if (target->get_type() == TargetType::INTERFACE_LIBRARY) {
            // Interface libraries have no artifacts
            continue;
        } else {
            continue;  // Skip other target types
        }

        // Skip if no destination specified
        if (dest->destination.empty()) {
            continue;
        }

        // Check if should skip based on config/component
        if (should_skip_rule(*dest, current_config, component_filter)) {
            continue;
        }

        // Compute final destination
        std::filesystem::path final_dest = std::filesystem::path(install_prefix) / dest->destination;

        // Per-target genex evaluator for property reads
        auto target_ctx = genex_ctx;
        target_ctx.current_target = target;
        GenexEvaluator eval(target_ctx);

        // For shared libraries with VERSION/SOVERSION, install with versioned filename
        std::string version, soversion;
        if (target->get_type() == TargetType::SHARED_LIBRARY) {
            version = eval.evaluate_target_property(*target, "VERSION");
            soversion = eval.evaluate_target_property(*target, "SOVERSION");

            if (!version.empty()) {
                final_dest /= "lib" + target->get_name() + ".so." + version;
            } else if (!soversion.empty()) {
                final_dest /= "lib" + target->get_name() + ".so." + soversion;
            } else {
                final_dest /= artifact_path.filename();
            }
        } else {
            final_dest /= artifact_path.filename();
        }

        // Parse permissions
        mode_t perms = parse_permissions(dest->permissions, default_perms);

        // Install the artifact
        auto res = install_file(artifact_path, final_dest, perms, dest->optional, out);
        if (!res) {
            return res;
        }

        // Create symlinks for shared libraries
        if (target->get_type() == TargetType::SHARED_LIBRARY) {
            if (!version.empty() || !soversion.empty()) {
                auto symlink_res = create_library_symlinks(final_dest, soversion, version, out);
                if (!symlink_res) {
                    return symlink_res;
                }
            }
        }

        // Install PUBLIC_HEADER files if destination specified
        if (!rule.public_header_dest.destination.empty()) {
            if (!should_skip_rule(rule.public_header_dest, current_config, component_filter)) {
                std::string public_headers = eval.evaluate_target_property(*target, "PUBLIC_HEADER");
                if (!public_headers.empty()) {
                    std::filesystem::path header_dest_dir =
                        std::filesystem::path(install_prefix) / rule.public_header_dest.destination;
                    mode_t header_perms = parse_permissions(rule.public_header_dest.permissions, 0644);

                    // PUBLIC_HEADER can be a semicolon-separated list
                    std::istringstream ss(public_headers);
                    std::string header;
                    while (std::getline(ss, header, ';')) {
                        if (header.empty()) continue;

                        std::filesystem::path header_path = header;
                        // Make relative paths absolute relative to source dir
                        if (!header_path.is_absolute()) {
                            header_path = std::filesystem::path(target->get_source_dir()) / header_path;
                        }

                        std::filesystem::path header_dest = header_dest_dir / header_path.filename();
                        auto res = install_file(header_path, header_dest, header_perms,
                                               rule.public_header_dest.optional, out);
                        if (!res) {
                            return res;
                        }
                    }
                }
            }
        }
    }

    return {};
}

// Execute install(FILES/PROGRAMS ...) rule
std::expected<void, std::string> execute_files_rule(
    Interpreter* interp,
    const InstallFilesRule& rule,
    const std::string& install_prefix,
    const std::string& current_config,
    const std::string& component_filter,
    const OutputCtx& out
) {
    // Check if should skip
    if (should_skip_rule(rule.destination, current_config, component_filter)) {
        return {};
    }

    mode_t default_perms = rule.is_programs ? 0755 : 0644;
    mode_t perms = parse_permissions(rule.destination.permissions, default_perms);

    std::filesystem::path dest_dir = std::filesystem::path(install_prefix) / rule.destination.destination;

    for (const auto& source_file : rule.files) {
        std::filesystem::path source = source_file;
        std::filesystem::path dest;

        // RENAME only valid with single file (validated during parsing)
        if (!rule.rename.empty()) {
            dest = dest_dir / rule.rename;
        } else {
            dest = dest_dir / source.filename();
        }

        auto res = install_file(source, dest, perms, rule.destination.optional, out);
        if (!res) {
            return res;
        }
    }

    return {};
}

// Execute install(DIRECTORY ...) rule
std::expected<void, std::string> execute_directory_rule(
    Interpreter* interp,
    const InstallDirectoryRule& rule,
    const std::string& install_prefix,
    const std::string& current_config,
    const std::string& component_filter,
    const OutputCtx& out
) {
    // Check if should skip
    if (should_skip_rule(rule.destination, current_config, component_filter)) {
        return {};
    }

    mode_t default_perms = 0644;

    for (const auto& source_dir : rule.directories) {
        std::filesystem::path src_path = source_dir;

        if (!std::filesystem::exists(src_path)) {
            if (rule.destination.optional) {
                continue;
            }
            return std::unexpected("Source directory does not exist: " + source_dir);
        }

        // Determine if we should preserve the directory name or not
        // If source ends with '/', install contents. Otherwise, install directory itself.
        bool install_contents = source_dir.back() == '/';

        std::filesystem::path base_dest = std::filesystem::path(install_prefix) / rule.destination.destination;

        // Recursive directory traversal
        for (const auto& entry : std::filesystem::recursive_directory_iterator(src_path)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            std::filesystem::path file_path = entry.path();
            std::filesystem::path relative = std::filesystem::relative(file_path, src_path);

            // Check patterns
            bool matches = rule.file_patterns.empty();  // If no patterns, match all
            if (!rule.file_patterns.empty()) {
                for (const auto& pattern : rule.file_patterns) {
                    if (matches_glob(file_path.filename().string(), pattern)) {
                        matches = true;
                        break;
                    }
                }
            }

            // Check exclude patterns
            for (const auto& pattern : rule.exclude_patterns) {
                if (matches_glob(file_path.filename().string(), pattern)) {
                    matches = false;
                    break;
                }
            }

            if (!matches) {
                continue;
            }

            // Compute destination
            std::filesystem::path dest;
            if (install_contents) {
                dest = base_dest / relative;
            } else {
                dest = base_dest / src_path.filename() / relative;
            }

            // Determine permissions
            mode_t perms;
            if (rule.use_source_permissions) {
                struct stat st;
                if (stat(file_path.string().c_str(), &st) == 0) {
                    perms = st.st_mode & 0777;
                } else {
                    perms = default_perms;
                }
            } else {
                perms = parse_permissions(rule.destination.permissions, default_perms);
            }

            auto res = install_file(file_path, dest, perms, rule.destination.optional, out);
            if (!res) {
                return res;
            }
        }
    }

    return {};
}

// Execute install(SCRIPT/CODE ...) rule
std::expected<void, std::string> execute_script_rule(
    Interpreter* interp,
    const InstallScriptRule& rule,
    const std::string& install_prefix,
    const std::string& component_filter,
    const OutputCtx& out
) {
    // Check component filter
    if (!component_filter.empty() && rule.component != component_filter) {
        return {};
    }

    // TODO: Implement script/code execution
    // This would require creating a temporary interpreter and running the script
    print_message(out, "WARNING", "SCRIPT/CODE install rules are not yet fully implemented");

    return {};
}

// Execute install(EXPORT ...) rule
std::expected<void, std::string> execute_export_rule(
    Interpreter* interp,
    const InstallExportRule& rule,
    const std::string& install_prefix,
    const std::string& current_config,
    const std::string& component_filter,
    const OutputCtx& out
) {
    // Check component filter
    if (!component_filter.empty() && rule.component != component_filter) {
        return {};
    }

    if (!interp) {
        return std::unexpected("install(EXPORT) requires interpreter context");
    }

    // Look up the export set
    const auto& export_sets = interp->get_export_sets();
    auto it = export_sets.find(rule.export_name);
    if (it == export_sets.end() || it->second.empty()) {
        // No targets in export set - nothing to install
        return {};
    }

    // Collect targets and remember the per-target install destinations so the
    // generator can emit IMPORTED_LOCATION at the actual install path
    // (install(TARGETS ... ARCHIVE DESTINATION foo) is non-default — hard-
    // coding lib/ would mis-locate the artifact).
    std::vector<Target*> targets_to_export;
    std::unordered_map<std::string, ExportContext::InstallDests> dests;
    for (const auto& entry : it->second) {
        auto* target = interp->find_target(entry.target_name);
        if (target) {
            targets_to_export.push_back(target);
            dests[entry.target_name] = {entry.archive_dest, entry.library_dest, entry.runtime_dest};
        }
    }

    if (targets_to_export.empty()) {
        return {};
    }

    // Determine output filename
    std::string filename = rule.file_name;
    if (filename.empty()) {
        filename = rule.export_name + ".cmake";
    }

    // Compute the destination directory
    std::filesystem::path dest_dir = std::filesystem::path(install_prefix) / rule.destination;
    std::filesystem::create_directories(dest_dir);

    // Generate export content
    ExportContext ctx;
    ctx.for_install = true;
    ctx.namespace_prefix = rule.namespace_prefix;
    ctx.destination = rule.destination;
    ctx.install_prefix = install_prefix;
    ctx.build_type = interp->get_variable("CMAKE_BUILD_TYPE");
    ctx.config = current_config;
    ctx.system_name = interp->get_variable("CMAKE_SYSTEM_NAME");
    ctx.cxx_compiler_id = interp->get_variable("CMAKE_CXX_COMPILER_ID");
    ctx.c_compiler_id = interp->get_variable("CMAKE_C_COMPILER_ID");
    ctx.cxx_compiler_version = interp->get_variable("CMAKE_CXX_COMPILER_VERSION");
    ctx.c_compiler_version = interp->get_variable("CMAKE_C_COMPILER_VERSION");
    ctx.all_targets = &interp->get_targets();
    ctx.target_aliases = &interp->get_target_aliases();
    ctx.target_install_dests = std::move(dests);

    std::string content = generate_export_content(ctx, targets_to_export);

    // Write main export file
    std::filesystem::path main_export_file = dest_dir / filename;
    std::ofstream out_file(main_export_file);
    if (!out_file) {
        return std::unexpected("Failed to create export file: " + main_export_file.string());
    }
    out_file << content;
    out_file.close();
    print_action(out, "Installing", main_export_file.string());

    // CMake also generates per-config files like MyLibTargets-release.cmake
    // Generate the config-specific file
    std::string config_lower = current_config;
    std::transform(config_lower.begin(), config_lower.end(), config_lower.begin(), ::tolower);

    // Remove .cmake extension for config file naming
    std::string base_name = filename;
    if (base_name.size() > 6 && base_name.substr(base_name.size() - 6) == ".cmake") {
        base_name = base_name.substr(0, base_name.size() - 6);
    }

    std::string config_filename = base_name + "-" + (config_lower.empty() ? "noconfig" : config_lower) + ".cmake";
    std::filesystem::path config_export_file = dest_dir / config_filename;

    // Generate config-specific content with per-config properties
    std::string config_content = generate_config_export_content(ctx, targets_to_export, install_prefix);

    std::ofstream config_out_file(config_export_file);
    if (!config_out_file) {
        return std::unexpected("Failed to create config export file: " + config_export_file.string());
    }
    config_out_file << config_content;
    config_out_file.close();
    print_action(out, "Installing", config_export_file.string());

    return {};
}

} // anonymous namespace

std::expected<void, std::string> execute_install_rules(
    Interpreter* interp,
    const std::vector<std::shared_ptr<InstallRule>>& rules,
    const std::string& install_prefix,
    const std::string& current_config,
    const std::string& component_filter,
    const OutputCtx& out
) {

    for (const auto& rule : rules) {
        switch (rule->type) {
            case InstallRuleType::TARGETS:
                {
                    auto res = execute_targets_rule(
                        interp,
                        *rule->targets_rule,
                        install_prefix,
                        current_config,
                        component_filter,
                        rule->binary_dir,
                        out
                    );
                    if (!res) return res;
                }
                break;

            case InstallRuleType::FILES:
            case InstallRuleType::PROGRAMS:
                {
                    auto res = execute_files_rule(
                        interp,
                        *rule->files_rule,
                        install_prefix,
                        current_config,
                        component_filter,
                        out
                    );
                    if (!res) return res;
                }
                break;

            case InstallRuleType::DIRECTORY:
                {
                    auto res = execute_directory_rule(
                        interp,
                        *rule->directory_rule,
                        install_prefix,
                        current_config,
                        component_filter,
                        out
                    );
                    if (!res) return res;
                }
                break;

            case InstallRuleType::SCRIPT:
            case InstallRuleType::CODE:
                {
                    auto res = execute_script_rule(
                        interp,
                        *rule->script_rule,
                        install_prefix,
                        component_filter,
                        out
                    );
                    if (!res) return res;
                }
                break;

            case InstallRuleType::EXPORT:
                {
                    auto res = execute_export_rule(
                        interp,
                        *rule->export_rule,
                        install_prefix,
                        current_config,
                        component_filter,
                        out
                    );
                    if (!res) return res;
                }
                break;
        }
    }

    return {};
}

} // namespace kiln
