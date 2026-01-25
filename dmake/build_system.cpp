#include "build_system.hpp"
#include "artifact.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <array>
#include <thread>
#include <condition_variable>
#include <functional>

namespace dmake {

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
    // 1. Resolve cross-artifact dependencies
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

    // 2. Incremental check
    auto cache = load_cache(build_dir);
    std::map<std::string, std::string> new_cache;

    // 3. Parallel execution
    std::set<std::string> completed;
    std::set<std::string> running;
    std::string fatal_error;
    std::mutex loop_mutex;
    std::condition_variable cv;

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

    while (true) {
        std::vector<std::string> to_start;
        {
            std::unique_lock<std::mutex> lock(loop_mutex);
            if (!fatal_error.empty()) return std::unexpected(fatal_error);
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
                return std::unexpected(oss.str());
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

            std::thread([this, id, &build_dir, &cache, &new_cache, &completed, &running, &fatal_error, &loop_mutex, &cv]() {
                const auto& task = tasks_.at(id);

                auto sig_res = calculate_signature(task);
                if (!sig_res) {
                    std::lock_guard<std::mutex> lock(loop_mutex);
                    fatal_error = sig_res.error();
                    cv.notify_all();
                    return;
                }
                std::string sig = *sig_res;

                bool outputs_exist = true;
                for (const auto& out : task.outputs) {
                    if (!std::filesystem::exists(out)) { outputs_exist = false; break; }
                }

                if (outputs_exist && cache.count(id) && cache[id] == sig) {
                    // Skip
                } else {
                                        std::string artifact_name = task.parent_artifact ? task.parent_artifact->get_name() : "unknown";
                                        std::string verb = "Compiling";
                                        std::string target_display = std::filesystem::path(id).filename().string();
                                        
                                        if (id == (task.parent_artifact ? task.parent_artifact->get_output_path() : "")) {
                                            verb = "  Linking";
                                        }
                    {
                        std::lock_guard<std::mutex> lock(output_mutex_);
                        std::cout << "\033[1;32m" << std::setw(12) << verb << "\033[0m ["
                                  << artifact_name << "] " << target_display << std::endl;
                    }

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

                    auto result = run_command(task.command);
                    if (result.exit_code != 0) {
                        std::lock_guard<std::mutex> lock(output_mutex_);
                        if (!result.output.empty()) std::cerr << result.output << std::endl;

                        std::lock_guard<std::mutex> lock_loop(loop_mutex);
                        fatal_error = "Command failed: " + task.command;
                        cv.notify_all();
                        return;
                    } else if (!result.output.empty()) {
                        std::lock_guard<std::mutex> lock(output_mutex_);
                        std::cout << result.output << std::endl;
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(loop_mutex);
                    new_cache[id] = sig;
                    completed.insert(id);
                    running.erase(id);
                    cv.notify_all();
                }
            }).detach();
        }

        std::unique_lock<std::mutex> lock(loop_mutex);
        if (running.size() >= static_cast<size_t>(jobs) || to_start.empty()) {
            cv.wait(lock);
        }
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

BuildGraph::CommandResult BuildGraph::run_command(const std::string& command) {
    std::string full_command = command + " 2>&1";
    FILE* pipe = popen(full_command.c_str(), "r");
    if (!pipe) return {-1, "Failed to execute command"};

    std::string output;
    std::array<char, 4096> buffer;
    while (fgets(buffer.data(), buffer.size(), pipe)) {
        output += buffer.data();
    }

    int status = pclose(pipe);
    return {WEXITSTATUS(status), output};
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

static std::expected<std::vector<std::string>, std::string> get_headers_via_h_flag(const std::string& command) {
    // Extract include flags and the source file from the command
    std::stringstream ss(command);
    std::string word;
    std::string flags;
    std::string source;

    while (ss >> word) {
        if (word.starts_with("-I") || word.starts_with("-D") || word.starts_with("-std=")) {
            flags += " " + word;
        } else if (word.ends_with(".cpp") || word.ends_with(".c") || word.ends_with(".cc")) {
            source = word;
        }
    }

    if (source.empty()) return std::vector<std::string>{};

    std::string scan_cmd = "g++ -H -E " + flags + " " + source + " 2>&1 > /dev/null";
    std::array<char, 256> buffer;
    std::vector<std::string> headers;

    FILE* pipe = popen(scan_cmd.c_str(), "r");
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
    oss << "cmd:" << task.command << "|";

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
    bool found_deps = false;
    for (const auto& out : task.outputs) {
        if (out.ends_with(".d") && std::filesystem::exists(out)) {
            auto deps = parse_deps_file(out);
            for (const auto& dep : deps) {
                if (std::filesystem::exists(dep)) {
                    oss << "dep:" << dep << ":" << get_file_time(dep).time_since_epoch().count() << "|";
                }
            }
            found_deps = true;
        }
    }

    // 3. If no .d file exists but it's a compile task, use g++ -H (slow but accurate path)
    if (!found_deps && (task.command.find(" -c ") != std::string::npos)) {
        auto headers_res = get_headers_via_h_flag(task.command);
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

} // namespace dmake
