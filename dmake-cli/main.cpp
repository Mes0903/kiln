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

    dmake::Interpreter interpreter(directory_path.string(), &std::cout, &std::cerr);
    interpreter.set_current_file(cmake_lists_path.string());
    interpreter.print_message("STATUS", "Finished collecting build information");

    auto interpret_result = interpreter.interpret(ast_or_error.value());
    if (!interpret_result) {
        const auto& error = interpret_result.error();
        print_error_context(error.file, error.row, error.col, error.message);
        return 1;
    }

    auto build_result = interpreter.run_build();
    if (!build_result) {
        const auto& error = build_result.error();
        std::cerr << "\033[1;31merror:\033[0m " << error.message << std::endl;
        return 1;
    }

    return 0;
}
