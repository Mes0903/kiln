#include "external_project.hpp"
#include "download_utils.hpp"
#include "../interperter.hpp"
#include "../command_parser.hpp"
#include "../utils.hpp"
#include <filesystem>
#include <algorithm>

namespace dmake {

void register_external_project_builtins(Interpreter& interp) {

    interp.add_builtin("externalproject_add", [](Interpreter& interp, const std::vector<std::string>& args) {
        CommandParser parser("ExternalProject_Add");
        std::string name;
        // Directory options
        std::string prefix, source_dir, binary_dir, install_dir, tmp_dir, stamp_dir, download_dir;
        // URL download
        std::vector<std::string> url_list;
        std::string url_hash, url_md5;
        // Git download
        std::string git_repository, git_tag;
        bool git_shallow = false;
        // Build options
        std::vector<std::string> cmake_args, cmake_cache_args;
        std::string list_separator;
        // Step commands
        std::vector<std::string> configure_command, build_command, install_command;
        std::vector<std::string> patch_command, update_command, test_command;
        // Misc
        std::vector<std::string> depends, build_byproducts;
        bool build_in_source = false, exclude_from_all = false, build_always = false;
        std::string download_no_extract_str;
        std::vector<std::string> download_command;
        std::string source_subdir;

        parser.positional(name, "name");
        parser.value("PREFIX", prefix);
        parser.value("SOURCE_DIR", source_dir);
        parser.value("BINARY_DIR", binary_dir);
        parser.value("INSTALL_DIR", install_dir);
        parser.value("TMP_DIR", tmp_dir);
        parser.value("STAMP_DIR", stamp_dir);
        parser.value("DOWNLOAD_DIR", download_dir);
        parser.value("SOURCE_SUBDIR", source_subdir);

        parser.list("URL", url_list);
        parser.value("URL_HASH", url_hash);
        parser.value("URL_MD5", url_md5);

        parser.value("GIT_REPOSITORY", git_repository);
        parser.value("GIT_TAG", git_tag);
        parser.flag("GIT_SHALLOW", git_shallow);

        parser.list("CMAKE_ARGS", cmake_args);
        parser.list("CMAKE_CACHE_ARGS", cmake_cache_args);
        parser.value("LIST_SEPARATOR", list_separator);

        parser.list("CONFIGURE_COMMAND", configure_command);
        parser.list("BUILD_COMMAND", build_command);
        parser.list("INSTALL_COMMAND", install_command);
        parser.list("PATCH_COMMAND", patch_command);
        parser.list("UPDATE_COMMAND", update_command);
        parser.list("TEST_COMMAND", test_command);
        parser.list("DOWNLOAD_COMMAND", download_command);

        parser.list("DEPENDS", depends);
        parser.list("BUILD_BYPRODUCTS", build_byproducts);
        parser.flag("BUILD_IN_SOURCE", build_in_source);
        parser.flag("EXCLUDE_FROM_ALL", exclude_from_all);
        parser.flag("BUILD_ALWAYS", build_always);
        parser.value("DOWNLOAD_NO_EXTRACT", download_no_extract_str);

        // Ignored options (accepted but not used)
        std::string log_download, log_configure, log_build, log_install;
        std::string log_update, log_patch, log_test, log_merged_stdouterr, log_output_on_failure;
        bool uses_terminal_download = false, uses_terminal_configure = false;
        bool uses_terminal_build = false, uses_terminal_install = false;
        std::vector<std::string> git_config;
        std::string git_remote_name, git_submodules, git_submodules_recurse;
        std::string git_progress;
        std::string timeout, download_extract_timestamp;
        std::vector<std::string> http_header;

        parser.value("LOG_DOWNLOAD", log_download);
        parser.value("LOG_CONFIGURE", log_configure);
        parser.value("LOG_BUILD", log_build);
        parser.value("LOG_INSTALL", log_install);
        parser.value("LOG_UPDATE", log_update);
        parser.value("LOG_PATCH", log_patch);
        parser.value("LOG_TEST", log_test);
        parser.value("LOG_MERGED_STDOUTERR", log_merged_stdouterr);
        parser.value("LOG_OUTPUT_ON_FAILURE", log_output_on_failure);
        parser.flag("USES_TERMINAL_DOWNLOAD", uses_terminal_download);
        parser.flag("USES_TERMINAL_CONFIGURE", uses_terminal_configure);
        parser.flag("USES_TERMINAL_BUILD", uses_terminal_build);
        parser.flag("USES_TERMINAL_INSTALL", uses_terminal_install);
        parser.list("GIT_CONFIG", git_config);
        parser.value("GIT_REMOTE_NAME", git_remote_name);
        parser.value("GIT_SUBMODULES", git_submodules);
        parser.value("GIT_SUBMODULES_RECURSE", git_submodules_recurse);
        parser.value("GIT_PROGRESS", git_progress);
        parser.value("TIMEOUT", timeout);
        parser.value("DOWNLOAD_EXTRACT_TIMESTAMP", download_extract_timestamp);
        parser.list("HTTPHEADER", http_header);
        parser.list("HTTP_HEADER", http_header);

        PARSE_OR_RETURN(parser, interp, args);

        if (name.empty()) {
            interp.set_fatal_error("ExternalProject_Add requires a name");
            return;
        }

        // Resolve prefix
        std::string bin_dir = interp.get_variable("CMAKE_CURRENT_BINARY_DIR");
        if (prefix.empty()) {
            prefix = bin_dir + "/" + name + "-prefix";
        }
        if (!std::filesystem::path(prefix).is_absolute()) {
            prefix = bin_dir + "/" + prefix;
        }

        // Resolve directories
        if (source_dir.empty()) source_dir = prefix + "/src/" + name;
        else if (!std::filesystem::path(source_dir).is_absolute())
            source_dir = bin_dir + "/" + source_dir;

        if (binary_dir.empty()) {
            if (build_in_source) binary_dir = source_dir;
            else binary_dir = prefix + "/src/" + name + "-build";
        } else if (!std::filesystem::path(binary_dir).is_absolute()) {
            binary_dir = bin_dir + "/" + binary_dir;
        }

        if (install_dir.empty()) install_dir = prefix;
        else if (!std::filesystem::path(install_dir).is_absolute())
            install_dir = bin_dir + "/" + install_dir;

        if (tmp_dir.empty()) tmp_dir = prefix + "/tmp";
        if (stamp_dir.empty()) stamp_dir = prefix + "/src/" + name + "-stamp";
        if (download_dir.empty()) download_dir = prefix + "/src";

        // Apply SOURCE_SUBDIR
        std::string effective_source_dir = source_dir;
        if (!source_subdir.empty()) {
            effective_source_dir = source_dir + "/" + source_subdir;
        }

        // Token replacements for step commands
        std::vector<std::pair<std::string, std::string>> tokens = {
            {"<SOURCE_DIR>", source_dir},
            {"<SOURCE_SUBDIR>", effective_source_dir},
            {"<BINARY_DIR>", binary_dir},
            {"<INSTALL_DIR>", install_dir},
            {"<TMP_DIR>", tmp_dir},
        };

        // Store properties on a CustomTarget
        std::string src_dir_cmake = interp.get_variable("CMAKE_CURRENT_SOURCE_DIR");
        auto target = std::make_shared<CustomTarget>(name, src_dir_cmake, bin_dir);
        target->set_build_by_default(!exclude_from_all);

        // Store all directories as target properties for ExternalProject_Get_Property
        target->set_property("_EP_SOURCE_DIR", source_dir);
        target->set_property("_EP_BINARY_DIR", binary_dir);
        target->set_property("_EP_INSTALL_DIR", install_dir);
        target->set_property("_EP_PREFIX", prefix);
        target->set_property("_EP_TMP_DIR", tmp_dir);
        target->set_property("_EP_STAMP_DIR", stamp_dir);
        target->set_property("_EP_DOWNLOAD_DIR", download_dir);
        if (!source_subdir.empty()) target->set_property("_EP_SOURCE_SUBDIR", source_subdir);

        for (const auto& dep : depends) {
            target->add_custom_dependency(dep);
        }

        // === Execute steps at configure time (like CMake does) ===

        interp.print_message("STATUS", "ExternalProject_Add: " + name);

        // 1. Download step
        bool source_dir_has_content = false;
        if (std::filesystem::exists(source_dir)) {
            auto it = std::filesystem::directory_iterator(source_dir);
            source_dir_has_content = (it != std::filesystem::directory_iterator{});
        }

        if (!source_dir_has_content) {
            if (!download_command.empty()) {
                // Custom download command
                replace_tokens(download_command, tokens);
                interp.print_message("STATUS", "  Downloading " + name + " (custom command)...");
                auto result = run_steps({download_command});
                if (!result) {
                    interp.set_fatal_error("ExternalProject_Add(" + name + ") download failed: " + result.error());
                    return;
                }
            } else if (!url_list.empty()) {
                // URL download (or local file)
                std::string url = url_list[0];

                // Parse hash
                std::string hash_algo, hash_value;
                if (!url_hash.empty()) {
                    auto eq_pos = url_hash.find('=');
                    if (eq_pos != std::string::npos) {
                        hash_algo = url_hash.substr(0, eq_pos);
                        hash_value = url_hash.substr(eq_pos + 1);
                        std::transform(hash_algo.begin(), hash_algo.end(), hash_algo.begin(),
                                       [](unsigned char c) { return std::toupper(c); });
                    }
                } else if (!url_md5.empty()) {
                    hash_algo = "MD5";
                    hash_value = url_md5;
                }

                // Detect local file vs remote URL
                bool is_local = std::filesystem::exists(url);
                if (!is_local && url.starts_with("file://")) {
                    url = url.substr(7);
                    is_local = true;
                }

                std::string archive_path;
                if (is_local) {
                    archive_path = url;
                    interp.print_message("STATUS", "  Using local archive for " + name + ": " + archive_path);
                } else {
                    std::string filename = std::filesystem::path(url).filename().string();
                    auto qpos = filename.find('?');
                    if (qpos != std::string::npos) filename = filename.substr(0, qpos);
                    archive_path = download_dir + "/" + filename;

                    interp.print_message("STATUS", "  Downloading " + name + " from " + url + "...");
                    auto dl_result = download_url(url, archive_path, hash_algo, hash_value);
                    if (!dl_result) {
                        interp.set_fatal_error("ExternalProject_Add(" + name + ") download failed: " + dl_result.error());
                        return;
                    }
                }

                // Extract unless DOWNLOAD_NO_EXTRACT
                bool no_extract = (!download_no_extract_str.empty() &&
                                   download_no_extract_str != "OFF" &&
                                   download_no_extract_str != "FALSE" &&
                                   download_no_extract_str != "0");
                if (!no_extract) {
                    interp.print_message("STATUS", "  Extracting " + name + "...");
                    auto ex_result = extract_archive(archive_path, source_dir);
                    if (!ex_result) {
                        interp.set_fatal_error("ExternalProject_Add(" + name + ") extraction failed: " + ex_result.error());
                        return;
                    }

                    // Many archives have a single top-level directory. Move contents up if so.
                    std::vector<std::filesystem::directory_entry> entries;
                    for (const auto& e : std::filesystem::directory_iterator(source_dir)) {
                        entries.push_back(e);
                    }
                    if (entries.size() == 1 && entries[0].is_directory()) {
                        std::string top_dir = entries[0].path().string();
                        std::string temp_dir = source_dir + ".__tmp_move";
                        std::filesystem::rename(top_dir, temp_dir);
                        for (const auto& e : std::filesystem::directory_iterator(temp_dir)) {
                            std::filesystem::rename(e.path(), std::filesystem::path(source_dir) / e.path().filename());
                        }
                        std::filesystem::remove_all(temp_dir);
                    }
                }
            } else if (!git_repository.empty()) {
                interp.print_message("STATUS", "  Cloning " + name + " from " + git_repository + "...");
                auto git_result = git_clone(git_repository, source_dir, git_tag, git_shallow);
                if (!git_result) {
                    interp.set_fatal_error("ExternalProject_Add(" + name + ") git clone failed: " + git_result.error());
                    return;
                }
            }
        } else {
            interp.print_message("STATUS", "  " + name + " source directory already exists, skipping download.");
        }

        // 2. Patch step
        // PATCH_COMMAND may contain multiple commands separated by "COMMAND" tokens
        if (!patch_command.empty()) {
            replace_tokens(patch_command, tokens);
            interp.print_message("STATUS", "  Patching " + name + "...");

            // Split on "COMMAND" tokens to get individual commands
            std::vector<std::vector<std::string>> commands;
            std::vector<std::string> current_cmd;
            for (const auto& token : patch_command) {
                if (token == "COMMAND") {
                    if (!current_cmd.empty()) {
                        commands.push_back(std::move(current_cmd));
                        current_cmd.clear();
                    }
                } else {
                    current_cmd.push_back(token);
                }
            }
            if (!current_cmd.empty()) {
                commands.push_back(std::move(current_cmd));
            }

            for (const auto& cmd : commands) {
                auto result = run_command(cmd, source_dir);
                if (result.exit_code != 0) {
                    interp.set_fatal_error("ExternalProject_Add(" + name + ") patch failed:\n" + result.output);
                    return;
                }
            }
        }

        // 3. Configure step
        bool configure_is_empty = (configure_command.size() == 1 && configure_command[0].empty());
        if (!configure_command.empty() && !configure_is_empty) {
            // Custom configure command
            replace_tokens(configure_command, tokens);
            interp.print_message("STATUS", "  Configuring " + name + " (custom)...");
            auto result = run_command(configure_command, binary_dir);
            if (result.exit_code != 0) {
                interp.set_fatal_error("ExternalProject_Add(" + name + ") configure failed:\n" + result.output);
                return;
            }
        } else if (configure_is_empty) {
            // Empty string means skip configure
        } else {
            // Default: CMake configure
            if (std::filesystem::exists(effective_source_dir + "/CMakeLists.txt")) {
                std::filesystem::create_directories(binary_dir);
                std::string dmake_path = get_executable_path();

                std::vector<std::string> cmd = {dmake_path};
                cmd.push_back("-S");
                cmd.push_back(effective_source_dir);
                cmd.push_back("-B");
                cmd.push_back(binary_dir);

                // Process LIST_SEPARATOR in cmake_args
                auto process_cmake_arg = [&](const std::string& arg) {
                    std::string processed = arg;
                    if (!list_separator.empty()) {
                        size_t pos = 0;
                        while ((pos = processed.find(list_separator, pos)) != std::string::npos) {
                            processed.replace(pos, list_separator.length(), ";");
                            pos += 1;
                        }
                    }
                    return processed;
                };

                for (const auto& arg : cmake_args) {
                    std::string processed = process_cmake_arg(arg);
                    // Token replacement
                    for (const auto& [token, value] : tokens) {
                        size_t pos = 0;
                        while ((pos = processed.find(token, pos)) != std::string::npos) {
                            processed.replace(pos, token.length(), value);
                            pos += value.length();
                        }
                    }
                    cmd.push_back(processed);
                }
                for (const auto& arg : cmake_cache_args) {
                    std::string processed = process_cmake_arg(arg);
                    for (const auto& [token, value] : tokens) {
                        size_t pos = 0;
                        while ((pos = processed.find(token, pos)) != std::string::npos) {
                            processed.replace(pos, token.length(), value);
                            pos += value.length();
                        }
                    }
                    cmd.push_back(processed);
                }

                // Add default install prefix if not already specified
                bool has_install_prefix = false;
                for (const auto& arg : cmd) {
                    if (arg.find("CMAKE_INSTALL_PREFIX") != std::string::npos) {
                        has_install_prefix = true;
                        break;
                    }
                }
                if (!has_install_prefix) {
                    cmd.push_back("-DCMAKE_INSTALL_PREFIX=" + install_dir);
                }

                interp.print_message("STATUS", "  Configuring+Building " + name + "...");
                auto result = run_command(cmd);
                if (result.exit_code != 0) {
                    interp.set_fatal_error("ExternalProject_Add(" + name + ") configure+build failed:\n" + result.output);
                    return;
                }
            }
        }

        // 4. Build step
        bool build_is_empty = (build_command.size() == 1 && build_command[0].empty());
        if (!build_command.empty() && !build_is_empty) {
            replace_tokens(build_command, tokens);
            interp.print_message("STATUS", "  Building " + name + " (custom)...");
            auto result = run_command(build_command, binary_dir);
            if (result.exit_code != 0) {
                interp.set_fatal_error("ExternalProject_Add(" + name + ") build failed:\n" + result.output);
                return;
            }
        }
        // Default build: no-op for dmake (configure step already builds)

        // 5. Install step
        bool install_is_empty = (install_command.size() == 1 && install_command[0].empty());
        if (!install_command.empty() && !install_is_empty) {
            replace_tokens(install_command, tokens);
            interp.print_message("STATUS", "  Installing " + name + " (custom)...");
            auto result = run_command(install_command, binary_dir);
            if (result.exit_code != 0) {
                interp.set_fatal_error("ExternalProject_Add(" + name + ") install failed:\n" + result.output);
                return;
            }
        }

        // Register the target (as a custom target with no commands since steps ran at configure)
        interp.get_targets()[name] = target;
        interp.get_current_directory_context().owned_targets.push_back(target);

        interp.print_message("STATUS", "ExternalProject_Add: " + name + " complete.");
    });

    interp.add_builtin("externalproject_get_property", [](Interpreter& interp, const std::vector<std::string>& args) {
        // ExternalProject_Get_Property(<name> <prop1> [<prop2>...])
        if (args.size() < 2) {
            interp.set_fatal_error("ExternalProject_Get_Property requires a target name and at least one property");
            return;
        }

        std::string target_name = args[0];
        auto& targets = interp.get_targets();
        auto it = targets.find(target_name);
        if (it == targets.end()) {
            interp.set_fatal_error("ExternalProject_Get_Property: target '" + target_name + "' not found");
            return;
        }

        auto& target = it->second;
        for (size_t i = 1; i < args.size(); ++i) {
            std::string prop_name = args[i];
            std::string upper_prop = prop_name;
            std::transform(upper_prop.begin(), upper_prop.end(), upper_prop.begin(),
                           [](unsigned char c) { return std::toupper(c); });

            std::string value = target->get_property("_EP_" + upper_prop);

            // Set variable with the lowercase property name (CMake convention)
            std::string var_name = prop_name;
            std::transform(var_name.begin(), var_name.end(), var_name.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            interp.set_variable(var_name, value);
        }
    });

    // Stub for ExternalProject_Add_Step (just ignore)
    interp.add_builtin("externalproject_add_step", [](Interpreter& interp, const std::vector<std::string>&) {
        // Silently ignore - steps are not used in our model
    });

    // Stub for ExternalProject_Add_StepTargets
    interp.add_builtin("externalproject_add_steptargets", [](Interpreter& interp, const std::vector<std::string>&) {
        // Silently ignore
    });

    // Stub for ExternalProject_Add_StepDependencies
    interp.add_builtin("externalproject_add_stepdependencies", [](Interpreter& interp, const std::vector<std::string>&) {
        // Silently ignore
    });
}

} // namespace dmake
