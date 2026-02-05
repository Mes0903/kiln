#include "build_system.hpp"
#include "target.hpp"
#include "utils.hpp"
#include "module_scanner.hpp"
#include "genex_evaluator.hpp"
#include "profiler.hpp"
#include <glaze/core/reflect.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <array>
#include <thread>
#include <condition_variable>
#include <functional>
#include <glaze/glaze.hpp>
#include <type_traits>
#include <unordered_map>

namespace dmake {

struct CompileCommand {
    std::string directory;
    std::string command;
    std::string file;
    std::string output;
};

std::expected<void, std::string> BuildGraph::generate_compile_commands(const std::string& build_dir) {
    std::vector<CompileCommand> commands;
    std::string current_dir = std::filesystem::current_path().string();

    for (const auto& [id, task] : tasks_) {
        if (task.is_compilation && !task.commands.empty()) {
            commands.push_back({
                .directory = current_dir,
                .command = join_command(task.commands[0]),
                .file = task.source_file,
                .output = task.outputs.empty() ? "" : task.outputs[0]
            });
        }
    }

    std::string json;
    if (auto ec = glz::write_json(commands, json)) {
        return std::unexpected<std::string>(glz::format_error(ec));
    }

    std::filesystem::path path = std::filesystem::path(build_dir) / "compile_commands.json";
    std::ofstream file(path);
    if (file) {
        file << json;
    }
    else {
        return std::unexpected<std::string>("Failed to create compile_commands.json for writing");
    }
    return {};
}

std::expected<void, std::string> BuildGraph::finalize(const GenexEvaluationContext& ctx) {
    // Build map of outputs to task IDs for dependency inference
    // Use unordered_map for O(1) lookups
    std::unordered_map<std::string, std::string> file_to_task;
    for (const auto& [id, task] : tasks_) {
        for (const auto& out : task.outputs) {
            file_to_task[out] = id;
        }
    }

    for (auto& [id, task] : tasks_) {
        // Create per-task context with compile_language if set
        GenexEvaluationContext task_ctx = ctx;
        if (task.compile_language) {
            task_ctx.compile_language = task.compile_language;
        }
        GenexEvaluator evaluator(task_ctx);

        // Evaluate all command arguments
        std::vector<std::vector<std::string>> evaluated_commands;
        for (const auto& cmd : task.commands) {
            std::vector<std::string> evaluated_cmd;
            for (const auto& arg : cmd) {
                // Fast path: no genex
                if (!GenexParser::contains_genex(arg)) {
                    evaluated_cmd.push_back(arg);
                    continue;
                }
                auto eval = evaluator.evaluate(arg);
                if (!eval) {
                    std::string target_name = task.parent_target ? task.parent_target->get_name() : "<unknown>";
                    return std::unexpected(
                        "Generator expression error in task '" + id + "' (target '" + target_name + "')\n"
                        "  Argument: '" + arg + "'\n"
                        "  Error: " + eval.error());
                }
                // Only add non-empty results (genex can evaluate to empty string)
                if (!eval->empty()) {
                    evaluated_cmd.push_back(*eval);
                }
            }
            if (!evaluated_cmd.empty()) {
                evaluated_commands.push_back(std::move(evaluated_cmd));
            }
        }
        task.commands = std::move(evaluated_commands);

        // Infer dependencies from command arguments that reference target outputs
        // After genex expansion (e.g., $<TARGET_FILE:foo> → /path/to/foo),
        // check if any argument is an output of another task
        for (const auto& cmd : task.commands) {
            for (const auto& arg : cmd) {
                // Skip non-file arguments - most args are flags like -I, -D, etc.
                // $<TARGET_FILE:...> always returns absolute paths starting with /
                if (arg.empty() || arg[0] != '/') continue;

                // O(1) lookup - check if this path is produced by another task
                auto it = file_to_task.find(arg);
                if (it != file_to_task.end() && it->second != id) {
                    // Add as input - execute() will convert to dependency
                    task.inputs.push_back(arg);
                }
            }
        }

        // Evaluate working_dir if it contains genex
        if (!task.working_dir.empty() && GenexParser::contains_genex(task.working_dir)) {
            auto result = evaluator.evaluate(task.working_dir);
            if (!result) {
                return std::unexpected("Generator expression error in working_dir for task '" + id + "': " + result.error());
            }
            task.working_dir = *result;
        }
    }
    return {};
}

void BuildGraph::add_task(BuildTask task) {
    std::string id = task.id;
    tasks_[id] = std::move(task);
}

std::optional<std::string> BuildGraph::check_for_cycles() {
    std::map<std::string, int> color; // 0: White, 1: Gray, 2: Black
    std::vector<std::string> stack;

    std::function<std::optional<std::string>(const std::string&)> visit = [&](const std::string& u) -> std::optional<std::string> {
        color[u] = 1; // Gray
        stack.push_back(u);

        for (const auto& v : tasks_[u].dependencies) {
            if (color[v] == 1) { // Cycle!
                std::ostringstream oss;
                oss << "Circular dependency detected: ";
                for (size_t i = 0; i < stack.size(); ++i) {
                    if (stack[i] == v) {
                        for (size_t j = i; j < stack.size(); ++j) oss << tasks_[stack[j]].id << " -> ";
                        oss << v;
                        break;
                    }
                }
                return oss.str();
            }
            if (color[v] == 0) {
                auto res = visit(v);
                if (res) return res;
            }
        }

        color[u] = 2; // Black
        stack.pop_back();
        return std::nullopt;
    };

    for (const auto& [id, task] : tasks_) {
        if (color[id] == 0) {
            auto res = visit(id);
            if (res) return res;
        }
    }
    return std::nullopt;
}

std::expected<void, std::string> BuildGraph::execute(const std::string& build_dir, int jobs) {
    // 1. Resolve cross-target dependencies
    std::map<std::string, std::string> file_to_task;
    for (const auto& [id, task] : tasks_) {
        for (const auto& out : task.outputs) file_to_task[out] = id;
    }

    for (auto& [id, task] : tasks_) {
        for (const auto& in : task.inputs) {
            if (file_to_task.count(in) && file_to_task[in] != id) {
                task.dependencies.insert(file_to_task[in]);
            }
        }
    }

    auto cycle_err = check_for_cycles();
    if (cycle_err) return std::unexpected(*cycle_err);

    // 1b. Validate build graph: all inputs must exist or be produced by a task
    for (const auto& [id, task] : tasks_) {
        for (const auto& in : task.inputs) {
            if (!std::filesystem::exists(in) && !file_to_task.count(in)) {
                return std::unexpected(
                    "Build graph error: Task '" + id + "' requires '" + in +
                    "' which doesn't exist and isn't produced by any task");
            }
        }
    }

    // 2. Incremental check
    auto cache = load_cache(build_dir);
    std::map<std::string, std::string> new_cache = cache; // Preserve entries for targets not built this time

    // 3. Parallel execution
    std::set<std::string> completed;
    std::set<std::string> running;
    std::string fatal_error;
    std::mutex loop_mutex;
    std::condition_variable cv;
    std::vector<std::thread> threads;  // Track all threads for joining

    if (jobs <= 0) {
        jobs = std::thread::hardware_concurrency();
        if (jobs <= 0) jobs = 2;
    }

    auto is_ready = [&](const std::string& id) {
        for (const auto& dep : tasks_[id].dependencies) {
            if (completed.find(dep) == completed.end()) return false;
        }
        return true;
    };

    auto start_time = std::chrono::steady_clock::now();
    bool build_loop_running = true;

    while (build_loop_running) {
        std::vector<std::string> to_start;
        {
            std::unique_lock<std::mutex> lock(loop_mutex);
            if (!fatal_error.empty()) {
                build_loop_running = false;
                break;
            }
            if (completed.size() == tasks_.size()) break;

            for (const auto& [id, task] : tasks_) {
                if (completed.count(id) == 0 && running.count(id) == 0 && is_ready(id)) {
                    to_start.push_back(id);
                }
            }

            if (to_start.empty() && running.empty()) {
                // Stall detection
                std::ostringstream oss;
                oss << "Internal error: Build graph stalled. Unresolved dependencies for tasks:";
                for (const auto& [id, task] : tasks_) {
                    if (completed.count(id)) continue;
                    oss << "\n  - " << id << " depends on: ";
                    for (const auto& dep : task.dependencies) {
                        if (completed.find(dep) == completed.end()) oss << dep << " ";
                    }
                }
                fatal_error = oss.str();
                build_loop_running = false;
                break;
            }

            if (to_start.empty()) {
                cv.wait(lock);
                continue;
            }
        }

        for (const auto& id : to_start) {
            {
                std::unique_lock<std::mutex> lock(loop_mutex);
                if (running.size() >= static_cast<size_t>(jobs)) break;
                running.insert(id);
            }

            threads.emplace_back([this, id, &build_dir, &cache, &new_cache, &completed, &running, &fatal_error, &loop_mutex, &cv]() {
                const auto& task = tasks_.at(id);

                // Profile this task
                std::string profile_name;
                if (g_profiling_enabled.load(std::memory_order_relaxed)) {
                    std::string artifact = task.parent_target ? task.parent_target->get_name() : "";
                    if (task.is_module_collator) profile_name = "collate " + artifact;
                    else if (task.is_module_scanner) profile_name = "scan " + std::filesystem::path(task.source_file).filename().string();
                    else if (task.is_compilation) profile_name = "compile " + std::filesystem::path(task.source_file).filename().string();
                    else if (task.parent_target && id == task.parent_target->get_output_path())
                        profile_name = "link " + artifact;
                    else profile_name = "run " + std::filesystem::path(id).filename().string();
                }
                ProfileScope task_profile(profile_name, "build");

                // Check if outputs exist first - no need to calculate signature if we must compile anyway
                bool outputs_exist = true;
                if (task.outputs.empty()) {
                    outputs_exist = false; // No outputs means always run (unless we add something like a 'stamp' file)
                } else {
                    for (const auto& out : task.outputs) {
                        if (!std::filesystem::exists(out)) { outputs_exist = false; break; }
                    }
                }

                std::string sig;
                bool should_compile = !outputs_exist || task.always_run; // Must compile if outputs don't exist or always_run is set

                if (outputs_exist && !task.always_run) {
                    // Only calculate signature if we might skip compilation
                    auto sig_res = calculate_signature(task);
                    if (!sig_res) {
                        std::lock_guard<std::mutex> lock(loop_mutex);
                        fatal_error = sig_res.error();
                        cv.notify_all();
                        return;
                    }
                    sig = *sig_res;

                    // Check if we can skip based on signature match
                    if (cache.count(id) && cache[id] == sig) {
                        should_compile = false;
                    } else {
                        should_compile = true;
                    }
                }

                if (should_compile) {
                    std::string artifact_name = task.parent_target ? task.parent_target->get_name() : "unknown";

                    // Pre-create output directories
                    for (const auto& out : task.outputs) {
                        std::error_code ec;
                        std::filesystem::create_directories(std::filesystem::path(out).parent_path(), ec);
                        if (ec) {
                            std::lock_guard<std::mutex> lock(loop_mutex);
                            fatal_error = "Failed to create directory for " + out + ": " + ec.message();
                            cv.notify_all();
                            return;
                        }
                    }

                    // C++20 modules: handle collator tasks specially (in-process execution)
                    if (task.is_module_collator) {
                        {
                            std::lock_guard<std::mutex> lock(output_mutex_);
                            std::cout << "\033[1;32m" << std::setw(12) << "Collating" << "\033[0m ["
                                      << artifact_name << "] modules" << std::endl;
                        }

                        // Parse all DDI files from scanner tasks
                        std::map<std::string, std::string> module_to_task;  // Module name -> provider object task
                        std::map<std::string, std::vector<std::string>> task_requires;  // Object task -> required modules
                        std::vector<ModuleMapEntry> mapper_entries;

                        for (const auto& ddi_path : task.inputs) {
                            auto ddi_result = parse_ddi_file(ddi_path);
                            if (!ddi_result) {
                                std::lock_guard<std::mutex> lock(loop_mutex);
                                fatal_error = ddi_result.error();
                                cv.notify_all();
                                return;
                            }

                            const auto& ddi = *ddi_result;

                            // Find the object task for this source
                            std::string obj_path = std::filesystem::path(task.parent_target->get_binary_dir())
                                / "objs" / (std::filesystem::path(ddi.source).filename().string() + ".o");
                            obj_path = std::filesystem::path(obj_path).lexically_normal().string();

                            // If this source provides a module, record it
                            if (!ddi.provides.empty()) {
                                module_to_task[ddi.provides] = obj_path;

                                // Add to module mapper
                                ModuleMapEntry entry;
                                entry.module_name = ddi.provides;
                                entry.bmi_path = get_bmi_path(task.parent_target->get_binary_dir(), ddi.provides);
                                entry.source_path = ddi.source;
                                entry.object_task_id = obj_path;
                                mapper_entries.push_back(entry);
                            }

                            // Record what this source imports
                            if (!ddi.imports.empty()) {
                                task_requires[obj_path] = ddi.imports;
                            }
                        }

                        // Write module mapper file
                        std::string mapper_content = generate_module_mapper_content(mapper_entries);
                        std::ofstream mapper_file(task.outputs[0]);
                        if (!mapper_file) {
                            std::lock_guard<std::mutex> lock(loop_mutex);
                            fatal_error = "Failed to write module mapper: " + task.outputs[0];
                            cv.notify_all();
                            return;
                        }
                        mapper_file << mapper_content;
                        mapper_file.close();

                        // Inject dependencies into compile tasks
                        inject_module_dependencies(module_to_task, task_requires);

                    } else if (task.is_module_scanner) {
                        // C++20 modules: scanner task - run preprocessor and parse output
                        {
                            std::lock_guard<std::mutex> lock(output_mutex_);
                            std::cout << "\033[1;32m" << std::setw(12) << "Scanning" << "\033[0m ["
                                      << artifact_name << "] "
                                      << std::filesystem::path(task.source_file).filename().string() << std::endl;
                        }

                        // Run the scan command
                        auto result = run_command(task.commands[0], task.working_dir);
                        // Scanner may have non-zero exit code for parse errors but still produce useful output
                        // We capture stdout which contains preprocessed directives

                        // Parse the output to extract module info
                        ModuleDependencyInfo ddi = parse_module_scan_output(result.output, task.source_file);
                        ddi.timestamp = std::filesystem::last_write_time(task.source_file);

                        // Write DDI file
                        auto write_result = write_ddi_file(task.outputs[0], ddi);
                        if (!write_result) {
                            std::lock_guard<std::mutex> lock(loop_mutex);
                            fatal_error = write_result.error();
                            cv.notify_all();
                            return;
                        }

                    } else {
                        // Regular task execution
                        // Execute all commands in sequence
                        for (const auto& cmd : task.commands) {
                            std::string verb = "Running";
                            std::string target_display = task.source_file.empty() ?
                                std::filesystem::path(id).filename().string() :
                                std::filesystem::path(task.source_file).filename().string();

                            if (task.is_compilation) {
                                 verb = "Compiling";
                            } else if (task.parent_target && id == task.parent_target->get_output_path() && task.parent_target->get_type() != TargetType::CUSTOM) {
                                verb = "  Linking";
                            }

                            // For custom targets, use the comment if available
                            if (task.parent_target && task.parent_target->get_type() == TargetType::CUSTOM) {
                                std::string comment = task.parent_target->get_property("COMMENT");
                                if (!comment.empty()) {
                                    target_display = comment;
                                }
                            }

                            {
                                std::lock_guard<std::mutex> lock(output_mutex_);
                                std::cout << "\033[1;32m" << std::setw(12) << verb << "\033[0m ["
                                          << artifact_name << "] " << target_display << std::endl;
                            }

                            auto result = run_command(cmd, task.working_dir);
                            if (result.exit_code != 0) {
                                std::lock_guard<std::mutex> lock(output_mutex_);
                                if (!result.output.empty()) std::cerr << result.output << std::endl;

                                std::lock_guard<std::mutex> lock_loop(loop_mutex);
                                fatal_error = "Command failed: " + join_command(cmd);
                                cv.notify_all();
                                return;
                            } else if (!result.output.empty()) {
                                std::lock_guard<std::mutex> lock(output_mutex_);
                                std::cout << result.output << std::endl;
                            }
                        }
                    }

                    // Must calculate signature after compilation for cache (now .d files exist)
                    if (!task.always_run) {
                        auto new_sig_res = calculate_signature(task);
                        if (!new_sig_res) {
                            std::lock_guard<std::mutex> lock(loop_mutex);
                            fatal_error = new_sig_res.error();
                            cv.notify_all();
                            return;
                        }
                        sig = *new_sig_res;
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(loop_mutex);
                    new_cache[id] = sig;
                    completed.insert(id);
                    running.erase(id);
                    cv.notify_all();
                }
            });
        }

        std::unique_lock<std::mutex> lock(loop_mutex);
        if (running.size() >= static_cast<size_t>(jobs) || to_start.empty()) {
            cv.wait(lock);
        }
    }

    // Join all threads before returning
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    // Check if there was a fatal error during execution
    if (!fatal_error.empty()) {
        return std::unexpected(fatal_error);
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    {
        std::lock_guard<std::mutex> lock(output_mutex_);
        std::cout << "\033[1;32m" << std::setw(12) << "Finished" << "\033[0m build in "
                << std::fixed << std::setprecision(2) << duration.count() / 1000.0 << "s" << std::endl;
    }

    return save_cache(build_dir, new_cache);
}

dmake::CommandResult BuildGraph::run_command(const std::vector<std::string>& command, const std::string& working_dir) {
    return dmake::run_command(command, working_dir);
}

std::vector<std::string> BuildGraph::parse_deps_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) return {};

    std::vector<std::string> deps;
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    size_t colon = content.find(':');
    if (colon == std::string::npos) return {};

    std::string deps_part = content.substr(colon + 1);
    std::replace(deps_part.begin(), deps_part.end(), '\\', ' ');
    std::replace(deps_part.begin(), deps_part.end(), '\n', ' ');

    std::stringstream ss(deps_part);
    std::string dep;
    while (ss >> dep) {
        deps.push_back(dep);
    }
    return deps;
}

static std::expected<std::vector<std::string>, std::string> get_headers_via_h_flag(const std::vector<std::vector<std::string>>& commands) {
    if (commands.empty()) return std::vector<std::string>{};
    const auto& command = commands[0];
    
    std::vector<std::string> scan_cmd;
    scan_cmd.push_back("g++");
    scan_cmd.push_back("-H");
    scan_cmd.push_back("-E");

    std::string source;
    for (size_t i = 0; i < command.size(); ++i) {
        const auto& arg = command[i];
        if (arg.starts_with("-I") || arg.starts_with("-D") || arg.starts_with("-std=") ||
            arg.starts_with("-f") || arg == "-include") {
            scan_cmd.push_back(arg);
            if (arg == "-include" && i + 1 < command.size()) {
                scan_cmd.push_back(command[++i]);
            }
        } else if (arg.ends_with(".cpp") || arg.ends_with(".c") || arg.ends_with(".cc")) {
            source = arg;
        }
    }

    if (source.empty()) return std::vector<std::string>{};
    scan_cmd.push_back(source);

    // Redirect stdout to /dev/null for the scan command
    std::string full_cmd = join_command(scan_cmd) + " 2>&1 > /dev/null";
    
    std::array<char, 256> buffer;
    std::vector<std::string> headers;

    FILE* pipe = popen(full_cmd.c_str(), "r");
    if (!pipe) return std::unexpected("Failed to execute g++ for header scanning");

        std::string full_output;
        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
            std::string line(buffer.data());
            full_output += line;
            if (line.starts_with(".")) {
                // Lines look like ". /path/to/header.h" or ".. /path/to/header.h"
                size_t first_space = line.find(' ');
                if (first_space != std::string::npos) {
                    std::string header = line.substr(first_space + 1);
                    if (!header.empty() && header.back() == '\n') header.pop_back();
                    headers.push_back(header);
                }
            }
        }

