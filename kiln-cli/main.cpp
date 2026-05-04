#include "kiln/cmake-language.hpp"
#include "kiln/interperter.hpp"
#include "kiln/printing.hpp"
#include "kiln/utils.hpp"
#include "kiln/install_executor.hpp"
#include "kiln/profiler.hpp"
#include "kiln/tool_mode.hpp"
#include "kiln/debugger.hpp"
#include "kiln/parse_number.hpp"
#include <linenoise.h>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <CLI/CLI.hpp>
#include <unistd.h>
#include <future>
#include "kiln/regex.hpp"
#include "kiln/genex_evaluator.hpp"
#include "kiln/genex_parser.hpp"
#include "kiln/CMakeArray.hpp"
#include <chrono>
#include <iomanip>
#include <csignal>
#include "kiln/build_system.hpp"

namespace {

void signal_handler(int sig) {
    if (kiln::g_interrupted.exchange(true, std::memory_order_relaxed)) {
        // Second signal — restore default handler and re-raise for hard kill
        signal(sig, SIG_DFL);
        raise(sig);
    }
}

struct GlobalOptions {
    int jobs = 0;
    std::string build_dir_str;
    std::vector<std::string> definitions;
    std::string config = "debug";
    std::string script_path;
    std::string source_dir_str;  // -S flag for ExternalProject recursive invocation
    std::string project_dir_str = ".";  // -C flag for project directory
    bool profile = false;
    bool trace = false;
    bool trace_expand = false;
    bool debugger = false;
    bool config_only = false;  // Debug: interpret + save cache, then exit (no build)
    bool no_sys_init = false;   // Skip compiler detection (for benchmarking)
    bool fresh = false;         // Skip loading persistent cache (fresh configure)
    bool fast_setup = false;    // Use kiln's hardcoded compiler-vars subset
                                // instead of including CMake's Compiler/<id>-<lang>.cmake
    std::string break_on_message;
    std::string log_level;       // --log-level (sets CMAKE_MESSAGE_LOG_LEVEL)
};

void print_error_context(const std::string& file_path, size_t row, size_t col, size_t offset, size_t length, const std::string& message, const std::vector<kiln::CallLocation>& backtrace = {}, const std::optional<std::string>& source_content = std::nullopt) {
    kiln::print_diagnostic(std::cerr, kiln::DiagnosticSeverity::Error, message, file_path, row, col, offset, length, backtrace, source_content);
}

void print_error_context(const kiln::InterpreterError& error) {
    print_error_context(error.file, error.row, error.col, error.offset, error.length, error.message, error.backtrace, error.source_content);
}

std::string to_cmake_case(const std::string& config) {
    if (config.empty()) return "";
    std::string result = config;
    result[0] = std::toupper(result[0]);
    if (result == "Relwithdebinfo") return "RelWithDebInfo";
    if (result == "Minsizerel") return "MinSizeRel";
    return result;
}

void apply_definitions(kiln::Interpreter& interpreter, const std::vector<std::string>& definitions) {
    for (const auto& def : definitions) {
        size_t eq = def.find('=');
        if (eq != std::string::npos) {
            std::string var_name = def.substr(0, eq);
            std::string value = def.substr(eq + 1);

            // Strip type annotation if present (e.g., VAR:STRING=value -> VAR=value)
            size_t colon = var_name.find(':');
            if (colon != std::string::npos) {
                var_name = var_name.substr(0, colon);
            }

            // CMake's -D sets a cache variable. Some scripts check
            // `if(NOT DEFINED CACHE{VAR})` to decide whether they were given
            // a value via the command line; setting only a normal variable
            // here would cause those checks to take the wrong branch.
            interpreter.set_cache_variable(var_name, value);
            interpreter.set_variable(var_name, value);
        } else {
            // Handle -DVAR or -DVAR:BOOL (no value)
            std::string var_name = def;
            size_t colon = var_name.find(':');
            if (colon != std::string::npos) {
                var_name = var_name.substr(0, colon);
            }
            interpreter.set_cache_variable(var_name, "ON");
            interpreter.set_variable(var_name, "ON");
        }
    }
}

void set_default_flags(kiln::Interpreter& interpreter, const std::vector<std::string>& definitions, const std::string& var, const std::string& flags) {
    bool already_set = false;
    for (const auto& def : definitions) {
        // Check for VAR=value or VAR:TYPE=value or VAR or VAR:TYPE
        std::string def_var = def;
        size_t eq = def_var.find('=');
        if (eq != std::string::npos) {
            def_var = def_var.substr(0, eq);
        }
        size_t colon = def_var.find(':');
        if (colon != std::string::npos) {
            def_var = def_var.substr(0, colon);
        }

        if (def_var == var) {
            already_set = true;
            break;
        }
    }
    if (!already_set && interpreter.get_variable(var).empty()) {
        interpreter.set_variable(var, flags);
    }
}

std::expected<std::unique_ptr<kiln::Interpreter>, std::string> run_build_action(const GlobalOptions& opt, kiln::DebugController& debug_controller, const std::string& project_dir, const std::vector<std::string>& targets, bool is_test_mode = false) {
    try {
        std::filesystem::path project_path = std::filesystem::canonical(project_dir);
        std::filesystem::path cmake_lists = project_path / "CMakeLists.txt";

        if (!std::filesystem::exists(cmake_lists)) {
            return std::unexpected("CMakeLists.txt not found in " + project_path.string());
        }

        std::filesystem::path build_path;
        std::filesystem::path build_root_path;  // For KILN_BUILD_ROOT

        if (!opt.source_dir_str.empty()) {
            // -S mode: use -B as-is (no config appended), used for ExternalProject recursive builds
            if (opt.build_dir_str.empty()) {
                return std::unexpected("-S requires -B to be specified");
            }
            build_path = std::filesystem::absolute(opt.build_dir_str).lexically_normal();
            // In -S mode, build_root is the parent of build_path (EP creates config-specific dirs)
            build_root_path = build_path.parent_path();
        } else {
            build_root_path = opt.build_dir_str.empty() ? (project_path / "build") : std::filesystem::absolute(opt.build_dir_str).lexically_normal();
            build_path = build_root_path / opt.config;
        }

        if (build_path == project_path) {
            return std::unexpected("Build directory cannot be source directory");
        }

        std::ifstream file(cmake_lists);
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        kiln::ProfileScope parse_profile("parse " + cmake_lists.filename().string(), "parse");
        kiln::Parser parser(content, cmake_lists.string());
        auto ast_or_error = parser.parse();
        parse_profile.stop();

        if (!ast_or_error) {
            const auto& error = ast_or_error.error();
            print_error_context(cmake_lists.string(), error.row, error.col, error.offset, error.length, error.reason);
            return std::unexpected("Parse error");
        }

        kiln::ProfileScope init_profile("setup for execution", "init");

        // Sniff -D args for toolchain overrides. If any are present, eager
        // host detection in the Interpreter ctor would be wasted (the
        // toolchain file or explicit compiler will redirect us anyway). On-
        // demand detection inside enable_compiler_for_language handles the
        // actual case.
        bool skip_host_detect = false;
        for (const auto& def : opt.definitions) {
            // Match "VAR=..." or "VAR:TYPE=..." forms.
            auto starts_with_var = [&](std::string_view name) {
                if (def.size() < name.size()) return false;
                if (std::string_view(def).substr(0, name.size()) != name) return false;
                char next = def[name.size()];
                return next == '=' || next == ':';
            };
            if (starts_with_var("CMAKE_TOOLCHAIN_FILE") ||
                starts_with_var("CMAKE_C_COMPILER") ||
                starts_with_var("CMAKE_CXX_COMPILER") ||
                starts_with_var("CMAKE_ASM_COMPILER") ||
                starts_with_var("CMAKE_SYSROOT") ||
                starts_with_var("CMAKE_C_COMPILER_TARGET") ||
                starts_with_var("CMAKE_CXX_COMPILER_TARGET")) {
                skip_host_detect = true;
                break;
            }
        }

        auto interpreter = std::make_unique<kiln::Interpreter>(project_path.string(), &std::cout, &std::cerr, build_path.string(), opt.no_sys_init, opt.fresh, skip_host_detect);
        interpreter->set_current_file(cmake_lists.string());
        debug_controller.attach(*interpreter);

        // Set KILN_BUILD_ROOT - shared location for EP/FetchContent sources
        interpreter->set_variable("KILN_BUILD_ROOT", build_root_path.string());

        // --fast-setup: surface as an internal variable so enable_language can
        // pick it up and skip including Compiler/<id>-<lang>.cmake.
        if (opt.fast_setup) interpreter->set_variable("KILN_FAST_SETUP", "1");

        // Initialize FETCHCONTENT_BASE_DIR (like CMake's FetchContent module does on include)
        // This must be set before any FetchContent_Declare is parsed since args reference it
        interpreter->set_cache_variable("FETCHCONTENT_BASE_DIR", build_root_path.string() + "/_deps");

        // Set BUILD_TESTING as a cache variable (like CMake's -D does).
        // option() only skips if a cache entry exists, so a normal variable
        // would be invisible to it and projects using include(CTest) would
        // have their option(BUILD_TESTING ... ON) override our default.
        interpreter->set_cache_variable("BUILD_TESTING", is_test_mode ? "ON" : "OFF");
        apply_definitions(*interpreter, opt.definitions);

        if (!opt.log_level.empty()) {
            interpreter->set_variable("CMAKE_MESSAGE_LOG_LEVEL", opt.log_level);
        }

        if (interpreter->get_variable("CMAKE_BUILD_TYPE").empty()) {
            interpreter->set_variable("CMAKE_BUILD_TYPE", to_cmake_case(opt.config));
        }

        set_default_flags(*interpreter, opt.definitions, "CMAKE_CXX_FLAGS_DEBUG", "-g -O0");
        set_default_flags(*interpreter, opt.definitions, "CMAKE_CXX_FLAGS_RELEASE", "-O3 -DNDEBUG");
        set_default_flags(*interpreter, opt.definitions, "CMAKE_CXX_FLAGS_RELWITHDEBINFO", "-g -O2 -DNDEBUG");
        set_default_flags(*interpreter, opt.definitions, "CMAKE_CXX_FLAGS_MINSIZEREL", "-Os -DNDEBUG");
        init_profile.stop();

        // Save subsystem cache on all exit paths where the interpreter exists.
        // Cache entries (try_compile, find_*, globs) are valid regardless of
        // whether later commands fail, so persisting them avoids redundant work
        // when re-running after a configuration or build error.
        auto save_cache = [&]() {
            auto cache_save_result = interpreter->get_cache_store().save();
            if (!cache_save_result) {
                std::cerr << kiln::c(std::cerr, kiln::colors::BOLD_YELLOW) << "warning:" << kiln::c(std::cerr, kiln::colors::RESET) << " Failed to save cache: " << cache_save_result.error() << std::endl;
            }
        };

        {
            auto config_start = std::chrono::steady_clock::now();
            kiln::ProfileScope scope("interpret " + cmake_lists.filename().string(), "interpret");
            auto interpret_result = interpreter->interpret(ast_or_error.value());
            if (!interpret_result) {
                print_error_context(interpret_result.error());
                save_cache();
                return std::unexpected("Interpretation error");
            }

            // Execute deferred calls and apply retroactive directory properties
            interpreter->execute_deferred_calls();
            interpreter->finalize_directory_targets();
            scope.stop();

            double config_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - config_start).count();
            std::ostringstream timing_msg;
            timing_msg << "Configuring done (" << std::fixed << std::setprecision(1) << config_s << "s)";
            kiln::print_message(std::cout, "STATUS", timing_msg.str());
        }

        if (opt.config_only) {
            save_cache();
            if (kiln::g_profiling_enabled.load(std::memory_order_relaxed)) {
                auto profile_path = (build_path / "profile.json").string();
                kiln::Profiler::instance().write(profile_path);
                std::cerr << kiln::c(std::cerr, kiln::colors::BOLD_CYAN) << "Profile" << kiln::c(std::cerr, kiln::colors::RESET) << " written to " << profile_path << std::endl;
            }
            return interpreter;
        }

        auto build_result = interpreter->run_build(opt.jobs, targets);
        if (!build_result) {
            if (!kiln::g_interrupted.load(std::memory_order_relaxed)) {
                std::cerr << kiln::c(std::cerr, kiln::colors::BOLD_RED) << "error:" << kiln::c(std::cerr, kiln::colors::RESET) << " " << build_result.error().message << std::endl;
            }
            save_cache();
            return std::unexpected(kiln::g_interrupted.load(std::memory_order_relaxed) ? "Interrupted" : "Build failed");
        }

        save_cache();

        // Write profile if enabled
        if (kiln::g_profiling_enabled.load(std::memory_order_relaxed)) {
            auto profile_path = (build_path / "profile.json").string();
            kiln::Profiler::instance().write(profile_path);
            std::cerr << kiln::c(std::cerr, kiln::colors::BOLD_CYAN) << "Profile" << kiln::c(std::cerr, kiln::colors::RESET) << " written to " << profile_path << std::endl;
        }

        return interpreter;
    } catch (const std::exception& e) {
        return std::unexpected(std::string("Exception: ") + e.what());
    }
}

