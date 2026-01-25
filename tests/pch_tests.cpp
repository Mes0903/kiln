#include <catch2/catch_test_macros.hpp>
#include "dmake/interperter.hpp"
#include "dmake/artifact.hpp"
#include "dmake/build_system.hpp"
#include "dmake/cmake-language.hpp"
#include <filesystem>
#include <fstream>

using namespace dmake;

TEST_CASE("PCH task generation", "[pch]") {
    Interpreter interp(".");
    register_target_builtins(interp);
    register_variable_builtins(interp);

    interp.set_variable("CMAKE_CURRENT_SOURCE_DIR", ".");
    interp.set_variable("CMAKE_CURRENT_BINARY_DIR", "./build");

    // Create dummy files for existence check
    {
        std::ofstream f1("my_lib.cpp");
        std::ofstream f2("pch.h");
    }

    dmake::Parser parser(R"(
        add_library(my_lib my_lib.cpp)
        target_precompile_headers(my_lib PRIVATE "pch.h")
    )");
    auto ast_or_err = parser.parse();
    REQUIRE(ast_or_err.has_value());
    auto res = interp.interpret(ast_or_err.value());
    if (!res.has_value()) {
        std::filesystem::remove("my_lib.cpp");
        std::filesystem::remove("pch.h");
    }
    REQUIRE(res.has_value());

    auto& artifacts = interp.get_artifacts();
    REQUIRE(artifacts.count("my_lib") > 0);
    auto lib = artifacts["my_lib"];

    BuildGraph graph;
    lib->generate_tasks(graph);

    std::string pch_out = "build/objs/my_lib_pch.hpp.gch";
    std::string obj_out = "build/objs/my_lib.cpp.o";

    REQUIRE(graph.has_task(pch_out));
    REQUIRE(graph.has_task(obj_out));

    auto& obj_task = graph.get_task(obj_out);
    REQUIRE(obj_task.dependencies.count(pch_out) > 0);
    REQUIRE(obj_task.command.find("-include build/objs/my_lib_pch.hpp") != std::string::npos);

    auto& pch_task = graph.get_task(pch_out);
    REQUIRE(pch_task.command.find("-x c++-header") != std::string::npos);

    // Clean up
    std::filesystem::remove("my_lib.cpp");
    std::filesystem::remove("pch.h");
    std::filesystem::remove("build/objs/my_lib_pch.hpp");
}