        int status = pclose(pipe);
        if (status != 0) {
            return std::unexpected("Header scanning failed for " + source + " with exit code " + std::to_string(status) + "\nOutput:\n" + full_output);
        }
        return headers;
    }
std::filesystem::file_time_type BuildGraph::get_file_time(const std::string& path) {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        auto it = stat_cache_.find(path);
        if (it != stat_cache_.end()) return it->second;
    }

    try {
        auto time = std::filesystem::last_write_time(path);
        std::lock_guard<std::mutex> lock(state_mutex_);
        stat_cache_[path] = time;
        return time;
    } catch (...) {
        // Return a very old time if we can't stat the file
        return std::filesystem::file_time_type::min();
    }
}

std::expected<std::string, std::string> BuildGraph::calculate_signature(const BuildTask& task) {
    std::ostringstream oss;
    oss << "cmds:";
    for (const auto& cmd : task.commands) oss << join_command(cmd) << ";";
    oss << "|";

    auto version_res = get_compiler_version();
    if (!version_res) return std::unexpected(version_res.error());
    oss << "compiler:" << *version_res << "|";

    oss << "dmake:" << get_dmake_version() << "|";

    // 1. Primary inputs
    for (const auto& in : task.inputs) {
        if (std::filesystem::exists(in)) {
            oss << in << ":" << get_file_time(in).time_since_epoch().count() << "|";
        }
    }

    // 2. Header dependencies from .d files (fast path)
    // .d files are generated alongside .o files with the pattern <output>.d
    bool found_deps = false;
    for (const auto& out : task.outputs) {
        std::string deps_file = out + ".d";
        if (std::filesystem::exists(deps_file)) {
            auto deps = parse_deps_file(deps_file);
            for (const auto& dep : deps) {
                if (std::filesystem::exists(dep)) {
                    oss << "dep:" << dep << ":" << get_file_time(dep).time_since_epoch().count() << "|";
                }
            }
            found_deps = true;
        }
    }

    // 3. If no .d file exists but it's a compile task, use g++ -H (slow but accurate path)
    auto is_compile_task = task.is_compilation;
    if (!found_deps && is_compile_task) {
        auto headers_res = get_headers_via_h_flag(task.commands);
        if (!headers_res) return std::unexpected(headers_res.error());
        for (const auto& header : *headers_res) {
            if (std::filesystem::exists(header)) {
                oss << "ext_dep:" << header << ":" << get_file_time(header).time_since_epoch().count() << "|";
            }
        }
    }

    return oss.str();
}

