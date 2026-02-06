#include "dmake/cmake-language.hpp"
#include "dmake/interperter.hpp"
#include "dmake/printing.hpp"
#include "dmake/utils.hpp"
#include "dmake/install_executor.hpp"
#include "dmake/profiler.hpp"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <CLI/CLI.hpp>
#include <unistd.h>
#include <future>
#include "dmake/regex.hpp"
#include <chrono>
#include <iomanip>

namespace {

struct GlobalOptions {
    int jobs = 0;
    std::string build_dir_str;
    std::vector<std::string> definitions;
    std::string config = "debug";
    std::string script_path;
    bool profile = false;
};

void print_error_context(const std::string& file_path, size_t row, size_t col, size_t offset, size_t length, const std::string& message, const std::vector<dmake::CallLocation>& backtrace = {}, const std::optional<std::string>& source_content = std::nullopt) {
    dmake::print_diagnostic(std::cerr, dmake::DiagnosticSeverity::Error, message, file_path, row, col, offset, length, backtrace, source_content);
}

void print_error_context(const dmake::InterpreterError& error) {
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

void apply_definitions(dmake::Interpreter& interpreter, const std::vector<std::string>& definitions) {
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

            interpreter.set_variable(var_name, value);
        } else {
            // Handle -DVAR or -DVAR:BOOL (no value)
            std::string var_name = def;
            size_t colon = var_name.find(':');
            if (colon != std::string::npos) {
                var_name = var_name.substr(0, colon);
            }
            interpreter.set_variable(var_name, "ON");
        }
    }
}

