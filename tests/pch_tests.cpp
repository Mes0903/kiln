#include <catch2/catch_test_macros.hpp>
#include "dmake/interperter.hpp"
#include "dmake/target.hpp"
#include "dmake/build_system.hpp"
#include "dmake/cmake-language.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace dmake;

TEST_CASE("PCH Task Generation", "[target][pch]") {
    // Create a dummy environment
    std::string temp_dir = "build_test_pch";
    std::filesystem::create_directories(temp_dir);
    std::filesystem::create_directories(temp_dir + "/objs");

    // Create dummy source files for existence checks
    {
        std::ofstream main_cpp("main.cpp");
        main_cpp << "int main() { return 0; }\n";
        std::ofstream my_lib_cpp("my_lib.cpp");
        my_lib_cpp << "void foo() {}\n";
        std::ofstream my_pch_h("my_pch.h");
        my_pch_h << "#define PCH_TEST\n";
    }

    Interpreter interp(".", &std::cout, &std::cerr, nullptr, temp_dir);
    
    // Register builtins
    register_target_builtins(interp);

    // Setup a project with PCH
    std::string script = R"(
        add_library(my_lib my_lib.cpp)
        target_precompile_headers(my_lib PRIVATE my_pch.h)
    )";
    
    Parser parser(script);
    auto ast_or_error = parser.parse();
    REQUIRE(ast_or_error.has_value());

    auto result = interp.interpret(ast_or_error.value());
    if (!result) {
        std::cerr << "Interpreter error: " << result.error().message << std::endl;
    }
    REQUIRE(result.has_value());

    auto& targets = interp.get_targets();
    REQUIRE(targets.count("my_lib") > 0);
    auto lib = targets["my_lib"];

    BuildGraph graph;
    lib->generate_tasks(graph);

    // Build directory as used by the interpreter
    std::string build_dir_abs = std::filesystem::absolute(temp_dir).lexically_normal().string();

    // Check if PCH task was generated
    std::string pch_wrapper_expected = std::filesystem::path(build_dir_abs) / "objs" / "my_lib_pch.hpp";
    pch_wrapper_expected = std::filesystem::path(pch_wrapper_expected).lexically_normal().string();
    std::string pch_gch_expected = pch_wrapper_expected + ".gch";
    
    REQUIRE(graph.has_task(pch_gch_expected));
    auto& pch_task = graph.get_task(pch_gch_expected);
    
    // PCH task should have the wrapper as input
    bool has_wrapper = false;
    for (const auto& in : pch_task.inputs) {
        if (in == pch_wrapper_expected) has_wrapper = true;
    }
    REQUIRE(has_wrapper);

    // Check if the object file task depends on the PCH
    std::string obj_file = std::filesystem::path(build_dir_abs) / "objs" / "my_lib.cpp.o";
    obj_file = std::filesystem::path(obj_file).lexically_normal().string();
    REQUIRE(graph.has_task(obj_file));
    auto& obj_task = graph.get_task(obj_file);
    
    REQUIRE(obj_task.dependencies.count(pch_gch_expected) > 0);
    REQUIRE(obj_task.command.find("-include " + pch_wrapper_expected) != std::string::npos);

    // Cleanup
    std::filesystem::remove_all(temp_dir);
    std::filesystem::remove("main.cpp");
    std::filesystem::remove("my_lib.cpp");
    std::filesystem::remove("my_pch.h");
}