// Must insure build is up to date before running tests
int run_test_action(const GlobalOptions& opt, kiln::Interpreter* interpreter, const std::string& pattern) {
    // Process TEST_INCLUDE_FILES from all directory contexts (for gtest_discover_tests etc.)
    for (const auto& [dir, ctx] : interpreter->get_all_directory_contexts()) {
        auto it = ctx.properties.find("TEST_INCLUDE_FILES");
        if (it == ctx.properties.end()) continue;
        for (auto file_it = kiln::CMakeArrayIterator::iterator(it->second);
             file_it != kiln::CMakeArrayIterator::sentinel{}; ++file_it) {
            std::string include_path(*file_it);
            if (include_path.empty()) continue;
            if (!std::filesystem::exists(include_path)) {
                std::cerr << kiln::c(std::cerr, kiln::colors::BOLD_YELLOW)
                          << "Warning: " << kiln::c(std::cerr, kiln::colors::RESET)
                          << "TEST_INCLUDE_FILES entry not found: " << include_path << std::endl;
                continue;
            }
            auto result = interpreter->include_file(include_path);
            if (!result) {
                print_error_context(result.error());
                return 1;
            }
        }
    }

    auto& tests = interpreter->get_tests();
    auto& targets_map = interpreter->get_targets();

    std::vector<kiln::TestDefinition*> selected_tests;
    std::set<std::string> targets_to_build;

    std::optional<kiln::Regex> filter;
    bool has_filter = !pattern.empty();
    if (has_filter) {
        auto re = kiln::Regex::compile(pattern);
        if (!re) {
            std::cerr << "Error: Invalid test pattern regex: " << re.error() << std::endl;
            return 1;
        }
        filter = std::move(*re);
    }

    // Create genex context for evaluating generator expressions in test commands/args
    auto genex_ctx = kiln::Target::make_genex_context(nullptr, *interpreter, targets_map, std::nullopt, false);

    for (auto& test : tests) {
        if (has_filter && !filter->search(test.name)) {
            continue;
        }

        // Evaluate genex in test command and arguments
        auto eval = [&](std::string& s) {
            if (kiln::GenexParser::contains_genex(s)) {
                kiln::GenexEvaluator evaluator(genex_ctx);
                auto result = evaluator.evaluate(s);
                if (result) {
                    s = std::move(*result);
                }
            }
        };
        eval(test.command);
        for (auto& arg : test.args) {
            eval(arg);
        }
        eval(test.working_dir);

        selected_tests.push_back(&test);
        if (targets_map.count(test.command)) {
            targets_to_build.insert(test.command);
        }
    }

    if (selected_tests.empty()) {
        std::cout << "No tests matched the pattern." << std::endl;
        return 0;
    }

    std::vector<std::string> target_list(targets_to_build.begin(), targets_to_build.end());

    std::cout << kiln::c(std::cout, kiln::colors::BOLD_BLUE) << "Running " << selected_tests.size() << " tests..." << kiln::c(std::cout, kiln::colors::RESET) << std::endl;

    struct TestResult {
        std::string name;
        bool passed;
        bool skipped;
        bool timed_out;
        double duration;
        std::string output;
    };

    auto start_all = std::chrono::high_resolution_clock::now();

    // Build dependency map and validate DEPENDS references
    std::set<std::string> selected_names;
    for (auto* test : selected_tests) {
        selected_names.insert(test->name);
    }

    // Parse DEPENDS for each test, warn on unknown deps (supported for compat, but wrong)
    std::map<std::string, std::vector<std::string>> deps_map;
    // Parse RESOURCE_LOCK for each test
    std::map<std::string, std::vector<std::string>> resource_locks_map;
    for (auto* test : selected_tests) {
        auto it = test->properties.find("DEPENDS");
        if (it != test->properties.end()) {
            auto& deps = deps_map[test->name];
            for (auto dep_it = kiln::CMakeArrayIterator::iterator(it->second);
                 dep_it != kiln::CMakeArrayIterator::sentinel{}; ++dep_it) {
                std::string dep(*dep_it);
                if (selected_names.find(dep) == selected_names.end()) {
                    std::cerr << kiln::c(std::cerr, kiln::colors::BOLD_YELLOW)
                              << "Warning: " << kiln::c(std::cerr, kiln::colors::RESET)
                              << "Test '" << test->name << "' DEPENDS on unknown test '" << dep
                              << "' (ignored)" << std::endl;
                } else {
                    deps.push_back(dep);
                }
            }
        }

        auto lock_it = test->properties.find("RESOURCE_LOCK");
        if (lock_it != test->properties.end()) {
            auto& locks = resource_locks_map[test->name];
            for (auto l_it = kiln::CMakeArrayIterator::iterator(lock_it->second);
                 l_it != kiln::CMakeArrayIterator::sentinel{}; ++l_it) {
                std::string lock(*l_it);
                if (!lock.empty()) locks.push_back(lock);
            }
        }
    }

    // Run a single test and return its result
    auto run_one_test = [&targets_map](kiln::TestDefinition* test) -> TestResult {
        TestResult res;
        res.name = test->name;
        res.passed = false;
        res.skipped = false;
        res.timed_out = false;

        auto start = std::chrono::high_resolution_clock::now();

        std::string cmd = test->command;
        if (targets_map.count(cmd)) {
            cmd = targets_map[cmd]->get_output_path();
        }

        std::vector<std::string> command_vec;
        command_vec.push_back(cmd);
        for (const auto& arg : test->args) {
            command_vec.push_back(arg);
        }

        // Check for TIMEOUT property
        double timeout = 0.0;
        auto timeout_it = test->properties.find("TIMEOUT");
        if (timeout_it != test->properties.end()) {
            auto v = kiln::parse_double(timeout_it->second);
            if (!v) {
                res.output = "Error: Invalid TIMEOUT value '" + timeout_it->second + "'\n";
                res.duration = 0.0;
                return res;
            }
            timeout = *v;
        }

        // Check for SKIP_RETURN_CODE property
        std::optional<int> skip_code;
        auto skip_it = test->properties.find("SKIP_RETURN_CODE");
        if (skip_it != test->properties.end()) {
            auto v = kiln::parse_number<int>(skip_it->second);
            if (!v) {
                res.output = "Error: Invalid SKIP_RETURN_CODE value '" + skip_it->second + "'\n";
                res.duration = 0.0;
                return res;
            }
            skip_code = *v;
        }

        // WORKING_DIRECTORY property overrides the add_test() working directory
        std::string working_dir = test->working_dir;
        auto wd_it = test->properties.find("WORKING_DIRECTORY");
        if (wd_it != test->properties.end() && !wd_it->second.empty()) {
            working_dir = wd_it->second;
        }

        // Execute the command with timeout handling
        std::future<kiln::CommandResult> cmd_future = std::async(std::launch::async, [&]() {
            return kiln::run_command(command_vec, working_dir);
        });

        kiln::CommandResult result;
        if (timeout > 0.0) {
            auto timeout_duration = std::chrono::duration<double>(timeout);
            if (cmd_future.wait_for(timeout_duration) == std::future_status::timeout) {
                res.timed_out = true;
                res.passed = false;
                res.output = "Test timed out after " + std::to_string(timeout) + " seconds\n";
                auto end = std::chrono::high_resolution_clock::now();
                res.duration = std::chrono::duration<double>(end - start).count();
                return res;
            }
            result = cmd_future.get();
        } else {
            result = cmd_future.get();
        }

        res.output = result.output;

        // Check SKIP_REGULAR_EXPRESSION — skip if output matches any pattern
        auto skip_re_it = test->properties.find("SKIP_REGULAR_EXPRESSION");
        if (skip_re_it != test->properties.end()) {
            for (auto pat_it = kiln::CMakeArrayIterator::iterator(skip_re_it->second);
                 pat_it != kiln::CMakeArrayIterator::sentinel{}; ++pat_it) {
                std::string pat(*pat_it);
                if (pat.empty()) continue;
                auto re = kiln::Regex::compile(pat);
                if (re && (*re).search(result.output)) {
                    res.skipped = true;
                    res.passed = true;
                    break;
                }
            }
        }

        if (!res.skipped) {
            if (skip_code.has_value() && result.exit_code == skip_code.value()) {
                res.skipped = true;
                res.passed = true;
            } else {
                res.passed = (result.exit_code == 0);
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        res.duration = std::chrono::duration<double>(end - start).count();
        return res;
    };

    // Schedule tests respecting DEPENDS ordering.
    // Tests with no deps (or only unknown deps) launch immediately in parallel.
    // Tests with deps wait until all their deps have completed.
    std::set<std::string> completed;
    std::vector<TestResult> results;
    results.reserve(selected_tests.size());

    // Track which tests are pending vs in-flight
    std::set<size_t> pending;
    for (size_t i = 0; i < selected_tests.size(); ++i) {
        pending.insert(i);
    }

    int passed_count = 0;
    int skipped_count = 0;
    int failed_count = 0;

    auto print_result = [&](const TestResult& res) {
        std::cout << "[" << results.size() << "/" << selected_tests.size() << "] "
                  << std::left << std::setw(40) << res.name << " ";

        if (res.skipped) {
            std::cout << kiln::c(std::cout, kiln::colors::BOLD_YELLOW) << "SKIPPED" << kiln::c(std::cout, kiln::colors::RESET);
            skipped_count++;
            passed_count++;
        } else if (res.timed_out) {
            std::cout << kiln::c(std::cout, kiln::colors::BOLD_MAGENTA) << "TIMEOUT" << kiln::c(std::cout, kiln::colors::RESET);
            failed_count++;
        } else if (res.passed) {
            std::cout << kiln::c(std::cout, kiln::colors::BOLD_GREEN) << "PASSED" << kiln::c(std::cout, kiln::colors::RESET);
            passed_count++;
        } else {
            std::cout << kiln::c(std::cout, kiln::colors::BOLD_RED) << "FAILED" << kiln::c(std::cout, kiln::colors::RESET);
            failed_count++;
        }
        std::cout << " (" << std::fixed << std::setprecision(2) << res.duration << "s)" << std::endl;

        if (!res.passed || res.timed_out) {
            std::cout << "--- Output ---" << std::endl;
            std::cout << res.output;
            std::cout << "--------------" << std::endl;
        }
    };

    // Track resource locks held by in-flight tests
    std::set<std::string> held_locks;

    auto get_locks = [&](size_t idx) -> const std::vector<std::string>& {
        static const std::vector<std::string> empty;
        auto it = resource_locks_map.find(selected_tests[idx]->name);
        return (it != resource_locks_map.end()) ? it->second : empty;
    };

    auto has_lock_conflict = [&](size_t idx) -> bool {
        for (const auto& lock : get_locks(idx)) {
            if (held_locks.count(lock)) return true;
        }
        return false;
    };

    while (!pending.empty()) {
        // Find tests whose deps are all completed and resource locks are available
        std::vector<size_t> ready;
        for (auto idx : pending) {
            // Check DEPENDS
            auto dit = deps_map.find(selected_tests[idx]->name);
            if (dit != deps_map.end()) {
                bool all_met = true;
                for (const auto& dep : dit->second) {
                    if (completed.find(dep) == completed.end()) {
                        all_met = false;
                        break;
                    }
                }
                if (!all_met) continue;
            }
            // Check RESOURCE_LOCK conflicts with in-flight tests
            if (has_lock_conflict(idx)) continue;
            ready.push_back(idx);
        }

        // If nothing is ready but tests are pending, we must have in-flight tests
        // holding locks. This shouldn't happen since we collect results below,
        // but guard against deadlock.
        if (ready.empty() && !pending.empty()) {
            // This can happen when all pending tests are blocked on resource locks
            // held by other pending (not yet launched) tests — shouldn't happen in
            // practice since we only block on in-flight locks. But if we somehow
            // get here with no in-flight tests, break to avoid infinite loop.
            break;
        }

        // Launch ready tests in parallel, acquiring locks as we go to prevent
        // two tests in the same wave from claiming the same lock
        std::vector<std::pair<size_t, std::future<TestResult>>> in_flight;
        for (auto idx : ready) {
            // Re-check lock conflict — a prior test in this wave may have claimed it
            if (has_lock_conflict(idx)) continue;
            pending.erase(idx);
            for (const auto& lock : get_locks(idx)) {
                held_locks.insert(lock);
            }
            in_flight.emplace_back(idx, std::async(std::launch::async, run_one_test, selected_tests[idx]));
        }

        // Collect results from this wave, releasing locks as tests complete
        for (auto& [idx, fut] : in_flight) {
            auto res = fut.get();
            // Release resource locks
            for (const auto& lock : get_locks(idx)) {
                held_locks.erase(lock);
            }
            completed.insert(res.name);
            results.push_back(res);
            print_result(res);
        }
    }

    auto end_all = std::chrono::high_resolution_clock::now();
    double total_duration = std::chrono::duration<double>(end_all - start_all).count();

    std::cout << "\nTest Summary: " << passed_count << "/" << selected_tests.size() << " passed";
    if (skipped_count > 0) {
        std::cout << " (" << skipped_count << " skipped)";
    }
    std::cout << " ("
              << std::fixed << std::setprecision(2) << total_duration << "s)" << std::endl;

    return (passed_count == static_cast<int>(selected_tests.size())) ? 0 : 1;
}

} // namespace

int main(int argc, char* argv[]) {
    CLI::App app{"kiln - A modern C++ build system with CMake compatibility.\n"
                  "  Use 'kiln <subcommand> --help' for subcommand-specific options."};
    app.set_version_flag("-v,--version", "0.1.0-alpha");
    GlobalOptions opt;
    std::string ignored_generator;

    // Helper to add global options to app or subcommand
    auto add_global_options = [&](CLI::App* target) {
        target->add_option("-C,--project", opt.project_dir_str, "Project directory (default: current directory)");
        target->add_option("-j,--parallel", opt.jobs, "Number of parallel jobs (0 = all cores)")
           ->default_val(0);
        target->add_option("-B", opt.build_dir_str, "Build directory (default: <project>/build/<config>)");
        target->add_option("-D", opt.definitions, "Define a CMake variable (-DVAR=VALUE or -DVAR)");
        target->add_option("-G", ignored_generator, "Generator (ignored, CMake compatibility)");
        target->add_flag("--profile", opt.profile, "Generate build profile (Chrome trace event format)");
        target->add_flag("--trace", opt.trace, "Print each command as it is executed (raw arguments)");
        target->add_flag("--trace-expand", opt.trace_expand, "Print each command with expanded arguments");
        target->add_flag("--debugger", opt.debugger, "Start interactive CMake debugger");
        target->add_option("--break-on-message", opt.break_on_message, "Break into debugger when message matches pattern");
        target->add_flag("--config-only", opt.config_only, "[debug] Interpret CMakeLists.txt and save cache, then exit without building");
        target->add_flag("--no-sys-init", opt.no_sys_init, "[benchmark] Skip compiler detection and system init");
        target->add_flag("--fast-setup", opt.fast_setup,
            "Skip loading CMake's Compiler/<id>-<lang>.cmake during enable_language; "
            "use kiln's built-in subset of compiler vars. Faster but covers fewer flags.");
        target->add_flag("--fresh", opt.fresh, "Skip loading persistent cache (fresh configure)");
        target->add_option("--log-level", opt.log_level, "Set message log level (ERROR, WARNING, NOTICE, STATUS, VERBOSE, DEBUG, TRACE)");
        target->add_option("-c,--config", opt.config, "Build configuration: debug, release, relwithdebinfo, minsizerel")
           ->default_val("debug")
           ->transform([](const std::string& value) -> std::string {
               auto copy = kiln::to_lower(value);
               if (copy == "debug" || copy == "release" || copy == "relwithdebinfo" || copy == "minsizerel") {
                   return copy;
               }
               throw CLI::ValidationError("Invalid configuration");
           }, "", "lowercase");
    };

    // Add global options to main app
    add_global_options(&app);

    // Special modes (only on main app)
    app.add_option("-P", opt.script_path, "Run a CMake script (script mode)");
    app.add_option("-S", opt.source_dir_str, "Source directory (CMake compat, requires -B)");

    app.footer(R"(
Examples:
  kiln                             # Build current directory (debug)
  kiln -j8 --config release        # Build with 8 jobs in release mode
  kiln -C ~/other/project          # Build a different project
  kiln run my_app -- --verbose     # Build and run a target with args
  kiln test "parser.*"             # Run tests matching a regex
  kiln -E copy file.txt dest/      # CMake-compatible tool command
  kiln -P script.cmake -DFOO=bar   # Run a CMake script
)");

    // Subcommands
    std::vector<std::string> build_targets;
    auto* build_cmd = app.add_subcommand("build", "Build specific targets (default if no subcommand given)");
    build_cmd->add_option("targets", build_targets, "Target names to build (default: all)");
    add_global_options(build_cmd);

    std::string test_pattern;
    auto* test_cmd = app.add_subcommand("test", "Build and run tests (sets BUILD_TESTING=ON)");
    test_cmd->add_option("pattern", test_pattern, "Regex filter for test names");
    add_global_options(test_cmd);

    std::string run_target;
    std::vector<std::string> run_args;
    auto* run_cmd = app.add_subcommand("run", "Build and run an executable target");
    run_cmd->add_option("target", run_target, "Executable target to run")->required();
    run_cmd->add_option("args", run_args, "Arguments passed to the target (use -- to separate)");
    add_global_options(run_cmd);

    auto* clean_cmd = app.add_subcommand("clean", "Remove build artifacts for the current config");
    add_global_options(clean_cmd);

    auto* install_cmd = app.add_subcommand("install", "Build and install project files");
    std::string install_prefix;
    std::string install_component;
    install_cmd->add_option("--prefix", install_prefix, "Installation prefix (overrides CMAKE_INSTALL_PREFIX)");
    install_cmd->add_option("--component", install_component, "Install only the specified component");
    add_global_options(install_cmd);

    auto* e_cmd = app.add_subcommand("tool", "CMake-compatible tool commands (echo, touch, copy, ...)");
    e_cmd->alias("-E");
    e_cmd->prefix_command();

    // Default targets when no subcommand (positionals go to build_targets)
    std::vector<std::string> positionals;
    app.add_option("targets", positionals, "Target names to build (default: all)");

    app.require_subcommand(0, 1);

    CLI11_PARSE(app, argc, argv);

    if (opt.profile) {
        kiln::Profiler::instance().enable();
    }

    // Set up debug/trace controller
    kiln::DebugOptions debug_opts{opt.trace, opt.trace_expand, opt.debugger, opt.break_on_message};
    kiln::DebugController debug_controller(debug_opts);

    // Install signal handlers for graceful interruption (cache saving on Ctrl-C, etc.)
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGHUP, signal_handler);

    if (debug_opts.any_enabled()) {
        // Set up linenoise-based input for interactive debugger
        static const char* dbg_commands[] = {
            "break", "break-on-message", "continue", "step", "next", "print",
            "backtrace", "list", "frame", "up", "down", "info variables",
            "info breakpoints", "watch", "delete", "quit", "help", nullptr
        };
        linenoiseSetCompletionCallback([](const char* buf, linenoiseCompletions* lc) {
            std::string_view prefix(buf);
            for (const char** cmd = dbg_commands; *cmd; ++cmd) {
                if (std::string_view(*cmd).starts_with(prefix)) {
                    linenoiseAddCompletion(lc, *cmd);
                }
            }
        });
        linenoiseHistorySetMaxLen(100);

        debug_controller.set_input_function([](const char* prompt, int& key_type) -> std::optional<std::string> {
            key_type = 0;
            char* raw = linenoise(prompt);
            if (!raw) {
                key_type = linenoiseKeyType();  // 1=Ctrl+C, 2=Ctrl+D
                return std::nullopt;
            }
            std::string line(raw);
            free(raw);
            linenoiseHistoryAdd(line.c_str());
            return line;
        });
    }

    if (e_cmd->parsed()) {
        auto e_args = e_cmd->remaining();
        return kiln::run_tool_mode(e_args);
    }

    if (!opt.script_path.empty()) {
        try {
            std::string content;
            std::string script_name;
            std::string working_dir;

            if (opt.script_path == "-") {
                // Read script from stdin
                content.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
                script_name = "<stdin>";
                working_dir = std::filesystem::current_path().string();
            } else {
                std::filesystem::path script_abs = std::filesystem::absolute(opt.script_path);
                if (!std::filesystem::exists(script_abs)) {
                    std::cerr << "Error: Script not found: " << opt.script_path << std::endl;
                    return 1;
                }
                std::ifstream file(script_abs);
                content.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
                script_name = script_abs.string();
                // CMake script mode: CMAKE_CURRENT_SOURCE_DIR = CWD (not script dir)
                working_dir = std::filesystem::current_path().string();
            }

            kiln::Parser parser(content, script_name);
            auto ast_or_error = parser.parse();
            if (!ast_or_error) {
                print_error_context(script_name, ast_or_error.error().row, ast_or_error.error().col, ast_or_error.error().offset, ast_or_error.error().length, ast_or_error.error().reason);
                return 1;
            }

            kiln::Interpreter interpreter(working_dir, &std::cout, &std::cerr, std::nullopt, opt.no_sys_init, /*skip_cache_load=*/true);
            interpreter.set_current_file(script_name);
            interpreter.set_variable("CMAKE_SCRIPT_MODE_FILE", script_name);
            // In script mode, CMAKE_CURRENT_LIST_DIR/FILE point to the script location
            std::string script_dir = std::filesystem::path(script_name).parent_path().string();
            interpreter.set_variable("CMAKE_CURRENT_LIST_DIR", script_dir);
            interpreter.set_variable("CMAKE_CURRENT_LIST_FILE", script_name);
            debug_controller.attach(interpreter);
            apply_definitions(interpreter, opt.definitions);
            if (!opt.log_level.empty()) {
                interpreter.set_variable("CMAKE_MESSAGE_LOG_LEVEL", opt.log_level);
            }

            auto result = interpreter.interpret(ast_or_error.value());
            if (!result) {
                print_error_context(result.error());
                return 1;
            }
            interpreter.execute_deferred_calls();
            if (auto err = interpreter.get_fatal_error()) {
                print_error_context(*err);
                return 1;
            }
            return 0;
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return 1;
        }
    }

    if (clean_cmd->parsed()) {
        std::filesystem::path project_path = std::filesystem::absolute(opt.project_dir_str);
        std::filesystem::path build_root = opt.build_dir_str.empty() ? (project_path / "build") : std::filesystem::absolute(opt.build_dir_str).lexically_normal();
        std::filesystem::path build_path = build_root / opt.config;
        if (std::filesystem::exists(build_path)) {
            std::uintmax_t total_bytes = 0;
            for (auto const& entry : std::filesystem::recursive_directory_iterator(build_path, std::filesystem::directory_options::skip_permission_denied)) {
                if (entry.is_regular_file()) {
                    std::error_code ec;
                    auto sz = entry.file_size(ec);
                    if (!ec) total_bytes += sz;
                }
            }
            std::cout << "Cleaning " << build_path << "..." << std::endl;
            std::filesystem::remove_all(build_path);
            // Format freed size
            auto format_bytes = [](std::uintmax_t bytes) -> std::string {
                constexpr double KB = 1024, MB = 1024*1024, GB = 1024*1024*1024;
                char buf[32];
                if (bytes >= GB)      std::snprintf(buf, sizeof(buf), "%.2f GB", bytes / GB);
                else if (bytes >= MB) std::snprintf(buf, sizeof(buf), "%.2f MB", bytes / MB);
                else if (bytes >= KB) std::snprintf(buf, sizeof(buf), "%.2f KB", bytes / KB);
                else                  std::snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)bytes);
                return buf;
            };
            std::cout << "Freed " << format_bytes(total_bytes) << std::endl;
        }
        return 0;
    }

    if (install_cmd->parsed()) {
        // Build project first
        auto build_res = run_build_action(opt, debug_controller, opt.project_dir_str, {});
        if (!build_res) {
            std::cerr << "Build failed: " << build_res.error() << std::endl;
            return 1;
        }

        auto& interpreter = build_res.value();

        // Determine install prefix
        std::string prefix = install_prefix.empty()
            ? interpreter->get_variable("CMAKE_INSTALL_PREFIX")
            : install_prefix;

        if (prefix.empty()) {
            prefix = "/usr/local";  // CMake default
        }

        std::cout << kiln::c(std::cout, kiln::colors::BOLD_BLUE) << "Installing to:" << kiln::c(std::cout, kiln::colors::RESET) << " " << prefix << std::endl;

        // Execute install
        auto result = kiln::execute_install_rules(
            interpreter.get(),
            interpreter->get_install_rules(),
            prefix,
            to_cmake_case(opt.config),
            install_component
        );

        if (!result) {
            std::cerr << kiln::c(std::cerr, kiln::colors::BOLD_RED) << "error:" << kiln::c(std::cerr, kiln::colors::RESET) << " " << result.error() << std::endl;
            return 1;
        }

        std::cout << kiln::c(std::cout, kiln::colors::BOLD_GREEN) << "Installation complete." << kiln::c(std::cout, kiln::colors::RESET) << std::endl;
        return 0;
    }

    if (run_cmd->parsed()) {
        auto build_res = run_build_action(opt, debug_controller, opt.project_dir_str, {run_target});
        if (!build_res) {
            std::cerr << "Error: " << build_res.error() << std::endl;
            return 1;
        }

        auto& interpreter = build_res.value();
        auto* target = interpreter->find_target(run_target);
        if (!target) {
            std::cerr << "Error: Target '" << run_target << "' not found after build." << std::endl;
            return 1;
        }

        if (target->get_type() != kiln::TargetType::EXECUTABLE) {
            std::cerr << "Error: Target '" << run_target << "' is not an executable." << std::endl;
            return 1;
        }

        std::string exec_path = target->get_output_path();
        if (!std::filesystem::exists(exec_path)) {
            std::cerr << "Error: Executable not found at " << exec_path << std::endl;
            return 1;
        }

        std::vector<char*> argv_exec;
        argv_exec.push_back(const_cast<char*>(exec_path.c_str()));
        for (const auto& arg : run_args) {
            argv_exec.push_back(const_cast<char*>(arg.c_str()));
        }
        argv_exec.push_back(nullptr);

        std::cout << kiln::c(std::cout, kiln::colors::BOLD_GREEN) << "Running" << kiln::c(std::cout, kiln::colors::RESET) << " " << exec_path << "..." << std::endl;
        execvp(argv_exec[0], argv_exec.data());

        std::cerr << "Error: Failed to execute " << exec_path << ": " << strerror(errno) << std::endl;
        return 1;
    }

    if (test_cmd->parsed()) {
        auto build_res = run_build_action(opt, debug_controller, opt.project_dir_str, {}, true);
        if (!build_res) {
            std::cerr << "Error: " << build_res.error() << std::endl;
            return 1;
        }

        return run_test_action(opt, build_res.value().get(), test_pattern);
    }

    // Handle -S flag: CMake-style build (ExternalProject compatibility)
    if (!opt.source_dir_str.empty()) {
        auto build_res = run_build_action(opt, debug_controller, opt.source_dir_str, {});
        if (!build_res) {
            std::cerr << "Error: " << build_res.error() << std::endl;
            return 1;
        }
        return 0;
    }

    // Determine targets: use build_targets if build subcommand, else positionals
    std::vector<std::string> targets = build_cmd->parsed() ? build_targets : positionals;

    // Migration hint: if a target looks like a project directory, suggest -C
    for (const auto& target : targets) {
        std::filesystem::path target_path(target);
        if (std::filesystem::exists(target_path / "CMakeLists.txt")) {
            std::cerr << kiln::c(std::cerr, kiln::colors::BOLD_YELLOW) << "hint:"
                      << kiln::c(std::cerr, kiln::colors::RESET)
                      << " '" << target << "' looks like a project directory. "
                      << "Use 'kiln -C " << target << "' to build it." << std::endl;
        }
    }

    auto build_res = run_build_action(opt, debug_controller, opt.project_dir_str, targets);
    if (!build_res) {
        std::cerr << "Error: " << build_res.error() << std::endl;
        return 1;
    }
    return 0;
}
