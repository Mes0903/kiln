#include "dmake/cmake-language.hpp"
#include "dmake/interperter.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <CLI/CLI.hpp> // Include CLI11 header

int main(int argc, char* argv[]) {
    CLI::App app{"dmake - Better C/C++ builds that just works with CMake as an input language."};

    std::string directory_path_str;
    app.add_option("directory", directory_path_str, "Path to the project directory containing CMakeLists.txt")
       ->required()
       ->check(CLI::ExistingDirectory);

    int jobs = 0;
    app.add_option("-j,--parallel", jobs, "Number of parallel jobs to run (default: 0, which uses all available cores)");

    std::string build_directory_str;
    app.add_option("-B", build_directory_str, "Path to the build directory (default: <directory>/build)");

    std::vector<std::string> definitions;
    app.add_option("-D", definitions, "Define a variable in the format VAR=VALUE or VAR");

    std::string config = "debug";
    app.add_option("-c,--config", config, "Build configuration (debug, release, relwithdebinfo, minsizerel)")
       ->transform(CLI::Transformer({
           {"debug", "debug"},
           {"release", "release"},
           {"relwithdebinfo", "relwithdebinfo"},
           {"minsizerel", "minsizerel"}
       }, CLI::ignore_case));

    CLI11_PARSE(app, argc, argv);

    std::filesystem::path directory_path;
    try {
        directory_path = std::filesystem::canonical(directory_path_str);
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error: Invalid project directory: " << e.what() << std::endl;
        return 1;
    }

    std::filesystem::path cmake_lists_path = directory_path / "CMakeLists.txt";

    if (!std::filesystem::exists(cmake_lists_path)) {
        std::cerr << "Error: CMakeLists.txt not found in " << directory_path.string() << std::endl;
        return 1;
    }

    // Determine build root directory
    std::filesystem::path build_root;
    if (build_directory_str.empty()) {
        build_root = directory_path / "build";
    } else {
        build_root = std::filesystem::absolute(build_directory_str).lexically_normal();
    }

    // Normalize config to lowercase for directory name
    std::string config_lower = config;
    std::transform(config_lower.begin(), config_lower.end(), config_lower.begin(), ::tolower);

    // Append config to build root to get final build path
    std::filesystem::path build_path = build_root / config_lower;

    if (build_path == directory_path) {
        std::cerr << "Error: Build directory cannot be the same as the source directory: " << directory_path.string() << std::endl;
        return 1;
    }

    std::ifstream file(cmake_lists_path);
    if (!file) {
        std::cerr << "Error: Could not open file: " << cmake_lists_path << std::endl;
        return 1;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    dmake::Parser parser(content);
    auto ast_or_error = parser.parse();

    auto print_error_context = [](const std::string& file_path, size_t row, size_t col, const std::string& message) {
        std::cerr << "\033[1;31merror:\033[0m " << message << std::endl;
        std::cerr << "  \033[1;34m-->\033[0m " << file_path << ":" << row << ":" << col << std::endl;

        // Read the file to get the error line
        std::ifstream error_file(file_path);
        if (!error_file) {
            return;
        }

        std::string file_content;
        std::ostringstream ss;
        ss << error_file.rdbuf();
        file_content = ss.str();

        // Find the start of the error line by counting newlines
        size_t line_start = 0;
        size_t current_line = 1;
        for (size_t i = 0; i < file_content.size() && current_line < row; ++i) {
            if (file_content[i] == '\n') {
                ++current_line;
                line_start = i + 1;
            }
        }

        // Find the end of the line
        size_t line_end = file_content.find('\n', line_start);
        if (line_end == std::string::npos) line_end = file_content.size();

        std::string line = file_content.substr(line_start, line_end - line_start);

        // Calculate the width for line number padding
        std::string line_num_str = std::to_string(row);
        std::string padding(line_num_str.length(), ' ');

        // Print the line with line number
        std::cerr << "   " << padding << " \033[1;34m|\033[0m" << std::endl;
        std::cerr << "   \033[1;34m" << row << " |\033[0m " << line << std::endl;
        std::cerr << "   " << padding << " \033[1;34m|\033[0m " << std::string(col > 0 ? col - 1 : 0, ' ') << "\033[1;31m^\033[0m" << std::endl;
    };

    if (!ast_or_error) {
        const auto& error = ast_or_error.error();
        print_error_context(cmake_lists_path.string(), error.row, error.col, error.reason);
        return 1;
    }

    dmake::Interpreter interpreter(directory_path.string(), &std::cout, &std::cerr, nullptr, build_path.string());
    interpreter.set_current_file(cmake_lists_path.string());

    // Apply CLI definitions
    for (const auto& def : definitions) {
        size_t eq = def.find('=');
        if (eq != std::string::npos) {
            interpreter.set_variable(def.substr(0, eq), def.substr(eq + 1));
        } else {
            interpreter.set_variable(def, "ON");
        }
    }

    // Helper to convert config to proper CMake case
    auto to_cmake_case = [](const std::string& config) -> std::string {
        if (config.empty()) return "";
        std::string result = config;
        result[0] = std::toupper(result[0]);
        // Handle special cases
        if (result == "Relwithdebinfo") return "RelWithDebInfo";
        if (result == "Minsizerel") return "MinSizeRel";
        return result;
    };

    // Check if CMAKE_BUILD_TYPE was set via -D
    bool cmake_build_type_set = false;
    for (const auto& def : definitions) {
        if (def.starts_with("CMAKE_BUILD_TYPE=") || def == "CMAKE_BUILD_TYPE") {
            cmake_build_type_set = true;
            break;
        }
    }

    // Set CMAKE_BUILD_TYPE if not already set via -D flag
    if (!cmake_build_type_set) {
        interpreter.set_variable("CMAKE_BUILD_TYPE", to_cmake_case(config));
    }

    // Set baseline CMAKE_CXX_FLAGS_<CONFIG> if not already set
    // These provide sensible defaults but can be overridden by user
    auto set_default_flags = [&](const std::string& var, const std::string& flags) {
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
    };

    set_default_flags("CMAKE_CXX_FLAGS_DEBUG", "-g -O0");
    set_default_flags("CMAKE_CXX_FLAGS_RELEASE", "-O3 -DNDEBUG");
    set_default_flags("CMAKE_CXX_FLAGS_RELWITHDEBINFO", "-g -O2 -DNDEBUG");
    set_default_flags("CMAKE_CXX_FLAGS_MINSIZEREL", "-Os -DNDEBUG");

    auto interpret_result = interpreter.interpret(ast_or_error.value());
    interpreter.print_message("STATUS", "Finished collecting build information");
    if (!interpret_result) {
        const auto& error = interpret_result.error();
        print_error_context(error.file, error.row, error.col, error.message);
        return 1;
    }


    auto build_result = interpreter.run_build(jobs);
    if (!build_result) {
        const auto& error = build_result.error();
        std::cerr << "\033[1;31merror:\033[0m " << error.message << std::endl;
        return 1;
    }

    return 0;
}
