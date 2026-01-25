#include "dmake/cmake-language.hpp"
#include "dmake/interperter.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <CLI/CLI.hpp> // Include CLI11 header

int main(int argc, char* argv[]) {
    CLI::App app{"dmake - A CMake-like but a build system"};

    std::string directory_path_str;
    app.add_option("directory", directory_path_str, "Path to the project directory containing CMakeLists.txt")
       ->required()
       ->check(CLI::ExistingDirectory);

    CLI11_PARSE(app, argc, argv);

    std::filesystem::path directory_path(directory_path_str);
    std::filesystem::path cmake_lists_path = directory_path / "CMakeLists.txt";

    if (!std::filesystem::exists(cmake_lists_path)) {
        std::cerr << "Error: CMakeLists.txt not found in " << directory_path.string() << std::endl;
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

    if (ast_or_error) {
        dmake::Interpreter interpreter(directory_path.string(), &std::cout, &std::cerr);
        interpreter.interpret(ast_or_error.value());
        interpreter.run_build();
    } else {
        const auto& error = ast_or_error.error();

        // Print header with file location
        std::cerr << cmake_lists_path.string() << ":" << error.row << ":" << error.col << ": error: " << error.reason << std::endl;

        // Find the start of the error line by counting newlines
        size_t line_start = 0;
        size_t current_line = 1;
        for (size_t i = 0; i < content.size() && current_line < error.row; ++i) {
            if (content[i] == '\n') {
                ++current_line;
                line_start = i + 1;
            }
        }

        // Find the end of the line
        size_t line_end = content.find('\n', line_start);
        if (line_end == std::string::npos) line_end = content.size();

        std::string line = content.substr(line_start, line_end - line_start);

        // Print the line with line number
        std::cerr << "    " << error.row << " | " << line << std::endl;

        // Print the pointer
        std::string prefix = "    " + std::to_string(error.row) + " | ";
        std::cerr << std::string(prefix.length() + error.col - 1, ' ') << "^" << std::endl;

        return 1;
    }

    return 0;
}
