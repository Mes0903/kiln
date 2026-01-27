#include "dmake/cmake-language.hpp"
#include "dmake/interperter.hpp"
#include "dmake/utils.hpp"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <CLI/CLI.hpp>
#include <unistd.h>
#include <future>
#include <regex>
#include <chrono>
#include <iomanip>

namespace {

struct GlobalOptions {
    int jobs = 0;
    std::string build_dir_str;
    std::vector<std::string> definitions;
    std::string config = "debug";
    std::string script_path;
};

void print_error_context(const std::string& file_path, size_t row, size_t col, size_t offset, size_t length, const std::string& message, const std::vector<dmake::CallLocation>& backtrace = {}, const std::optional<std::string>& source_content = std::nullopt) {
    std::cerr << "\033[1;31merror:\033[0m " << message << std::endl;
    std::cerr << "  \033[1;34m-->\033[0m " << file_path << ":" << row << ":" << col << std::endl;

    std::string file_content;
    bool has_content = false;

    if (source_content) {
        file_content = *source_content;
        has_content = true;
    } else {
        std::ifstream error_file(file_path);
        if (error_file) {
            file_content.assign((std::istreambuf_iterator<char>(error_file)), std::istreambuf_iterator<char>());
            has_content = true;
        }
    }
    
    if (has_content) {
        size_t line_start = 0;
        size_t current_line = 1;
        for (size_t i = 0; i < file_content.size() && current_line < row; ++i) {
            if (file_content[i] == '\n') {
                ++current_line;
                line_start = i + 1;
            }
        }

        size_t line_end = file_content.find('\n', line_start);
        if (line_end == std::string::npos) line_end = file_content.size();

        std::string line = file_content.substr(line_start, line_end - line_start);
        std::string padding(std::to_string(row).length(), ' ');

        std::cerr << "   " << padding << " \033[1;34m|\033[0m" << std::endl;
        std::cerr << "   \033[1;34m" << row << " |\033[0m " << line << std::endl;
        std::cerr << "   " << padding << " \033[1;34m|\033[0m " << std::string(col > 0 ? col - 1 : 0, ' ');

        size_t caret_len = (length > 0) ? length : 1;
        if (col > line.length()) {
            caret_len = 0;
        } else if (col + caret_len - 1 > line.length()) {
            caret_len = line.length() - col + 1;
        }
        
        if (caret_len > 0) {
            std::cerr << "\033[1;31m" << std::string(caret_len, '^') << "\033[0m" << std::endl;
        }
    }

    if (!backtrace.empty()) {
        std::cerr << "Call Stack (most recent call first):" << std::endl;
        for (auto it = backtrace.rbegin(); it != backtrace.rend(); ++it) {
            std::cerr << "  " << it->file << ":" << it->row << " (" << it->command << ")" << std::endl;
        }
    }
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
            interpreter.set_variable(def.substr(0, eq), def.substr(eq + 1));
        } else {
            interpreter.set_variable(def, "ON");
        }
    }
}

void set_default_flags(dmake::Interpreter& interpreter, const std::vector<std::string>& definitions, const std::string& var, const std::string& flags) {
    bool already_set = false;
    for (const auto& def : definitions) {
        if (def.starts_with(var + "=") || def == var) {
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

        dmake::Parser parser(content);
        auto ast_or_error = parser.parse();
        if (!ast_or_error) {
            const auto& error = ast_or_error.error();
            print_error_context(cmake_lists.string(), error.row, error.col, error.offset, error.length, error.reason);
            return std::unexpected("Parse error");
        }

        auto interpreter = std::make_unique<dmake::Interpreter>(project_path.string(), &std::cout, &std::cerr, nullptr, build_path.string());
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

        auto interpret_result = interpreter->interpret(ast_or_error.value());
        if (!interpret_result) {
            print_error_context(interpret_result.error());
            return std::unexpected("Interpretation error");
        }

        auto build_result = interpreter->run_build(opt.jobs, targets);
        if (!build_result) {
            std::cerr << "\033[1;31merror:\033[0m " << build_result.error().message << std::endl;
            return std::unexpected("Build failed");
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

    std::regex filter;
    bool has_filter = !pattern.empty();
    if (has_filter) {
        try {
            filter = std::regex(pattern);
        } catch (const std::regex_error& e) {
            std::cerr << "Error: Invalid test pattern regex: " << e.what() << std::endl;
            return 1;
        }
    }

    for (auto& test : tests) {
        if (has_filter && !std::regex_search(test.name, filter)) {
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

    std::cout << "\033[1;34mRunning " << selected_tests.size() << " tests...\033[0m" << std::endl;

    struct TestResult {
        std::string name;
        bool passed;
        double duration;
        std::string output;
    };

    std::vector<std::future<TestResult>> futures;
    auto start_all = std::chrono::high_resolution_clock::now();

    for (auto* test : selected_tests) {
        TestResult res;
        res.name = test->name;

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

        auto result = dmake::run_command(command_vec, test->working_dir);
        res.passed = (result.exit_code == 0);
        res.output = result.output;

        auto end = std::chrono::high_resolution_clock::now();
        res.duration = std::chrono::duration<double>(end - start).count();
        futures.push_back(std::async(std::launch::deferred, [res]() { return res; }));
    }

    int passed_count = 0;
    for (size_t i = 0; i < futures.size(); ++i) {
        auto res = futures[i].get();
        std::cout << "[" << (i + 1) << "/" << selected_tests.size() << "] "
                  << std::left << std::setw(40) << res.name << " ";
        if (res.passed) {
            std::cout << "\033[1;32mPASSED\033[0m";
            passed_count++;
        } else {
            std::cout << "\033[1;31mFAILED\033[0m";
        }
        std::cout << " (" << std::fixed << std::setprecision(2) << res.duration << "s)" << std::endl;

        if (!res.passed) {
            std::cout << "--- Output ---" << std::endl;
            std::cout << res.output;
            std::cout << "--------------" << std::endl;
        }
    }

    auto end_all = std::chrono::high_resolution_clock::now();
    double total_duration = std::chrono::duration<double>(end_all - start_all).count();

    std::cout << "\nTest Summary: " << passed_count << "/" << selected_tests.size() << " passed ("
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

    app.require_subcommand(0, 1);
    app.allow_extras();

    CLI11_PARSE(app, argc, argv);

    if (!opt.script_path.empty()) {
        try {
            std::filesystem::path script_abs = std::filesystem::absolute(opt.script_path);
            if (!std::filesystem::exists(script_abs)) {
                std::cerr << "Error: Script not found: " << opt.script_path << std::endl;
                return 1;
            }

            std::ifstream file(script_abs);
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

            dmake::Parser parser(content);
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

        std::cout << "\033[1;32mRunning\033[0m " << exec_path << "..." << std::endl;
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

    if (!build_cmd->parsed() && !test_cmd->parsed() && !run_cmd->parsed() && !clean_cmd->parsed()) {
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