void set_default_flags(dmake::Interpreter& interpreter, const std::vector<std::string>& definitions, const std::string& var, const std::string& flags) {
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

std::expected<std::unique_ptr<dmake::Interpreter>, std::string> run_build_action(const GlobalOptions& opt, const std::string& project_dir, const std::vector<std::string>& targets, bool is_test_mode = false) {
    try {
        std::filesystem::path project_path = std::filesystem::canonical(project_dir);
        std::filesystem::path cmake_lists = project_path / "CMakeLists.txt";

        if (!std::filesystem::exists(cmake_lists)) {
            return std::unexpected("CMakeLists.txt not found in " + project_path.string());
        }

        std::filesystem::path build_root = opt.build_dir_str.empty() ? (project_path / "build") : std::filesystem::absolute(opt.build_dir_str).lexically_normal();
        std::filesystem::path build_path = build_root / opt.config;

        if (build_path == project_path) {
            return std::unexpected("Build directory cannot be source directory");
        }

        std::ifstream file(cmake_lists);
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        dmake::ProfileScope parse_profile("parse " + cmake_lists.filename().string(), "parse");
        dmake::Parser parser(content, cmake_lists.string());
        auto ast_or_error = parser.parse();
        parse_profile.stop();

        if (!ast_or_error) {
            const auto& error = ast_or_error.error();
            print_error_context(cmake_lists.string(), error.row, error.col, error.offset, error.length, error.reason);
            return std::unexpected("Parse error");
        }

        dmake::ProfileScope configure_profile("configure", "configure");
        auto interpreter = std::make_unique<dmake::Interpreter>(project_path.string(), &std::cout, &std::cerr, build_path.string());
        interpreter->set_current_file(cmake_lists.string());

        // Set BUILD_TESTING before user definitions (user can override with -D)
        interpreter->set_variable("BUILD_TESTING", is_test_mode ? "ON" : "OFF");
        apply_definitions(*interpreter, opt.definitions);

        if (interpreter->get_variable("CMAKE_BUILD_TYPE").empty()) {
            interpreter->set_variable("CMAKE_BUILD_TYPE", to_cmake_case(opt.config));
        }

        set_default_flags(*interpreter, opt.definitions, "CMAKE_CXX_FLAGS_DEBUG", "-g -O0");
        set_default_flags(*interpreter, opt.definitions, "CMAKE_CXX_FLAGS_RELEASE", "-O3 -DNDEBUG");
        set_default_flags(*interpreter, opt.definitions, "CMAKE_CXX_FLAGS_RELWITHDEBINFO", "-g -O2 -DNDEBUG");
        set_default_flags(*interpreter, opt.definitions, "CMAKE_CXX_FLAGS_MINSIZEREL", "-Os -DNDEBUG");
        configure_profile.stop();

        {
            dmake::ProfileScope scope("interpret " + cmake_lists.filename().string(), "interpret");
            auto interpret_result = interpreter->interpret(ast_or_error.value());
            if (!interpret_result) {
                print_error_context(interpret_result.error());
                return std::unexpected("Interpretation error");
            }

            // Apply retroactive directory properties to root-level targets
            interpreter->finalize_directory_targets();
        }

        auto build_result = interpreter->run_build(opt.jobs, targets);
        if (!build_result) {
            std::cerr << dmake::c(std::cerr, dmake::colors::BOLD_RED) << "error:" << dmake::c(std::cerr, dmake::colors::RESET) << " " << build_result.error().message << std::endl;
            return std::unexpected("Build failed");
        }

        // Save cache after successful build
        auto cache_save_result = interpreter->get_cache_store().save();
        if (!cache_save_result) {
            std::cerr << dmake::c(std::cerr, dmake::colors::BOLD_YELLOW) << "warning:" << dmake::c(std::cerr, dmake::colors::RESET) << " Failed to save cache: " << cache_save_result.error() << std::endl;
        }

        // Write profile if enabled
        if (dmake::g_profiling_enabled.load(std::memory_order_relaxed)) {
            auto profile_path = (build_path / "profile.json").string();
            dmake::Profiler::instance().write(profile_path);
            std::cerr << dmake::c(std::cerr, dmake::colors::BOLD_CYAN) << "Profile" << dmake::c(std::cerr, dmake::colors::RESET) << " written to " << profile_path << std::endl;
        }

        return interpreter;
    } catch (const std::exception& e) {
        return std::unexpected(std::string("Exception: ") + e.what());
    }
}

// Must insure build is up to date before running tests
int run_test_action(const GlobalOptions& opt, dmake::Interpreter* interpreter, const std::string& pattern) {
    auto& tests = interpreter->get_tests();
    auto& targets_map = interpreter->get_targets();

    std::vector<dmake::TestDefinition*> selected_tests;
    std::set<std::string> targets_to_build;

    std::optional<dmake::Regex> filter;
    bool has_filter = !pattern.empty();
    if (has_filter) {
        auto re = dmake::Regex::compile(pattern);
        if (!re) {
            std::cerr << "Error: Invalid test pattern regex: " << re.error() << std::endl;
            return 1;
        }
        filter = std::move(*re);
    }

    for (auto& test : tests) {
        if (has_filter && !filter->search(test.name)) {
            continue;
        }
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

    std::cout << dmake::c(std::cout, dmake::colors::BOLD_BLUE) << "Running " << selected_tests.size() << " tests..." << dmake::c(std::cout, dmake::colors::RESET) << std::endl;

    struct TestResult {
        std::string name;
        bool passed;
        bool skipped;
        bool timed_out;
        double duration;
        std::string output;
    };

    std::vector<std::future<TestResult>> futures;
    auto start_all = std::chrono::high_resolution_clock::now();

    for (auto* test : selected_tests) {
        futures.push_back(std::async(std::launch::async, [test, &targets_map]() -> TestResult {
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
                try {
                    timeout = std::stod(timeout_it->second);
                } catch (...) {
                    res.output = "Error: Invalid TIMEOUT value '" + timeout_it->second + "'\n";
                    res.duration = 0.0;
                    return res;
                }
            }

            // Check for SKIP_RETURN_CODE property
            std::optional<int> skip_code;
            auto skip_it = test->properties.find("SKIP_RETURN_CODE");
            if (skip_it != test->properties.end()) {
                try {
                    skip_code = std::stoi(skip_it->second);
                } catch (...) {
                    res.output = "Error: Invalid SKIP_RETURN_CODE value '" + skip_it->second + "'\n";
                    res.duration = 0.0;
                    return res;
                }
            }

            // Execute the command with timeout handling
            std::future<dmake::CommandResult> cmd_future = std::async(std::launch::async, [&]() {
                return dmake::run_command(command_vec, test->working_dir);
            });

            dmake::CommandResult result;
            if (timeout > 0.0) {
                auto timeout_duration = std::chrono::duration<double>(timeout);
                if (cmd_future.wait_for(timeout_duration) == std::future_status::timeout) {
                    res.timed_out = true;
                    res.passed = false;
                    res.output = "Test timed out after " + std::to_string(timeout) + " seconds\n";
                    auto end = std::chrono::high_resolution_clock::now();
                    res.duration = std::chrono::duration<double>(end - start).count();
                    // Note: The actual test process may still be running, but we don't wait for it
                    return res;
                }
                result = cmd_future.get();
            } else {
                result = cmd_future.get();
            }

            res.output = result.output;

            // Check if test was skipped
            if (skip_code.has_value() && result.exit_code == skip_code.value()) {
                res.skipped = true;
                res.passed = true;  // Skipped tests count as passed
            } else {
                res.passed = (result.exit_code == 0);
            }

            auto end = std::chrono::high_resolution_clock::now();
            res.duration = std::chrono::duration<double>(end - start).count();
            return res;
        }));
    }

    int passed_count = 0;
    int skipped_count = 0;
    int failed_count = 0;
    for (size_t i = 0; i < futures.size(); ++i) {
        auto res = futures[i].get();
        std::cout << "[" << (i + 1) << "/" << selected_tests.size() << "] "
                  << std::left << std::setw(40) << res.name << " ";

        if (res.skipped) {
            std::cout << dmake::c(std::cout, dmake::colors::BOLD_YELLOW) << "SKIPPED" << dmake::c(std::cout, dmake::colors::RESET);
            skipped_count++;
            passed_count++;  // Skipped tests count as passed
        } else if (res.timed_out) {
            std::cout << dmake::c(std::cout, dmake::colors::BOLD_MAGENTA) << "TIMEOUT" << dmake::c(std::cout, dmake::colors::RESET);
            failed_count++;
        } else if (res.passed) {
            std::cout << dmake::c(std::cout, dmake::colors::BOLD_GREEN) << "PASSED" << dmake::c(std::cout, dmake::colors::RESET);
            passed_count++;
        } else {
            std::cout << dmake::c(std::cout, dmake::colors::BOLD_RED) << "FAILED" << dmake::c(std::cout, dmake::colors::RESET);
            failed_count++;
        }
        std::cout << " (" << std::fixed << std::setprecision(2) << res.duration << "s)" << std::endl;

        if (!res.passed || res.timed_out) {
            std::cout << "--- Output ---" << std::endl;
            std::cout << res.output;
            std::cout << "--------------" << std::endl;
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

    return (passed_count == selected_tests.size()) ? 0 : 1;
}

} // namespace

int main(int argc, char* argv[]) {
    CLI::App app{"dmake - A modern C++ build system with CMake compatibility."};

    GlobalOptions opt;
    app.add_option("-P", opt.script_path, "Run dmake in script mode");
    app.add_option("-j,--parallel", opt.jobs, "Parallel jobs (default: 0 for all cores)");
    app.add_option("-B", opt.build_dir_str, "Build directory (default: <project>/build)");
    app.add_option("-D", opt.definitions, "Define variables (VAR=VALUE)");
    app.add_flag("--profile", opt.profile, "Generate build profile (Chrome trace event format)");
    app.add_option("-c,--config", opt.config, "Build configuration (debug, release, relwithdebinfo, minsizerel)")
       ->transform([](const std::string& value) -> std::string {
           auto copy = value;
           std::transform(copy.begin(), copy.end(), copy.begin(), ::tolower);
           if (copy == "debug" || copy == "release" || copy == "relwithdebinfo" || copy == "minsizerel") {
               return copy;
           }
           throw CLI::ValidationError("Invalid configuration");
       }, "convert to lower case", "lowercase");

    std::string build_project_dir = ".";
    std::vector<std::string> build_targets;
    auto* build_cmd = app.add_subcommand("build", "Build targets (default)");
    build_cmd->add_option("project", build_project_dir, "Project directory or first target");
    build_cmd->add_option("targets", build_targets, "Targets to build");

    std::string test_project_dir = ".";
    std::string test_pattern;
    auto* test_cmd = app.add_subcommand("test", "Run tests");
    test_cmd->add_option("project", test_project_dir, "Project directory");
    test_cmd->add_option("pattern", test_pattern, "Test pattern (regex)");

    std::string run_project_dir = ".";
    std::string run_target;
    std::vector<std::string> run_args;
    auto* run_cmd = app.add_subcommand("run", "Run a target");
    run_cmd->add_option("project", run_project_dir, "Project directory");
    run_cmd->add_option("target", run_target, "Target to run")->required();
    run_cmd->add_option("args", run_args, "Arguments for the target");

    auto* clean_cmd = app.add_subcommand("clean", "Clean build directory");
    std::string clean_project_dir = ".";
    clean_cmd->add_option("project", clean_project_dir, "Project directory");

    auto* install_cmd = app.add_subcommand("install", "Install project files");
    std::string install_project_dir = ".";
    std::string install_prefix;
    std::string install_component;
    install_cmd->add_option("project", install_project_dir, "Project directory");
    install_cmd->add_option("--prefix", install_prefix, "Installation prefix (overrides CMAKE_INSTALL_PREFIX)");
    install_cmd->add_option("--component", install_component, "Install specific component only");

    auto* e_cmd = app.add_subcommand("mode-E", "CMake-like command-line tool mode");
    e_cmd->alias("-E");
    e_cmd->prefix_command();

    app.require_subcommand(0, 1);
    app.allow_extras();

    CLI11_PARSE(app, argc, argv);

    if (opt.profile) {
        dmake::Profiler::instance().enable();
    }

    if (e_cmd->parsed()) {
        auto e_args = e_cmd->remaining();
        if (e_args.empty()) {
            std::cerr << "Error: -E requires a command" << std::endl;
            return 1;
        }

        std::string cmd = e_args[0];
        if (cmd == "echo") {
            for (size_t i = 1; i < e_args.size(); ++i) {
                std::cout << e_args[i] << (i == e_args.size() - 1 ? "" : " ");
            }
            std::cout << std::endl;
            return 0;
        } else if (cmd == "touch") {
            for (size_t i = 1; i < e_args.size(); ++i) {
                std::ofstream f(e_args[i], std::ios::app);
            }
            return 0;
        } else if (cmd == "remove") {
            for (size_t i = 1; i < e_args.size(); ++i) {
                std::filesystem::remove_all(e_args[i]);
            }
            return 0;
        } else if (cmd == "make_directory") {
            for (size_t i = 1; i < e_args.size(); ++i) {
                std::filesystem::create_directories(e_args[i]);
            }
            return 0;
        } else if (cmd == "create_symlink") {
            if(e_args.size() != 3) {
                std::cerr << "Error: create_symlink requires exactly two arguments" << std::endl;
                return 1;
            }

            auto src = e_args[1];
            auto dst = e_args[2];
            bool exists = std::filesystem::exists(dst);
            bool is_symlink = std::filesystem::is_symlink(dst);
            if(exists && !is_symlink) {
                std::cerr << "Error: Link destination " << dst << " already exists and is not a symlink" << std::endl;
                return 1;
            }
            else if(exists && is_symlink) {
                return 0;
            }
            std::filesystem::create_symlink(src, dst);
            return 0;
        } else {
            std::cerr << "Error: Unknown -E command: " << cmd << std::endl;
            return 1;
        }
    }

    if (!opt.script_path.empty()) {
        try {
            std::filesystem::path script_abs = std::filesystem::absolute(opt.script_path);
            if (!std::filesystem::exists(script_abs)) {
                std::cerr << "Error: Script not found: " << opt.script_path << std::endl;
                return 1;
            }

            std::ifstream file(script_abs);
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

            dmake::Parser parser(content, script_abs.string());
            auto ast_or_error = parser.parse();
            if (!ast_or_error) {
                print_error_context(script_abs.string(), ast_or_error.error().row, ast_or_error.error().col, ast_or_error.error().offset, ast_or_error.error().length, ast_or_error.error().reason);
                return 1;
            }

            dmake::Interpreter interpreter(script_abs.parent_path().string(), &std::cout, &std::cerr);
            interpreter.set_current_file(script_abs.string());
            apply_definitions(interpreter, opt.definitions);

            auto result = interpreter.interpret(ast_or_error.value());
            if (!result) {
                print_error_context(result.error());
                return 1;
            }
            return 0;
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return 1;
        }
    }

    if (clean_cmd->parsed()) {
        std::filesystem::path project_path = std::filesystem::absolute(clean_project_dir);
        std::filesystem::path build_root = opt.build_dir_str.empty() ? (project_path / "build") : std::filesystem::absolute(opt.build_dir_str).lexically_normal();
        std::filesystem::path build_path = build_root / opt.config;
        if (std::filesystem::exists(build_path)) {
            std::cout << "Cleaning " << build_path << "..." << std::endl;
            std::filesystem::remove_all(build_path);
        }
        return 0;
    }

    if (install_cmd->parsed()) {
        // Build project first
        auto build_res = run_build_action(opt, install_project_dir, {});
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

        std::cout << dmake::c(std::cout, dmake::colors::BOLD_BLUE) << "Installing to:" << dmake::c(std::cout, dmake::colors::RESET) << " " << prefix << std::endl;

        // Execute install
        auto result = dmake::execute_install_rules(
            interpreter.get(),
            interpreter->get_install_rules(),
            prefix,
            to_cmake_case(opt.config),
            install_component
        );

        if (!result) {
            std::cerr << dmake::c(std::cerr, dmake::colors::BOLD_RED) << "error:" << dmake::c(std::cerr, dmake::colors::RESET) << " " << result.error() << std::endl;
            return 1;
        }

        std::cout << dmake::c(std::cout, dmake::colors::BOLD_GREEN) << "Installation complete." << dmake::c(std::cout, dmake::colors::RESET) << std::endl;
        return 0;
    }

    if (run_cmd->parsed()) {
        if (!std::filesystem::exists(std::filesystem::path(run_project_dir) / "CMakeLists.txt")) {
            if (std::filesystem::exists("CMakeLists.txt")) {
                run_args.insert(run_args.begin(), run_target);
                run_target = run_project_dir;
                run_project_dir = ".";
            }
        }
        auto build_res = run_build_action(opt, run_project_dir, {run_target});
        if (!build_res) {
            std::cerr << "Error: " << build_res.error() << std::endl;
            return 1;
        }

        auto& interpreter = build_res.value();
        auto& targets_map = interpreter->get_targets();
        if (!targets_map.count(run_target)) {
            std::cerr << "Error: Target '" << run_target << "' not found after build." << std::endl;
            return 1;
        }

        auto target = targets_map[run_target];
        if (target->get_type() != dmake::TargetType::EXECUTABLE) {
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

        std::cout << dmake::c(std::cout, dmake::colors::BOLD_GREEN) << "Running" << dmake::c(std::cout, dmake::colors::RESET) << " " << exec_path << "..." << std::endl;
        execvp(argv_exec[0], argv_exec.data());

        std::cerr << "Error: Failed to execute " << exec_path << ": " << strerror(errno) << std::endl;
        return 1;
    }

    if (test_cmd->parsed()) {
        if (!std::filesystem::exists(std::filesystem::path(test_project_dir) / "CMakeLists.txt")) {
            if (std::filesystem::exists("CMakeLists.txt")) {
                test_pattern = test_project_dir;
                test_project_dir = ".";
            }
        }

        auto build_res = run_build_action(opt, test_project_dir, {}, true);
        if (!build_res) {
            std::cerr << "Error: " << build_res.error() << std::endl;
            return 1;
        }

        return run_test_action(opt, build_res.value().get(), test_pattern);
    }

    std::string project_dir = build_project_dir;
    std::vector<std::string> targets = build_targets;

    if (!build_cmd->parsed() && !test_cmd->parsed() && !run_cmd->parsed() && !clean_cmd->parsed() && !install_cmd->parsed()) {
        auto remaining = app.remaining();
        if (!remaining.empty()) {
            project_dir = remaining[0];
            for (size_t i = 1; i < remaining.size(); ++i) {
                targets.push_back(remaining[i]);
            }
        }
    }

    if (!std::filesystem::exists(std::filesystem::path(project_dir) / "CMakeLists.txt")) {
        if (std::filesystem::exists("CMakeLists.txt")) {
            targets.insert(targets.begin(), project_dir);
            project_dir = ".";
        }
    }

    auto build_res = run_build_action(opt, project_dir, targets);
    if (!build_res) {
        std::cerr << "Error: " << build_res.error() << std::endl;
        return 1;
    }
    return 0;
}
