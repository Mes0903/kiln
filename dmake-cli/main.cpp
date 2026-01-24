#include "dmake/cmake-language.hpp"
#include "dmake/interperter.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <CLI/CLI.hpp> // Include CLI11 header

int main(int argc, char* argv[]) {
    CLI::App app{"dmake - A CMake-like build system interpreter"};

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
        std::cerr << "Parse failed at " << error.row << ":" << error.col << " - " << error.reason << std::endl;
        return 1;
    }

    return 0;
}
