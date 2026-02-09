#include "registry.hpp"
#include "../interperter.hpp"
#include "../command_parser.hpp"
#include <filesystem>
#include <algorithm>

namespace dmake {

namespace {

// Map install TYPE keyword to default destination directory.
// Checks CMAKE_INSTALL_*DIR variables first, falling back to GNUInstallDirs defaults.
std::string resolve_install_type(Interpreter& interp, const std::string& type) {
    static const struct { const char* type; const char* var; const char* fallback; } mappings[] = {
        {"BIN",         "CMAKE_INSTALL_BINDIR",         "bin"},
        {"SBIN",        "CMAKE_INSTALL_SBINDIR",        "sbin"},
        {"LIB",         "CMAKE_INSTALL_LIBDIR",         "lib"},
        {"INCLUDE",     "CMAKE_INSTALL_INCLUDEDIR",     "include"},
        {"SYSCONF",     "CMAKE_INSTALL_SYSCONFDIR",     "etc"},
        {"SHAREDSTATE", "CMAKE_INSTALL_SHAREDSTATEDIR", "com"},
        {"LOCALSTATE",  "CMAKE_INSTALL_LOCALSTATEDIR",  "var"},
        {"RUNSTATE",    "CMAKE_INSTALL_RUNSTATEDIR",    "run"},
        {"DATA",        "CMAKE_INSTALL_DATADIR",        "share"},
        {"INFO",        "CMAKE_INSTALL_INFODIR",        "share/info"},
        {"LOCALE",      "CMAKE_INSTALL_LOCALEDIR",      "share/locale"},
        {"MAN",         "CMAKE_INSTALL_MANDIR",         "share/man"},
        {"DOC",         "CMAKE_INSTALL_DOCDIR",         "share/doc"},
    };
    for (const auto& m : mappings) {
        if (type == m.type) {
            auto val = interp.get_variable(m.var);
            return val.empty() ? std::string(m.fallback) : val;
        }
    }
    return {};
}

// Parse common destination properties
void parse_destination_properties(
    CommandParser& parser,
    InstallDestination& dest,
    const std::string& keyword_prefix = ""
) {
    std::string dest_keyword = keyword_prefix.empty() ? "DESTINATION" : keyword_prefix + "_DESTINATION";
    std::string perm_keyword = keyword_prefix.empty() ? "PERMISSIONS" : keyword_prefix + "_PERMISSIONS";

    parser.value(dest_keyword, dest.destination);
    parser.list(perm_keyword, dest.permissions);
    parser.value("COMPONENT", dest.component);
    parser.list("CONFIGURATIONS", dest.configurations);
    parser.flag("OPTIONAL", dest.optional);
    parser.flag("EXCLUDE_FROM_ALL", dest.exclude_from_all);
}

// Parse install(TARGETS ...) command
void parse_install_targets(
    Interpreter& interp,
    const std::vector<std::string>& args,
    const std::string& src_dir,
    const std::string& bin_dir
) {
    // Skip first argument (TARGETS keyword)
    std::vector<std::string> parse_args(args.begin() + 1, args.end());

    CommandParser parser("install(TARGETS)");

    auto rule = std::make_shared<InstallTargetsRule>();

    // Parse target list (all positional args before first keyword)
    size_t i = 0;
    std::string export_name;
    while (i < parse_args.size()) {
        const auto& arg = parse_args[i];
        // Stop at first keyword
        if (arg == "EXPORT" || arg == "ARCHIVE" || arg == "LIBRARY" || arg == "RUNTIME" ||
            arg == "PUBLIC_HEADER" || arg == "PRIVATE_HEADER" ||
            arg == "DESTINATION" || arg == "PERMISSIONS" || arg == "CONFIGURATIONS" ||
            arg == "COMPONENT" || arg == "OPTIONAL" || arg == "EXCLUDE_FROM_ALL") {
            break;
        }
        rule->targets.push_back(arg);
        ++i;
    }

    if (rule->targets.empty()) {
        interp.set_fatal_error("install(TARGETS) requires at least one target");
        return;
    }

    // Validate all targets exist
    for (const auto& target_name : rule->targets) {
        std::string resolved_name = interp.resolve_target_alias(target_name);
        auto& targets = interp.get_targets();
        if (targets.find(resolved_name) == targets.end()) {
            interp.set_fatal_error("install(TARGETS) unknown target: " + target_name);
            return;
        }
    }

    // Parse destination-specific keywords
    std::vector<std::string> remaining_args(parse_args.begin() + i, parse_args.end());

    // Track which destination type we're currently parsing
    std::string current_dest_type;
    InstallDestination* current_dest = nullptr;

    i = 0;
    while (i < remaining_args.size()) {
        const auto& arg = remaining_args[i];

        if (arg == "EXPORT") {
            // Handle EXPORT keyword - consume it and its value
            if (i + 1 >= remaining_args.size()) {
                interp.set_fatal_error("install(TARGETS) EXPORT requires a value");
                return;
            }
            export_name = remaining_args[i + 1];
            i += 2;
        } else if (arg == "ARCHIVE" || arg == "LIBRARY" || arg == "RUNTIME" ||
            arg == "PUBLIC_HEADER" || arg == "PRIVATE_HEADER") {
            current_dest_type = arg;
            if (arg == "ARCHIVE") current_dest = &rule->archive_dest;
            else if (arg == "LIBRARY") current_dest = &rule->library_dest;
            else if (arg == "RUNTIME") current_dest = &rule->runtime_dest;
            else if (arg == "PUBLIC_HEADER") current_dest = &rule->public_header_dest;
            else if (arg == "PRIVATE_HEADER") current_dest = &rule->private_header_dest;
            ++i;
        } else if (arg == "DESTINATION" && current_dest) {
            if (i + 1 >= remaining_args.size()) {
                interp.set_fatal_error("install(TARGETS) DESTINATION requires a value");
                return;
            }
            current_dest->destination = remaining_args[i + 1];
            i += 2;
        } else if (arg == "PERMISSIONS" && current_dest) {
            ++i;
            while (i < remaining_args.size() &&
                   remaining_args[i].find("_") != std::string::npos &&
                   (remaining_args[i].find("OWNER_") == 0 ||
                    remaining_args[i].find("GROUP_") == 0 ||
                    remaining_args[i].find("WORLD_") == 0)) {
                current_dest->permissions.push_back(remaining_args[i]);
                ++i;
            }
        } else if (arg == "CONFIGURATIONS" && current_dest) {
            ++i;
            while (i < remaining_args.size() &&
                   remaining_args[i] != "EXPORT" && remaining_args[i] != "ARCHIVE" &&
                   remaining_args[i] != "LIBRARY" && remaining_args[i] != "RUNTIME" &&
                   remaining_args[i] != "PUBLIC_HEADER" && remaining_args[i] != "PRIVATE_HEADER" &&
                   remaining_args[i] != "DESTINATION" && remaining_args[i] != "PERMISSIONS" &&
                   remaining_args[i] != "COMPONENT" && remaining_args[i] != "OPTIONAL" &&
                   remaining_args[i] != "EXCLUDE_FROM_ALL") {
                current_dest->configurations.push_back(remaining_args[i]);
                ++i;
            }
        } else if (arg == "COMPONENT" && current_dest) {
            if (i + 1 >= remaining_args.size()) {
                interp.set_fatal_error("install(TARGETS) COMPONENT requires a value");
                return;
            }
            current_dest->component = remaining_args[i + 1];
            i += 2;
        } else if (arg == "OPTIONAL" && current_dest) {
            current_dest->optional = true;
            ++i;
        } else if (arg == "EXCLUDE_FROM_ALL" && current_dest) {
            current_dest->exclude_from_all = true;
            ++i;
        } else {
            ++i;
        }
    }

    // Warn if EXPORT was specified
    if (!export_name.empty()) {
        interp.print_message("WARNING",
            "install(TARGETS ... EXPORT) is not yet supported - export set '" + export_name +
            "' will be ignored");
    }

    // Create install rule
    auto install_rule = std::make_shared<InstallRule>();
    install_rule->type = InstallRuleType::TARGETS;
    install_rule->source_dir = src_dir;
    install_rule->binary_dir = bin_dir;
    install_rule->targets_rule = rule;

    interp.get_install_rules().push_back(install_rule);
}

// Parse install(FILES ...) or install(PROGRAMS ...)
void parse_install_files(
    Interpreter& interp,
    const std::vector<std::string>& args,
    const std::string& src_dir,
    const std::string& bin_dir,
    bool is_programs
) {
    const char* mode = is_programs ? "PROGRAMS" : "FILES";

    // Skip first argument (FILES/PROGRAMS keyword)
    std::vector<std::string> parse_args(args.begin() + 1, args.end());

    auto rule = std::make_shared<InstallFilesRule>();
    rule->is_programs = is_programs;

    std::vector<std::string> raw_files;
    std::string type_str;

    CommandParser parser("install", mode);
    parser.positionals(raw_files, "files", false);
    parser.value("DESTINATION", rule->destination.destination);
    parser.value("TYPE", type_str);
    parser.list("PERMISSIONS", rule->destination.permissions);
    parser.list("CONFIGURATIONS", rule->destination.configurations);
    parser.value("COMPONENT", rule->destination.component);
    parser.flag("OPTIONAL", rule->destination.optional);
    parser.flag("EXCLUDE_FROM_ALL", rule->destination.exclude_from_all);
    PARSE_OR_RETURN(parser, interp, parse_args);

    // No files provided - CMake silently accepts this (undocumented behavior)
    if (raw_files.empty()) {
        interp.print_message("WARNING",
            "install(" + std::string(mode) + ") called with no files - ignoring (undocumented CMake behavior)", true);
        return;
    }

    // Resolve TYPE to destination
    if (!type_str.empty()) {
        auto dest = resolve_install_type(interp, type_str);
        if (dest.empty()) {
            interp.set_fatal_error("install(" + std::string(mode) + ") unknown TYPE: " + type_str);
            return;
        }
        rule->destination.destination = dest;
    }

    if (rule->destination.destination.empty()) {
        interp.set_fatal_error("install(" + std::string(mode) + ") requires DESTINATION or TYPE");
        return;
    }

    // Resolve file paths to absolute
    for (const auto& f : raw_files) {
        std::filesystem::path file_path = f;
        if (!file_path.is_absolute()) {
            file_path = std::filesystem::path(src_dir) / file_path;
        }
        rule->files.push_back(file_path.lexically_normal().string());
    }

    // Create install rule
    auto install_rule = std::make_shared<InstallRule>();
    install_rule->type = is_programs ? InstallRuleType::PROGRAMS : InstallRuleType::FILES;
    install_rule->source_dir = src_dir;
    install_rule->binary_dir = bin_dir;
    install_rule->files_rule = rule;

    interp.get_install_rules().push_back(install_rule);
}

// Parse install(DIRECTORY ...)
void parse_install_directory(
    Interpreter& interp,
    const std::vector<std::string>& args,
    const std::string& src_dir,
    const std::string& bin_dir
) {
    // Skip first argument (DIRECTORY keyword)
    std::vector<std::string> parse_args(args.begin() + 1, args.end());

    auto rule = std::make_shared<InstallDirectoryRule>();

    // Parse directory list
    size_t i = 0;
    while (i < parse_args.size()) {
        const auto& arg = parse_args[i];
        if (arg == "DESTINATION" || arg == "TYPE" || arg == "FILE_PERMISSIONS" ||
            arg == "DIRECTORY_PERMISSIONS" || arg == "USE_SOURCE_PERMISSIONS" ||
            arg == "FILES_MATCHING" || arg == "PATTERN" ||
            arg == "CONFIGURATIONS" || arg == "COMPONENT" || arg == "OPTIONAL" ||
            arg == "EXCLUDE_FROM_ALL") {
            break;
        }
        // Resolve relative paths
        std::filesystem::path dir_path = arg;
        if (!dir_path.is_absolute()) {
            dir_path = std::filesystem::path(src_dir) / dir_path;
        }
        rule->directories.push_back(dir_path.lexically_normal().string());
        ++i;
    }

    if (rule->directories.empty()) {
        interp.set_fatal_error("install(DIRECTORY) requires at least one directory");
        return;
    }

    // Parse remaining arguments
    std::vector<std::string> remaining_args(parse_args.begin() + i, parse_args.end());

    i = 0;
    while (i < remaining_args.size()) {
        const auto& arg = remaining_args[i];

        if (arg == "DESTINATION") {
            if (i + 1 >= remaining_args.size()) {
                interp.set_fatal_error("install(DIRECTORY) DESTINATION requires a value");
                return;
            }
            rule->destination.destination = remaining_args[i + 1];
            i += 2;
        } else if (arg == "TYPE") {
            if (i + 1 >= remaining_args.size()) {
                interp.set_fatal_error("install(DIRECTORY) TYPE requires a value");
                return;
            }
            auto dest = resolve_install_type(interp, remaining_args[i + 1]);
            if (dest.empty()) {
                interp.set_fatal_error("install(DIRECTORY) unknown TYPE: " + remaining_args[i + 1]);
                return;
            }
            rule->destination.destination = dest;
            i += 2;
        } else if (arg == "USE_SOURCE_PERMISSIONS") {
            rule->use_source_permissions = true;
            ++i;
        } else if (arg == "PATTERN") {
            if (i + 1 >= remaining_args.size()) {
                interp.set_fatal_error("install(DIRECTORY) PATTERN requires a value");
                return;
            }
            ++i;
            std::string pattern = remaining_args[i];
            ++i;

            // Check if followed by EXCLUDE
            bool is_exclude = false;
            if (i < remaining_args.size() && remaining_args[i] == "EXCLUDE") {
                is_exclude = true;
                ++i;
            }

            if (is_exclude) {
                rule->exclude_patterns.push_back(pattern);
            } else {
                rule->file_patterns.push_back(pattern);
            }
        } else if (arg == "FILES_MATCHING") {
            ++i;
        } else if (arg == "COMPONENT") {
            if (i + 1 >= remaining_args.size()) {
                interp.set_fatal_error("install(DIRECTORY) COMPONENT requires a value");
                return;
            }
            rule->destination.component = remaining_args[i + 1];
            i += 2;
        } else if (arg == "CONFIGURATIONS") {
            ++i;
            while (i < remaining_args.size() &&
                   remaining_args[i] != "DESTINATION" && remaining_args[i] != "PATTERN" &&
                   remaining_args[i] != "COMPONENT" && remaining_args[i] != "OPTIONAL" &&
                   remaining_args[i] != "EXCLUDE_FROM_ALL" && remaining_args[i] != "USE_SOURCE_PERMISSIONS") {
                rule->destination.configurations.push_back(remaining_args[i]);
                ++i;
            }
        } else if (arg == "OPTIONAL") {
            rule->destination.optional = true;
            ++i;
        } else if (arg == "EXCLUDE_FROM_ALL") {
            rule->destination.exclude_from_all = true;
            ++i;
        } else {
            ++i;
        }
    }

    if (rule->destination.destination.empty()) {
        interp.set_fatal_error("install(DIRECTORY) requires DESTINATION or TYPE");
        return;
    }

    // Create install rule
    auto install_rule = std::make_shared<InstallRule>();
    install_rule->type = InstallRuleType::DIRECTORY;
    install_rule->source_dir = src_dir;
    install_rule->binary_dir = bin_dir;
    install_rule->directory_rule = rule;

    interp.get_install_rules().push_back(install_rule);
}

// Parse install(SCRIPT ...) or install(CODE ...)
void parse_install_script(
    Interpreter& interp,
    const std::vector<std::string>& args,
    const std::string& src_dir,
    const std::string& bin_dir,
    bool is_script
) {
    const char* mode = is_script ? "SCRIPT" : "CODE";

    // Skip first argument (SCRIPT/CODE keyword)
    std::vector<std::string> parse_args(args.begin() + 1, args.end());

    auto rule = std::make_shared<InstallScriptRule>();
    std::string content;

    CommandParser parser("install", mode);
    parser.positional(content, is_script ? "script_path" : "code", true);
    parser.value("COMPONENT", rule->component);
    PARSE_OR_RETURN(parser, interp, parse_args);

    if (is_script) {
        std::filesystem::path script_path = content;
        if (!script_path.is_absolute()) {
            script_path = std::filesystem::path(src_dir) / script_path;
        }
        rule->script_path = script_path.lexically_normal().string();
    } else {
        rule->code = content;
    }

    // Create install rule
    auto install_rule = std::make_shared<InstallRule>();
    install_rule->type = is_script ? InstallRuleType::SCRIPT : InstallRuleType::CODE;
    install_rule->source_dir = src_dir;
    install_rule->binary_dir = bin_dir;
    install_rule->script_rule = rule;

    interp.get_install_rules().push_back(install_rule);
}

// Parse install(EXPORT ...)
void parse_install_export(
    Interpreter& interp,
    const std::vector<std::string>& args,
    const std::string& src_dir,
    const std::string& bin_dir
) {
    // Skip first argument (EXPORT keyword)
    std::vector<std::string> parse_args(args.begin() + 1, args.end());

    auto rule = std::make_shared<InstallExportRule>();

    CommandParser parser("install", "EXPORT");
    parser.positional(rule->export_name, "export_name", true);
    parser.value("FILE", rule->file_name);
    parser.value("NAMESPACE", rule->namespace_prefix);
    parser.value("DESTINATION", rule->destination);
    parser.value("COMPONENT", rule->component);
    PARSE_OR_RETURN(parser, interp, parse_args);

    // Print warning immediately during script interpretation
    interp.print_message("WARNING",
        "install(EXPORT) is not yet supported - export set '" + rule->export_name +
        "' will not generate CMake target files");

    // Create install rule (as no-op for now)
    auto install_rule = std::make_shared<InstallRule>();
    install_rule->type = InstallRuleType::EXPORT;
    install_rule->source_dir = src_dir;
    install_rule->binary_dir = bin_dir;
    install_rule->export_rule = rule;

    interp.get_install_rules().push_back(install_rule);
}

} // anonymous namespace

void register_install_builtins(Interpreter& interp) {
    interp.add_builtin("install", [](Interpreter& interp, const std::vector<std::string>& args) {
        if (args.empty()) {
            interp.set_fatal_error("install() requires arguments");
            return;
        }

        std::string mode = args[0];
        std::string src_dir = interp.get_variable("CMAKE_CURRENT_SOURCE_DIR");
        std::string bin_dir = interp.get_variable("CMAKE_CURRENT_BINARY_DIR");

        if (mode == "TARGETS") {
            parse_install_targets(interp, args, src_dir, bin_dir);
        } else if (mode == "FILES") {
            parse_install_files(interp, args, src_dir, bin_dir, false);
        } else if (mode == "PROGRAMS") {
            parse_install_files(interp, args, src_dir, bin_dir, true);
        } else if (mode == "DIRECTORY") {
            parse_install_directory(interp, args, src_dir, bin_dir);
        } else if (mode == "SCRIPT") {
            parse_install_script(interp, args, src_dir, bin_dir, true);
        } else if (mode == "CODE") {
            parse_install_script(interp, args, src_dir, bin_dir, false);
        } else if (mode == "EXPORT") {
            parse_install_export(interp, args, src_dir, bin_dir);
        } else {
            interp.set_fatal_error("install() first argument must be TARGETS, FILES, PROGRAMS, DIRECTORY, SCRIPT, CODE, or EXPORT");
        }
    });

    interp.add_builtin("export", [](Interpreter& interp, const std::vector<std::string>& args) {
        // Export is not yet supported - just show a warning and ignore
        std::string export_name = "unknown";
        if (!args.empty()) {
            export_name = args[0];
        }
        interp.print_message("WARNING", "export() is not yet supported - ignoring export of '" + export_name + "'", true);
    });
}

} // namespace dmake
