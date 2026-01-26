#include "dmake/cmake-language.hpp"
#include "dmake/interperter.hpp"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <CLI/CLI.hpp>
#include <unistd.h>

namespace {

void print_error_context(const std::string& file_path, size_t row, size_t col, const std::string& message) {
    std::cerr << "\033[1;31merror:\033[0m " << message << std::endl;
    std::cerr << "  \033[1;34m-->\033[0m " << file_path << ":" << row << ":" << col << std::endl;

    std::ifstream error_file(file_path);
    if (!error_file) return;

    std::string file_content((std::istreambuf_iterator<char>(error_file)), std::istreambuf_iterator<char>());

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
    std::cerr << "   " << padding << " \033[1;34m|\033[0m " << std::string(col > 0 ? col - 1 : 0, ' ') << "\033[1;31m^\033[0m" << std::endl;
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

} // namespace

int main(int argc, char* argv[]) {
    CLI::App app{"dmake - A modern C++ build system with CMake compatibility."};

    std::string project_dir_str = ".";
    app.add_option("directory", project_dir_str, "Project directory (default: .)");

    std::string script_path;
    app.add_option("-P", script_path, "Run dmake in script mode");

    int jobs = 0;
    app.add_option("-j,--parallel", jobs, "Parallel jobs (default: 0 for all cores)");

    std::string build_dir_str;
    app.add_option("-B", build_dir_str, "Build directory (default: <project>/build)");

    std::vector<std::string> definitions;
    app.add_option("-D", definitions, "Define variables (VAR=VALUE)");

    std::string config = "debug";
    app.add_option("-c,--config", config, "Build configuration (debug, release, relwithdebinfo, minsizerel)")
       ->transform([](const std::string& value) -> std::string {
           auto copy = value;
           std::transform(copy.begin(), copy.end(), copy.begin(), ::tolower);
           if (copy == "debug" || copy == "release" || copy == "relwithdebinfo" || copy == "minsizerel") {
               return copy;
           }
           throw CLI::ValidationError("Invalid configuration");
       }, "convert to lower case", "lowercase");

    CLI11_PARSE(app, argc, argv);

    try {
        if (!script_path.empty()) {
            // Script Mode
            std::filesystem::path script_abs = std::filesystem::absolute(script_path);
            if (!std::filesystem::exists(script_abs)) {
                std::cerr << "Error: Script not found: " << script_path << std::endl;
                return 1;
            }

            std::ifstream file(script_abs);
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

            dmake::Parser parser(content);
            auto ast_or_error = parser.parse();
            if (!ast_or_error) {
                print_error_context(script_abs.string(), ast_or_error.error().row, ast_or_error.error().col, ast_or_error.error().reason);
                return 1;
            }

            dmake::Interpreter interpreter(std::filesystem::current_path().string(), &std::cout, &std::cerr);
            interpreter.set_current_file(script_abs.string());
            apply_definitions(interpreter, definitions);

            auto result = interpreter.interpret(ast_or_error.value());
            if (!result) {
                print_error_context(result.error().file, result.error().row, result.error().col, result.error().message);
                return 1;
            }
            return 0;
        } else {
            // Build Mode
            std::filesystem::path project_path = std::filesystem::canonical(project_dir_str);
            std::filesystem::path cmake_lists = project_path / "CMakeLists.txt";

            if (!std::filesystem::exists(cmake_lists)) {
                std::cerr << "Error: CMakeLists.txt not found in " << project_path.string() << std::endl;
                return 1;
            }

            std::filesystem::path build_root = build_dir_str.empty() ? (project_path / "build") : std::filesystem::absolute(build_dir_str).lexically_normal();
            std::filesystem::path build_path = build_root / config;

            if (build_path == project_path) {
                std::cerr << "Error: Build directory cannot be source directory." << std::endl;
                return 1;
            }

            std::ifstream file(cmake_lists);
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

            dmake::Parser parser(content);
            auto ast_or_error = parser.parse();
            if (!ast_or_error) {
                print_error_context(cmake_lists.string(), ast_or_error.error().row, ast_or_error.error().col, ast_or_error.error().reason);
                return 1;
            }

            dmake::Interpreter interpreter(project_path.string(), &std::cout, &std::cerr, nullptr, build_path.string());
            interpreter.set_current_file(cmake_lists.string());
            apply_definitions(interpreter, definitions);

            // Set CMAKE_BUILD_TYPE if not overridden
            if (interpreter.get_variable("CMAKE_BUILD_TYPE").empty()) {
                interpreter.set_variable("CMAKE_BUILD_TYPE", to_cmake_case(config));
            }

            set_default_flags(interpreter, definitions, "CMAKE_CXX_FLAGS_DEBUG", "-g -O0");
            set_default_flags(interpreter, definitions, "CMAKE_CXX_FLAGS_RELEASE", "-O3 -DNDEBUG");
            set_default_flags(interpreter, definitions, "CMAKE_CXX_FLAGS_RELWITHDEBINFO", "-g -O2 -DNDEBUG");
            set_default_flags(interpreter, definitions, "CMAKE_CXX_FLAGS_MINSIZEREL", "-Os -DNDEBUG");

            auto interpret_result = interpreter.interpret(ast_or_error.value());
            if (!interpret_result) {
                print_error_context(interpret_result.error().file, interpret_result.error().row, interpret_result.error().col, interpret_result.error().message);
                return 1;
            }

            auto build_result = interpreter.run_build(jobs);
            if (!build_result) {
                std::cerr << "\033[1;31merror:\033[0m " << build_result.error().message << std::endl;
                return 1;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
