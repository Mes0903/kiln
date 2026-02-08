#include "install_executor.hpp"
#include "interperter.hpp"
#include "target.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <algorithm>

namespace dmake {

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
    std::ostream& out
) {
    // Check source exists
    if (!std::filesystem::exists(source)) {
        if (optional) {
            return {};
        }
        return std::unexpected("Source file does not exist: " + source.string());
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

    out << "-- Installing: " << destination.string() << std::endl;
    return {};
}

// Create symlinks for shared libraries
std::expected<void, std::string> create_library_symlinks(
    const std::filesystem::path& library_path,
    const std::string& soversion,
    const std::string& version,
    std::ostream& out
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
            out << "-- Installing: " << soversion_link.string() << std::endl;

            // Create libfoo.so -> libfoo.so.1
            std::filesystem::path unversioned = dir / (basename + ext);
            std::filesystem::remove(unversioned, ec);
            std::filesystem::create_symlink(soversion_link.filename(), unversioned, ec);
            if (ec) {
                return std::unexpected("Failed to create symlink " + unversioned.string() + ": " + ec.message());
            }
            out << "-- Installing: " << unversioned.string() << std::endl;
        } else {
            // Create libfoo.so -> libfoo.so.1.2.3
            std::filesystem::path unversioned = dir / (basename + ext);
            std::error_code ec;
            std::filesystem::remove(unversioned, ec);
            std::filesystem::create_symlink(versioned.filename(), unversioned, ec);
            if (ec) {
                return std::unexpected("Failed to create symlink " + unversioned.string() + ": " + ec.message());
            }
            out << "-- Installing: " << unversioned.string() << std::endl;
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
        out << "-- Installing: " << unversioned.string() << std::endl;
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
            std::string config_lower = dmake::to_lower(config);
            std::string current_lower = dmake::to_lower(current_config);
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
    std::ostream& out
) {
    for (const auto& target_name : rule.targets) {
        std::string resolved_name = interp->resolve_target_alias(target_name);
        auto& targets = interp->get_targets();
        auto it = targets.find(resolved_name);
        if (it == targets.end()) {
            return std::unexpected("Unknown target: " + target_name);
        }

        auto target = it->second;

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

        // For shared libraries with VERSION/SOVERSION, install with versioned filename
        std::string version, soversion;
        if (target->get_type() == TargetType::SHARED_LIBRARY) {
            version = target->get_property("VERSION");
            soversion = target->get_property("SOVERSION");

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
    std::ostream& out
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
        std::filesystem::path dest = dest_dir / source.filename();

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
    std::ostream& out
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
    std::ostream& out
) {
    // Check component filter
    if (!component_filter.empty() && rule.component != component_filter) {
        return {};
    }

    // TODO: Implement script/code execution
    // This would require creating a temporary interpreter and running the script
    out << "-- Note: SCRIPT/CODE install rules are not yet fully implemented" << std::endl;

    return {};
}

// Execute install(EXPORT ...) rule
std::expected<void, std::string> execute_export_rule(
    Interpreter* interp,
    const InstallExportRule& rule,
    const std::string& install_prefix,
    const std::string& component_filter,
    std::ostream& out
) {
    // Check component filter
    if (!component_filter.empty() && rule.component != component_filter) {
        return {};
    }

    // No-op - warning already printed during script interpretation
    return {};
}

} // anonymous namespace

std::expected<void, std::string> execute_install_rules(
    Interpreter* interp,
    const std::vector<std::shared_ptr<InstallRule>>& rules,
    const std::string& install_prefix,
    const std::string& current_config,
    const std::string& component_filter
) {
    std::ostream& out = std::cout;

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

} // namespace dmake
