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
#include "language.hpp"

namespace dmake {

class Target;
struct GenexEvaluationContext;

struct BuildTask {
    std::string id;              // Unique identifier (usually the primary output file)
    std::vector<std::vector<std::string>> commands;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    Target* parent_target = nullptr;
    bool always_run = false;
    std::string working_dir;

    // For compile_commands.json
    bool is_compilation = false;
    std::string source_file;

    // For C++20 modules support
    bool is_module_scanner = false;    // True if this is a module scanning task
    bool is_module_collator = false;   // True if this is the collator task that builds the module map
    bool is_module_source = false;     // True if this source file uses modules (imports or exports)
    std::string module_provides;       // Module name this source provides (if any)
    std::vector<std::string> module_requires;  // Module names this source requires

    // For COMPILE_LANGUAGE genex support
    std::optional<Language> compile_language;  // Language being compiled (for $<COMPILE_LANGUAGE:...>)

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

    // Finalize the build graph: evaluate all generator expressions in all tasks.
    // Call after generate_tasks() and before execute().
    std::expected<void, std::string> finalize(const GenexEvaluationContext& ctx);

    // Helpers for target task generation
    bool has_task(const std::string& id) const { return tasks_.count(id); }
    BuildTask& get_task(const std::string& id) { return tasks_.at(id); }

    // C++20 modules support: inject dependencies after collator runs
    // Called by collator task to update compile task dependencies based on module imports
    void inject_module_dependencies(
        const std::map<std::string, std::string>& module_to_task,  // Module name -> provider task ID
        const std::map<std::string, std::vector<std::string>>& task_requires  // Task ID -> required modules
    );

private:
    std::map<std::string, BuildTask> tasks_;
    mutable std::mutex output_mutex_;
    mutable std::mutex state_mutex_;
    mutable std::mutex graph_mutation_mutex_;  // For thread-safe module dependency injection

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
    CommandResult run_command(const std::vector<std::string>& command, const std::string& working_dir = "");
};

} // namespace dmake
