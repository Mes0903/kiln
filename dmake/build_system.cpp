#include "build_system.hpp"
#include "target.hpp"
#include "intercept/external_project_target.hpp"
#include "utils.hpp"
#include "container_utils.hpp"
#include "module_scanner.hpp"
#include "genex_evaluator.hpp"
#include "profiler.hpp"
#include "printing.hpp"
#include "CMakeArray.hpp"
#include "progress_bar.hpp"
#include "interperter.hpp"
#include "toolchain.hpp"
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
    std::string current_dir = std::filesystem::current_path().string();

    auto commands = filter_map(tasks_,
        [](const auto& pair) { return pair.second.is_compilation && !pair.second.commands.empty(); },
        [&](const auto& pair) -> CompileCommand {
            const auto& task = pair.second;
            return {
                .directory = current_dir,
                .command = join_command(task.commands[0]),
                .file = task.source_file,
                .output = task.outputs.empty() ? "" : task.outputs[0]
            };
        });

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
                // Genex may produce semicolon-separated lists
                if (!eval->empty()) {
                    for (auto sv : CMakeArrayView(*eval)) {
                        evaluated_cmd.emplace_back(sv);
                    }
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

std::expected<std::pair<std::vector<BuildTask>, std::string>, std::string>
BuildGraph::extract_dirty_tasks(const std::string& build_dir) {
    // Similar to execute()'s pre-scan, but returns dirty tasks instead of executing them.
    // Used by EP orchestrator to extract tasks for injection into parent graph.

    // 1. Resolve cross-target dependencies (same as execute() step 1)
    std::map<std::string, std::string> file_to_task;
    for (const auto& [id, task] : tasks_) {
        for (const auto& out : task.outputs) file_to_task[out] = id;
    }
    for (auto& [id, task] : tasks_) {
        for (const auto& in : task.inputs) {
            auto it = file_to_task.find(in);
            if (it != file_to_task.end() && it->second != id) {
                task.dependencies.insert(it->second);
                tasks_[it->second].dependents.insert(id);
            }
        }
    }

    // 2. Load cache and compute dirty set
    auto cache = load_cache(build_dir);

    std::unordered_set<std::string> dirty_set;
    for (const auto& [id, task] : tasks_) {
        // Skip marker tasks
        if (task.outputs.empty() && task.commands.empty() &&
            !task.is_module_collator && !task.is_ep_orchestrator && !task.is_ep_sentinel) continue;

        // Check if outputs exist
        bool outputs_exist = true;
        for (const auto& out : task.outputs) {
            if (!std::filesystem::exists(out)) { outputs_exist = false; break; }
        }

        if (!outputs_exist || task.always_run) {
            dirty_set.insert(id);
            continue;
        }

        auto sig_res = calculate_signature(task);
        if (!sig_res || !(cache.count(id) && cache[id] == *sig_res)) {
            dirty_set.insert(id);
        }
    }

    // 3. Propagate dirtiness
    if (!dirty_set.empty()) {
        bool changed;
        do {
            changed = false;
            for (const auto& [id, task] : tasks_) {
                if (dirty_set.count(id)) continue;
                for (const auto& dep : task.dependencies) {
                    if (dirty_set.count(dep)) {
                        dirty_set.insert(id);
                        changed = true;
                        break;
                    }
                }
            }
        } while (changed);
    }

    // 4. Collect dirty tasks
    std::vector<BuildTask> dirty_tasks;
    std::string last_task_id;

    for (const auto& id : dirty_set) {
        dirty_tasks.push_back(tasks_[id]);
        // Track the "last" task - the one with no dirty dependents (final output)
        bool has_dirty_dependent = false;
        for (const auto& dep_id : tasks_[id].dependents) {
            if (dirty_set.count(dep_id)) {
                has_dirty_dependent = true;
                break;
            }
        }
        if (!has_dirty_dependent && !tasks_[id].outputs.empty()) {
            last_task_id = id;  // This could be the final task
        }
    }

    return std::make_pair(std::move(dirty_tasks), last_task_id);
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

    // 1a. CMake compatibility: ALL custom targets implicitly run before compilation.
    // CMake's Makefile generator orders ALL custom targets before regular targets,
    // so projects often rely on generated headers being available without explicit
    // add_dependencies(). We replicate this, skipping compilation tasks that are
    // transitive dependencies of any ALL custom target (to avoid cycles).
    {
        std::vector<std::string> all_custom_task_ids;
        std::set<std::string> excluded_tasks;  // union of transitive deps of all ALL custom targets

        for (const auto& [id, task] : tasks_) {
            if (!task.parent_target || !task.always_run) continue;
            auto* custom = dynamic_cast<CustomTarget*>(task.parent_target);
            if (!custom || !custom->is_build_by_default()) continue;

            all_custom_task_ids.push_back(id);

            // BFS to find all transitive dependencies
            std::vector<std::string> stack = {id};
            while (!stack.empty()) {
                std::string cur = stack.back();
                stack.pop_back();
                if (!excluded_tasks.insert(cur).second) continue;
                auto it = tasks_.find(cur);
                if (it != tasks_.end()) {
                    for (const auto& dep : it->second.dependencies) {
                        stack.push_back(dep);
                    }
                }
            }
        }

        if (!all_custom_task_ids.empty()) {
            for (auto& [id, task] : tasks_) {
                if (!task.is_compilation || excluded_tasks.count(id)) continue;
                for (const auto& ct_id : all_custom_task_ids) {
                    if (task.dependencies.count(ct_id)) continue;
                    task.dependencies.insert(ct_id);
                }
            }
        }
    }

    auto cycle_err = check_for_cycles();
    if (cycle_err) return std::unexpected(*cycle_err);

    // 1b. Build reverse dependency graph for efficient completion notification.
    // Must be done AFTER all dependencies are added (including CMake compatibility).
    for (auto& [id, task] : tasks_) {
        for (const auto& dep_id : task.dependencies) {
            if (tasks_.count(dep_id)) {
                tasks_[dep_id].dependents.insert(id);
            }
        }
    }

    // 1c. Validate all dependencies reference existing tasks (catches phantom deps
    // that would cause stalls at runtime)
    for (const auto& [id, task] : tasks_) {
        for (const auto& dep : task.dependencies) {
            if (!tasks_.count(dep)) {
                return std::unexpected(
                    "Build graph error: Task '" + id + "' depends on '" + dep +
                    "' which is not a known task");
            }
        }
    }

    // 1d. Validate build graph: inputs that don't exist and aren't produced by
    // any task are warned about but not fatal. CMake's Ninja generator resolves
    // DEPENDS target names at generation time; unresolved names become file
    // inputs that may or may not exist. We match that behavior.
    for (const auto& [id, task] : tasks_) {
        for (const auto& in : task.inputs) {
            if (!std::filesystem::exists(in) && !file_to_task.count(in)) {
                dmake::print_message(std::cerr, "WARNING",
                    "Task '" + id + "' references '" + in +
                    "' which doesn't exist and isn't produced by any task "
                    "(CMake/Ninja resolves DEPENDS at generation time)");
            }
        }
    }

    // 2. Incremental check
    auto cache = load_cache(build_dir);
    std::map<std::string, std::string> new_cache = cache; // Preserve entries for targets not built this time

    // 2b. Pre-scan: determine which tasks need to execute.
    // This serves two purposes:
    //   (a) Accurate progress count (avoids [3/2] when deps propagate dirtiness)
    //   (b) Workers skip clean tasks instantly via dirty_set lookup instead of
    //       redundantly recomputing signatures.
    ProfileScope pre_scan_profile("pre-scan incremental check", "build");
    std::unordered_set<std::string> dirty_set;
    if(tasks_.size() > 100) {
        dirty_set.reserve(20);
    }
    for (const auto& [id, task] : tasks_) {
        // Skip marker tasks (but NOT EP orchestrator/sentinel which run in-process)
        if (task.outputs.empty() && task.commands.empty() &&
            !task.is_module_collator && !task.is_ep_orchestrator && !task.is_ep_sentinel) continue;

        bool outputs_exist = !task.outputs.empty();
        if (outputs_exist) {
            for (const auto& out : task.outputs) {
                if (!get_file_time_if_exists(out)) { outputs_exist = false; break; }
            }
        }

        if (!outputs_exist || task.always_run) {
            dirty_set.insert(id);
            continue;
        }

        auto sig_res = calculate_signature(task);
        if (!sig_res || !(cache.count(id) && cache[id] == *sig_res)) {
            dirty_set.insert(id);
        }
    }

    // Propagate: any task with a dirty dependency is also dirty.
    // This includes marker tasks (no outputs, no commands) like custom target
    // grouping nodes — they must be scheduled so dependents wait for them.
    if (!dirty_set.empty()) {
        bool changed;
        do {
            changed = false;
            for (const auto& [id, task] : tasks_) {
                if (dirty_set.count(id)) continue;
                for (const auto& dep : task.dependencies) {
                    if (dirty_set.count(dep)) {
                        dirty_set.insert(id);
                        changed = true;
                        break;
                    }
                }
            }
        } while (changed);
    }
    int dirty_task_count = static_cast<int>(dirty_set.size());
    pre_scan_profile.stop();

    bool stdout_is_tty = isatty(STDOUT_FILENO);
    ProgressBar progress(dirty_task_count, stdout_is_tty);

    // 3. Parallel execution with fixed worker threads
    // Pre-populate completed with clean tasks whose deps are all clean.
    // Tasks not in dirty_set but with dirty deps (e.g. marker/grouping targets)
    // must flow through the worker loop to preserve ordering.
    std::set<std::string> completed;
    for (const auto& [id, task] : tasks_) {
        if (dirty_set.count(id)) continue;
        bool has_dirty_dep = false;
        for (const auto& dep : task.dependencies) {
            if (dirty_set.count(dep)) { has_dirty_dep = true; break; }
        }
        if (!has_dirty_dep) {
            completed.insert(id);
        }
    }
    std::set<std::string> running;
    std::string fatal_error;
    std::mutex loop_mutex;
    std::condition_variable cv;

    if (jobs <= 0) {
        jobs = std::thread::hardware_concurrency();
        if (jobs <= 0) jobs = 2;
    }

    // Check if all dependencies of a task are complete
    auto is_ready = [&](const std::string& id) {
        for (const auto& dep : tasks_[id].dependencies) {
            if (completed.find(dep) == completed.end()) return false;
        }
        return true;
    };

    // Initialize ready_set with dirty tasks whose deps are all complete.
    // Using std::set for deterministic ordering (alphabetical by task ID).
    std::set<std::string> ready_set;
    for (const std::string& id : dirty_set) {
        if (is_ready(id)) {
            ready_set.insert(id);
        }
    }

    auto start_time = std::chrono::steady_clock::now();

    std::vector<std::thread> workers;
    workers.reserve(jobs);

    for (int w = 0; w < jobs; w++) {
        workers.emplace_back([this, &build_dir, &cache, &new_cache, &completed, &running, &fatal_error, &loop_mutex, &cv, &progress, stdout_is_tty, &dirty_set, &ready_set]() {
            bool profiling = g_profiling_enabled.load(std::memory_order_relaxed);

            while (true) {
                std::string id;

                // Grab the next ready task from ready_set (O(log n) instead of O(n))
                {
                    std::unique_lock<std::mutex> lock(loop_mutex);
                    cv.wait(lock, [&] {
                        if (!fatal_error.empty()) return true;
                        if (completed.size() == tasks_.size()) return true;
                        if (!ready_set.empty()) return true;
                        if (running.empty()) return true; // stall: nothing ready and nothing in flight
                        return g_interrupted.load(std::memory_order_relaxed);
                    });

                    if (!fatal_error.empty() || completed.size() == tasks_.size()) {
                        return;
                    }
                    if (g_interrupted.load(std::memory_order_relaxed)) {
                        if (fatal_error.empty()) fatal_error = "Interrupted";
                        cv.notify_all();
                        return;
                    }

                    if (!ready_set.empty()) {
                        auto it = ready_set.begin();
                        id = *it;
                        ready_set.erase(it);
                        running.insert(id);
                    } else if (running.empty() && completed.size() < tasks_.size()) {
                        // Stall detection
                        std::ostringstream oss;
                        oss << "Internal error: Build graph stalled. Unresolved dependencies for tasks:";
                        for (const auto& [tid, task] : tasks_) {
                            if (completed.count(tid)) continue;
                            oss << "\n  - " << tid << " depends on: ";
                            for (const auto& dep : task.dependencies) {
                                if (completed.find(dep) == completed.end()) oss << dep << " ";
                            }
                        }
                        fatal_error = oss.str();
                        cv.notify_all();
                        return;
                    } else {
                        // Spurious wakeup or other condition - wait again
                        continue;
                    }
                }

                // Execute the task (outside lock)
                auto& task = tasks_.at(id);
                std::string sig;
                std::string task_error;
                std::string active_display_name;  // tracks name in progress bar's active list
                auto task_start = std::chrono::steady_clock::now();

                do { // do-while(false) for break-on-error
                    // ready_set only contains dirty tasks. Marker tasks with dirty deps
                    // are now included in dirty_set via propagation, so they flow through
                    // here naturally (executing 0 commands, then completing).

                    int64_t profile_start = 0;
                    std::string profile_name;
                    if (profiling) {
                        profile_start = Profiler::instance().now_us();
                        std::string artifact = task.parent_target ? task.parent_target->get_name() : "";
                        if (task.is_module_collator) profile_name = "collate " + artifact;
                        else if (task.is_module_scanner) profile_name = "scan " + std::filesystem::path(task.source_file).filename().string();
                        else if (task.is_compilation) profile_name = "compile " + std::filesystem::path(task.source_file).filename().string();
                        else if (task.parent_target && id == task.parent_target->get_output_path())
                            profile_name = "link " + artifact;
                        else profile_name = "run " + std::filesystem::path(id).filename().string();
                    }

                    std::string artifact_name = task.parent_target ? task.parent_target->get_name() : "unknown";

                    // Pre-create output directories and working directory
                    {
                        std::error_code ec;
                        if (!task.working_dir.empty()) {
                            std::filesystem::create_directories(task.working_dir, ec);
                        }
                        for (const auto& out : task.outputs) {
                            std::filesystem::create_directories(std::filesystem::path(out).parent_path(), ec);
                            if (ec) { task_error = "Failed to create directory for " + out + ": " + ec.message(); break; }
                        }
                    }
                    if (!task_error.empty()) break;

                    // Helper: print a permanent build status line.
                    // Uses print_line() to atomically erase bar + print + redraw
                    // in a single write, preventing flicker.
                    auto print_status = [&](std::string_view verb, std::string_view target_display) {
                        int done = progress.mark_completed();
                        active_display_name = std::string(target_display);
                        progress.task_started(active_display_name);

                        auto color = [&](std::string_view code) -> std::string_view {
                            return stdout_is_tty ? code : std::string_view{};
                        };

                        std::ostringstream oss;
                        if (stdout_is_tty) {
                            oss << color(dmake::colors::BOLD_GREEN) << std::setw(12) << verb
                                << color(dmake::colors::RESET) << " ["
                                << artifact_name << "] " << target_display;
                        } else {
                            int tot = progress.total();
                            int width = static_cast<int>(std::to_string(tot).size());
                            oss << "   [" << std::setw(width) << done << "/" << tot << "] "
                                << color(dmake::colors::BOLD_GREEN) << verb
                                << color(dmake::colors::RESET) << " ["
                                << artifact_name << "] " << target_display;
                        }

                        std::lock_guard<std::mutex> lock(output_mutex_);
                        progress.print_line(oss.str());
                    };

                    // ExternalProject: handle orchestrator tasks specially (in-process execution)
                    if (task.is_ep_orchestrator) {
                        print_status("Configuring", task.ep_name);

                        // Run the EP orchestrator under the loop_mutex since it may call inject_tasks
                        // Actually, we need to call it outside the lock and acquire the lock when injecting
                        auto ep_result = run_ep_orchestrator(task, build_dir, completed, dirty_set, ready_set, progress, new_cache, stdout_is_tty);
                        if (ep_result) {
                            task_error = *ep_result;
                            break;
                        }
                        // EP orchestrator completed successfully

                    } else if (task.is_ep_sentinel) {
                        // Sentinel task - synchronization point, signals EP completion
                        print_status("Ready", task.ep_name);

                    // C++20 modules: handle collator tasks specially (in-process execution)
                    } else if (task.is_module_collator) {
                        print_status("Collating", "modules");

                        std::map<std::string, std::string> module_to_task;
                        std::map<std::string, std::vector<std::string>> task_requires;
                        std::vector<ModuleMapEntry> mapper_entries;

                        for (const auto& ddi_path : task.inputs) {
                            auto ddi_result = parse_ddi_file(ddi_path);
                            if (!ddi_result) { task_error = ddi_result.error(); break; }

                            const auto& ddi = *ddi_result;
                            std::string obj_path = get_obj_path(task.parent_target->get_binary_dir(), task.parent_target->get_name(), ddi.source);

                            if (!ddi.provides.empty()) {
                                module_to_task[ddi.provides] = obj_path;
                                ModuleMapEntry entry;
                                entry.module_name = ddi.provides;
                                entry.bmi_path = get_bmi_path(task.parent_target->get_binary_dir(), ddi.provides);
                                entry.source_path = ddi.source;
                                entry.object_task_id = obj_path;
                                mapper_entries.push_back(entry);
                            }

                            if (!ddi.imports.empty()) {
                                task_requires[obj_path] = ddi.imports;
                            }
                        }
                        if (!task_error.empty()) break;

                        std::string mapper_content = generate_module_mapper_content(mapper_entries);
                        std::ofstream mapper_file(task.outputs[0]);
                        if (!mapper_file) { task_error = "Failed to write module mapper: " + task.outputs[0]; break; }
                        mapper_file << mapper_content;
                        mapper_file.close();

                        inject_module_dependencies(module_to_task, task_requires);

                    } else if (task.is_module_scanner) {
                        std::string scan_display = std::filesystem::path(task.source_file).filename().string();
                        print_status("Scanning", scan_display);

                        auto result = run_command(task.commands[0], task.working_dir);
                        ModuleDependencyInfo ddi = parse_module_scan_output(result.output, task.source_file);
                        ddi.timestamp = std::filesystem::last_write_time(task.source_file);

                        auto write_result = write_ddi_file(task.outputs[0], ddi);
                        if (!write_result) { task_error = write_result.error(); break; }

                    } else {
                        // Regular task execution
                        std::string verb = "Running";
                        std::string target_display = task.source_file.empty() ?
                            std::filesystem::path(id).filename().string() :
                            std::filesystem::path(task.source_file).filename().string();

                        if (task.is_compilation) {
                             verb = "Compiling";
                        } else if (task.parent_target && id == task.parent_target->get_output_path() && task.parent_target->get_type() != TargetType::CUSTOM) {
                            verb = "  Linking";
                        }

                        if (task.parent_target && task.parent_target->get_type() == TargetType::CUSTOM) {
                            std::string comment = task.parent_target->get_property("COMMENT");
                            if (!comment.empty()) {
                                target_display = comment;
                            }
                        }

                        print_status(verb, target_display);

                        for (auto cmd : task.commands) {
                            // Strip shell-style quoting from COMMAND args.
                            // CMake expects shell to strip quotes like -flag="value".
                            // Since we use execvp (no shell), we strip them here.
                            if (task.is_shell_command) {
                                for (auto& arg : cmd) {
                                    arg = strip_shell_quoting(arg);
                                }
                            }
                            auto result = dmake::run_command(cmd, task.working_dir);
                            if (result.exit_code != 0) {
                                {
                                    std::lock_guard<std::mutex> lock(output_mutex_);
                                    progress.erase();
                                    std::cout.flush();  // erase wrote to cout; flush before cerr
                                    if (!result.output.empty()) std::cerr << result.output << std::endl;
                                }
                                task_error = "Command failed: " + join_command(cmd);
                                break;
                            } else if (!result.output.empty()) {
                                std::lock_guard<std::mutex> lock(output_mutex_);
                                progress.print_line(result.output);
                            }
                        }
                        if (!task_error.empty()) break;
                    }

                    // Emit profiling event with full command details
                    if (profiling) {
                        auto dur = Profiler::instance().now_us() - profile_start;
                        Profiler::Args args;
                        if (!task.commands.empty()) {
                            std::string cmds;
                            for (const auto& cmd : task.commands) {
                                if (!cmds.empty()) cmds += " && ";
                                cmds += join_command(cmd);
                            }
                            args = std::map<std::string, std::string>{{"cmd", std::move(cmds)}};
                        }
                        Profiler::instance().add_complete(profile_name, "build", profile_start, dur, std::move(args));
                    }

                    // Invalidate stat cache for outputs so downstream tasks
                    // see fresh mtimes (e.g. link task sees recompiled .o)
                    {
                        std::lock_guard<std::mutex> lock(state_mutex_);
                        for (const auto& out : task.outputs) {
                            stat_cache_.erase(out);
                        }
                    }

                    // Recalculate signature after compilation (now .d files exist)
                    if (!task.always_run) {
                        auto new_sig_res = calculate_signature(task);
                        if (!new_sig_res) { task_error = new_sig_res.error(); break; }
                        sig = *new_sig_res;
                    }
                } while (false);

                // Remove from active task list if we added it
                if (!active_display_name.empty()) {
                    progress.task_finished(active_display_name);
                }

                // Mark task complete (or failed)
                {
                    std::lock_guard<std::mutex> lock(loop_mutex);

                    // Record execution time for critical path computation
                    auto task_end = std::chrono::steady_clock::now();
                    task.execution_time_s = std::chrono::duration<double>(task_end - task_start).count();

                    if (!task_error.empty()) {
                        fatal_error = task_error;
                    } else {
                        new_cache[id] = sig;
                    }
                    completed.insert(id);
                    running.erase(id);

                    // Check if any dirty dependents are now ready
                    for (const auto& dep_id : task.dependents) {
                        if (!dirty_set.count(dep_id)) continue;  // clean task, skip
                        if (completed.count(dep_id)) continue;   // already done
                        if (running.count(dep_id)) continue;     // already running
                        if (ready_set.count(dep_id)) continue;   // already in ready set

                        // Check if all its dependencies are complete
                        bool ready = true;
                        for (const auto& d : tasks_[dep_id].dependencies) {
                            if (!completed.count(d)) { ready = false; break; }
                        }
                        if (ready) ready_set.insert(dep_id);
                    }

                    cv.notify_all();
                }
            }
        });
    }

    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }

    // Always save cache — even on failure, successful tasks should be cached
    // so we don't redo them on the next build.
    save_cache(build_dir, new_cache);

    progress.finish();

    if (!fatal_error.empty()) {
        return std::unexpected(fatal_error);
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    // Compute critical path: longest dependency chain by execution time
    // critical_path_s[task] = execution_time_s + max(critical_path_s[dep])
    // Tasks complete in dependency order, so all deps are already computed.
    double max_critical_path = 0.0;
    if (dirty_task_count > 1) {
        // Topological traversal: process tasks whose deps are all done
        std::set<std::string> visited;
        std::function<double(const std::string&)> compute_critical = [&](const std::string& tid) -> double {
            auto it = tasks_.find(tid);
            if (it == tasks_.end()) return 0.0;
            auto& t = it->second;
            if (t.critical_path_s > 0.0) return t.critical_path_s;  // memoized
            if (!visited.insert(tid).second) return 0.0;  // cycle guard

            double dep_max = 0.0;
            for (const auto& dep : t.dependencies) {
                dep_max = std::max(dep_max, compute_critical(dep));
            }
            t.critical_path_s = t.execution_time_s + dep_max;
            return t.critical_path_s;
        };

        for (const auto& [tid, _] : tasks_) {
            max_critical_path = std::max(max_critical_path, compute_critical(tid));
        }
    }

    {
        std::lock_guard<std::mutex> lock(output_mutex_);
        double wall_s = duration.count() / 1000.0;
        std::cout << dmake::c(std::cout, dmake::colors::BOLD_GREEN) << std::setw(12) << "Finished" << dmake::c(std::cout, dmake::colors::RESET) << " build in "
                << std::fixed << std::setprecision(2) << wall_s << "s";
        if (max_critical_path > 0.0) {
            std::cout << " (critical path: " << std::setprecision(2) << max_critical_path << "s)";
        }
        std::cout << std::endl;
    }

    return {};
}

dmake::CommandResult BuildGraph::run_command(const std::vector<std::string>& command, const std::string& working_dir) {
    return dmake::run_command(command, working_dir);
}

std::vector<std::string> BuildGraph::get_deps_for_output(const std::string& output_path) {
    std::string deps_file = output_path + ".d";

    // Check if .d file exists and get its mtime
    auto d_mtime = get_file_time_if_exists(deps_file);
    if (!d_mtime) return {};

    // Check cache
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        auto it = deps_cache_.find(deps_file);
        if (it != deps_cache_.end() && it->second.d_file_mtime == *d_mtime) {
            return it->second.deps;
        }
    }

    // Cache miss - parse the .d file
    std::ifstream file(deps_file);
    if (!file) return {};

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    size_t colon = content.find(':');
    if (colon == std::string::npos) return {};

    std::string deps_part = content.substr(colon + 1);
    std::replace(deps_part.begin(), deps_part.end(), '\\', ' ');
    std::replace(deps_part.begin(), deps_part.end(), '\n', ' ');

    std::vector<std::string> deps;
    std::stringstream ss(deps_part);
    std::string dep;
    while (ss >> dep) {
        deps.push_back(dep);
    }

    // Store in cache
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        deps_cache_[deps_file] = DepsFileCache{*d_mtime, deps};
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
std::optional<std::filesystem::file_time_type> BuildGraph::get_file_time_if_exists(const std::string& path) {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        auto it = stat_cache_.find(path);
        if (it != stat_cache_.end()) return it->second;
    }

    std::optional<std::filesystem::file_time_type> result;
    try {
        result = std::filesystem::last_write_time(path);
    } catch (...) {
        // File doesn't exist or not accessible
        result = std::nullopt;
    }

    std::lock_guard<std::mutex> lock(state_mutex_);
    stat_cache_[path] = result;
    return result;
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

    // 1. Primary inputs (combined exists+stat)
    for (const auto& in : task.inputs) {
        if (auto mtime = get_file_time_if_exists(in)) {
            oss << in << ":" << mtime->time_since_epoch().count() << "|";
        }
    }

    // 2. Header dependencies from .d files (cached parsing + combined exists+stat)
    bool found_deps = false;
    for (const auto& out : task.outputs) {
        auto deps = get_deps_for_output(out);
        if (!deps.empty()) {
            for (const auto& dep : deps) {
                if (auto mtime = get_file_time_if_exists(dep)) {
                    oss << "dep:" << dep << ":" << mtime->time_since_epoch().count() << "|";
                }
            }
            found_deps = true;
        }
    }

    // 3. If no .d file exists but it's a compile task, use g++ -H (slow but accurate path)
    // Skip for ASM tasks - g++ -H doesn't work reliably with raw .s files
    auto is_compile_task = task.is_compilation;
    if (!found_deps && is_compile_task && task.compile_language != std::optional{Language::ASM}) {
        auto headers_res = get_headers_via_h_flag(task.commands);
        if (!headers_res) return std::unexpected(headers_res.error());
        for (const auto& header : *headers_res) {
            if (auto mtime = get_file_time_if_exists(header)) {
                oss << "ext_dep:" << header << ":" << mtime->time_since_epoch().count() << "|";
            }
        }
    }

    return oss.str();
}

