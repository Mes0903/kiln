#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <expected>
#include <optional>
#include <filesystem>
#include <mutex>
#include "utils.hpp"

namespace dmake {

class Target;

struct BuildTask {
    std::string id;              // Unique identifier (usually the primary output file)
    std::vector<std::string> command;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    Target* parent_target = nullptr;

    // For compile_commands.json
    bool is_compilation = false;
    std::string source_file;

    // For graph execution
    std::set<std::string> dependencies; // Task IDs we depend on
    std::set<std::string> dependents;   // Task IDs that depend on us
};

class BuildGraph {
public:
    void add_task(BuildTask task);

    // Checks for cycles and returns an error message if one is found
    std::optional<std::string> check_for_cycles();

    // Executes the graph.
    std::expected<void, std::string> execute(const std::string& build_dir, int jobs = 0);

    std::expected<void, std::string> generate_compile_commands(const std::string& build_dir);

    // Helpers for target task generation
    bool has_task(const std::string& id) const { return tasks_.count(id); }
    BuildTask& get_task(const std::string& id) { return tasks_.at(id); }

private:
    std::map<std::string, BuildTask> tasks_;
    mutable std::mutex output_mutex_;
    mutable std::mutex state_mutex_;

    // Incremental build logic
    std::expected<std::string, std::string> calculate_signature(const BuildTask& task);
    std::map<std::string, std::string> load_cache(const std::string& build_dir);
    std::expected<void, std::string> save_cache(const std::string& build_dir, const std::map<std::string, std::string>& cache);

    std::filesystem::file_time_type get_file_time(const std::string& path);
    std::map<std::string, std::filesystem::file_time_type> stat_cache_;

    std::expected<std::string, std::string> get_compiler_version();
    std::optional<std::string> compiler_version_cache_;
    std::string get_dmake_version() { return "0.1.0-alpha (task-refactor)"; }

    // Parsers for .d files (header dependencies)
    std::vector<std::string> parse_deps_file(const std::string& path);

    // Subprocess execution with output capture
    CommandResult run_command(const std::vector<std::string>& command);
};

} // namespace dmake
