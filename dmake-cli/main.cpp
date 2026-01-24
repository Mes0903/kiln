#include "dmake/cmake-language.hpp"
#include "dmake/interperter.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>

int main(int argc, char* argv[]) {
    std::string file_path_str = "CMakeLists.txt";
    if (argc > 1) {
        file_path_str = argv[1];
    }

    std::filesystem::path file_path(file_path_str);
    std::filesystem::path script_dir = file_path.parent_path();

    std::ifstream file(file_path);
    if (!file) {
        std::cerr << "Could not open file: " << file_path << std::endl;
        return 1;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    dmake::Parser parser(content);
    auto ast_or_error = parser.parse();

    if (ast_or_error) {
        dmake::Interpreter interpreter(script_dir.string(), &std::cout, &std::cerr);
        interpreter.interpret(ast_or_error.value());
        interpreter.run_build();
    } else {
        const auto& error = ast_or_error.error();
        std::cerr << "Parse failed at " << error.row << ":" << error.col << " - " << error.reason << std::endl;
        return 1;
    }

    return 0;
}