std::map<std::string, std::string> BuildGraph::load_cache(const std::string& build_dir) {
    std::map<std::string, std::string> cache;

    // Validate build_dir hasn't moved
    std::error_code ec;
    auto canonical_build_dir = std::filesystem::canonical(build_dir, ec);
    if (ec) return cache;

    std::ifstream meta_file(std::filesystem::path(build_dir) / ".dmake_build_path");
    if (meta_file) {
        std::string cached_path;
        std::getline(meta_file, cached_path);
        if (cached_path != canonical_build_dir.string()) {
            // Build dir moved - invalidate cache
            return cache;
        }
    }

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

    // Write build_dir validation file
    auto canonical_build_dir = std::filesystem::canonical(build_dir, ec);
    if (ec) {
        return std::unexpected("Failed to canonicalize build directory: " + build_dir);
    }
    std::ofstream meta_file(std::filesystem::path(build_dir) / ".dmake_build_path");
    if (meta_file) {
        meta_file << canonical_build_dir.string();
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

void BuildGraph::inject_tasks(
    std::vector<BuildTask> new_tasks,
    const std::string& sentinel_id,
    const std::string& last_task_id,
    std::set<std::string>& completed,
    std::unordered_set<std::string>& dirty_set,
    std::set<std::string>& ready_set,
    ProgressBar& progress) {

    // This is called under loop_mutex (from run_ep_orchestrator)

    if (new_tasks.empty()) {
        // No tasks to inject - EP is clean
        return;
    }

    // 1. Build file->task map for new tasks (for dependency resolution)
    std::unordered_map<std::string, std::string> new_file_to_task;
    for (const auto& task : new_tasks) {
        for (const auto& out : task.outputs) {
            new_file_to_task[out] = task.id;
        }
    }

    // 2. Add new tasks to the graph
    for (auto& task : new_tasks) {
        tasks_[task.id] = std::move(task);
    }

    // 3. Resolve file->task dependencies for all tasks
    //    (new tasks may depend on each other, or existing tasks may depend on new outputs)
    for (auto& [id, task] : tasks_) {
        for (const auto& in : task.inputs) {
            auto it = new_file_to_task.find(in);
            if (it != new_file_to_task.end() && it->second != id) {
                task.dependencies.insert(it->second);
                tasks_[it->second].dependents.insert(id);
            }
        }
    }

    // 4. Wire sentinel to depend on the last injected task
    if (!last_task_id.empty() && tasks_.count(sentinel_id) && tasks_.count(last_task_id)) {
        tasks_[sentinel_id].dependencies.insert(last_task_id);
        tasks_[last_task_id].dependents.insert(sentinel_id);
        // Sentinel now has unsatisfied dependency - remove from ready set
        ready_set.erase(sentinel_id);
    }

    // 5. Mark all new tasks as dirty and ensure they're NOT in completed
    //    (They may be in completed if parent pre-scan marked a file path as "clean"
    //     before we injected the task that produces it)
    for (const auto& [out, task_id] : new_file_to_task) {
        dirty_set.insert(task_id);
        completed.erase(task_id);
    }
    // Also ensure sentinel is dirty
    dirty_set.insert(sentinel_id);

    // 6. Update progress total
    progress.bump_total(static_cast<int>(new_file_to_task.size()));

    // 7. Compute ready set for new tasks
    for (const auto& [out, task_id] : new_file_to_task) {
        if (completed.count(task_id)) continue;  // already done (shouldn't happen)
        if (ready_set.count(task_id)) continue;   // already ready

        bool all_deps_done = true;
        for (const auto& dep : tasks_[task_id].dependencies) {
            if (!completed.count(dep)) {
                all_deps_done = false;
                break;
            }
        }
        if (all_deps_done) {
            ready_set.insert(task_id);
        }
    }
}

std::optional<std::string> BuildGraph::run_ep_orchestrator(
    BuildTask& task,
    const std::string& build_dir,
    std::set<std::string>& completed,
    std::unordered_set<std::string>& dirty_set,
    std::set<std::string>& ready_set,
    ProgressBar& progress,
    std::map<std::string, std::string>& new_cache,
    bool stdout_is_tty) {

    // Get the ExternalProjectTarget from the task
    auto* ep_target = dynamic_cast<ExternalProjectTarget*>(task.parent_target);
    if (!ep_target) {
        return "EP orchestrator task has no ExternalProjectTarget";
    }

    std::string ep_name = ep_target->get_name();
    std::string sentinel_id = ep_name;  // Sentinel task ID is the EP name

    // Helper to print output lines with EP name prefix (for child interpreter output)
    // DIM + WHITE gives gray text for the prefix
    // Uses progress.print_line() to properly handle the progress bar
    auto print_prefixed_output = [&](const std::string& output) {
        if (output.empty()) return;
        std::string prefix = std::string(c(stdout_is_tty, colors::DIM)) + std::string(c(stdout_is_tty, colors::WHITE)) + "[" + ep_name + "] " + std::string(c(stdout_is_tty, colors::RESET));
        std::istringstream iss(output);
        std::string line;
        std::lock_guard<std::mutex> lock(output_mutex_);
        while (std::getline(iss, line)) {
            progress.print_line(prefix + line);
        }
    };

    // Check if this is a cmake-based EP or custom commands EP
    if (ep_target->is_cmake_based()) {
        // === CMAKE-BASED EP ===
        // Spawn an isolated interpreter to build the EP

        std::string source_dir = ep_target->get_effective_source_dir();
        std::string binary_dir = ep_target->get_ep_binary_dir();
        std::string install_dir = ep_target->get_ep_install_dir();

        // Create isolated interpreter
        std::stringstream ep_output;
        auto ep_interp = std::make_unique<Interpreter>(source_dir, &ep_output, &ep_output, binary_dir);

        // Force colors in child interpreter if parent stdout is a TTY
        if (stdout_is_tty) {
            ep_interp->set_force_colors(true);
        }

        // Apply CMAKE_ARGS and CMAKE_CACHE_ARGS
        for (const auto& arg : ep_target->get_cmake_args()) {
            // Parse -DVAR=value or -DVAR:TYPE=value
            if (arg.starts_with("-D")) {
                std::string def = arg.substr(2);
                size_t eq = def.find('=');
                if (eq != std::string::npos) {
                    std::string var = def.substr(0, eq);
                    std::string val = def.substr(eq + 1);
                    // Strip type annotation
                    size_t colon = var.find(':');
                    if (colon != std::string::npos) var = var.substr(0, colon);
                    ep_interp->set_variable(var, val);
                }
            }
        }
        for (const auto& arg : ep_target->get_cmake_cache_args()) {
            if (arg.starts_with("-D")) {
                std::string def = arg.substr(2);
                size_t eq = def.find('=');
                if (eq != std::string::npos) {
                    std::string var = def.substr(0, eq);
                    std::string val = def.substr(eq + 1);
                    size_t colon = var.find(':');
                    if (colon != std::string::npos) var = var.substr(0, colon);
                    ep_interp->set_variable(var, val);
                }
            }
        }

        // Set CMAKE_INSTALL_PREFIX if not already set
        if (ep_interp->get_variable("CMAKE_INSTALL_PREFIX").empty()) {
            ep_interp->set_variable("CMAKE_INSTALL_PREFIX", install_dir);
        }

        // Suppress STATUS messages in child interpreter
        ep_interp->set_variable("CMAKE_MESSAGE_LOG_LEVEL", "WARNING");

        // Read and parse CMakeLists.txt
        std::string cmake_file = source_dir + "/CMakeLists.txt";
        if (!std::filesystem::exists(cmake_file)) {
            return "EP " + ep_name + ": CMakeLists.txt not found at " + cmake_file;
        }

        std::ifstream file(cmake_file);
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        Parser parser(content, cmake_file);
        auto ast = parser.parse();
        if (!ast) {
            return "EP " + ep_name + ": Parse error in " + cmake_file + ": " + ast.error().reason;
        }

        ep_interp->set_current_file(cmake_file);

        // Interpret
        auto interp_result = ep_interp->interpret(*ast);
        if (!interp_result) {
            std::string output = ep_output.str();
            return "EP " + ep_name + ": Interpretation error: " + interp_result.error().message +
                   (output.empty() ? "" : "\nOutput:\n" + output);
        }

        // Print any buffered interpretation output (with EP name prefix)
        print_prefixed_output(ep_output.str());
        ep_output.str("");  // Clear for next phase

        // Generate dirty tasks (resolve -> generate -> dirty scan)
        auto tasks_result = ep_interp->generate_dirty_tasks({});
        if (!tasks_result) {
            std::string output = ep_output.str();
            return "EP " + ep_name + ": Task generation error: " + tasks_result.error().message +
                   (output.empty() ? "" : "\nOutput:\n" + output);
        }

        auto& [dirty_tasks, last_task_id] = *tasks_result;

        // Print any buffered task generation output
        print_prefixed_output(ep_output.str());

        // Inject dirty tasks into parent's graph
        if (!dirty_tasks.empty()) {
            inject_tasks(std::move(dirty_tasks), sentinel_id, last_task_id,
                        completed, dirty_set, ready_set, progress);
        }
        // If no dirty tasks, sentinel will complete immediately after orchestrator

    } else {
        // === CUSTOM COMMANDS EP ===
        // Run CONFIGURE_COMMAND, BUILD_COMMAND, INSTALL_COMMAND sequentially

        std::string working_dir = ep_target->get_ep_binary_dir();
        std::filesystem::create_directories(working_dir);

        // Configure step
        const auto& configure_cmd = ep_target->get_configure_command();
        if (!configure_cmd.command.empty() && !configure_cmd.is_empty) {
            auto result = dmake::run_command(configure_cmd.command, working_dir);
            if (result.exit_code != 0) {
                print_prefixed_output(result.output);
                return "EP " + ep_name + " configure failed";
            }
            print_prefixed_output(result.output);
        }

        // Build step
        const auto& build_cmd = ep_target->get_build_command();
        if (!build_cmd.command.empty() && !build_cmd.is_empty) {
            auto result = dmake::run_command(build_cmd.command, working_dir);
            if (result.exit_code != 0) {
                print_prefixed_output(result.output);
                return "EP " + ep_name + " build failed";
            }
            print_prefixed_output(result.output);
        }

        // Install step
        const auto& install_cmd = ep_target->get_install_command();
        if (!install_cmd.command.empty() && !install_cmd.is_empty) {
            auto result = dmake::run_command(install_cmd.command, working_dir);
            if (result.exit_code != 0) {
                print_prefixed_output(result.output);
                return "EP " + ep_name + " install failed";
            }
            print_prefixed_output(result.output);
        }
    }

    return std::nullopt;  // Success
}

} // namespace dmake