std::map<std::string, std::string> BuildGraph::load_cache(const std::string& build_dir) {
    std::map<std::string, std::string> cache;
    std::ifstream file(std::filesystem::path(build_dir) / ".dmake_cache");
    if (!file) return cache;

    std::string line;
    while (std::getline(file, line)) {
        size_t sep = line.find('=');
        if (sep != std::string::npos) {
            cache[line.substr(0, sep)] = line.substr(sep + 1);
        }
    }
    return cache;
}

std::expected<void, std::string> BuildGraph::save_cache(const std::string& build_dir, const std::map<std::string, std::string>& cache) {
    std::error_code ec;
    std::filesystem::create_directories(build_dir, ec);
    if (ec) {
        return std::unexpected("Failed to create build directory: " + build_dir + " (" + ec.message() + ")");
    }

    std::ofstream file(std::filesystem::path(build_dir) / ".dmake_cache");
    if (!file) {
        return std::unexpected("Failed to open cache file for writing: " + (std::filesystem::path(build_dir) / ".dmake_cache").string());
    }

    for (const auto& [id, sig] : cache) {
        file << id << "=" << sig << "\n";
    }

    if (!file) {
        return std::unexpected("Failed to write to cache file");
    }

    return {};
}

std::expected<std::string, std::string> BuildGraph::get_compiler_version() {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (compiler_version_cache_) return *compiler_version_cache_;
    }

    std::array<char, 128> buffer;
    std::string result;
    FILE* pipe = popen("g++ --version 2>/dev/null | head -n 1", "r");
    if (!pipe) {
        return std::unexpected("Failed to execute g++ to get version");
    }

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }

    int status = pclose(pipe);
    if (status != 0) {
        return std::unexpected("g++ --version failed with exit code " + std::to_string(status));
    }

    if (result.empty()) {
        return std::unexpected("g++ --version produced no output");
    }

    if (result.back() == '\n') result.pop_back();

    std::lock_guard<std::mutex> lock(state_mutex_);
    compiler_version_cache_ = result;

    return *compiler_version_cache_;
}

void BuildGraph::inject_module_dependencies(
    const std::map<std::string, std::string>& module_to_task,
    const std::map<std::string, std::vector<std::string>>& task_requires) {

    std::lock_guard<std::mutex> lock(graph_mutation_mutex_);

    for (auto& [task_id, task] : tasks_) {
        // Find module requirements for this task
        auto req_it = task_requires.find(task_id);
        if (req_it == task_requires.end()) continue;

        const auto& required_modules = req_it->second;

        for (const auto& required_module : required_modules) {
            // Skip standard library modules (not built locally)
            if (required_module == "std" || required_module.starts_with("std.")) {
                continue;
            }

            auto provider_it = module_to_task.find(required_module);
            if (provider_it == module_to_task.end()) {
                // Module not found - this might be a system module or error
                // For now, we'll silently skip; proper error handling would
                // require context about whether this is expected
                continue;
            }

            const std::string& provider_task_id = provider_it->second;

            // Add dependency: this task depends on the provider task
            task.dependencies.insert(provider_task_id);

            // Update reverse dependency
            if (tasks_.count(provider_task_id)) {
                tasks_[provider_task_id].dependents.insert(task_id);
            }
        }
    }
}

} // namespace dmake
