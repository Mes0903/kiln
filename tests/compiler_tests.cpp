#include <catch2/catch_test_macros.hpp>
#include "kiln/gnu_compiler.hpp"
#include "kiln/utils.hpp"
#include <vector>

using namespace kiln;

TEST_CASE("GnuCompiler: CXX compile command", "[compiler]") {
    GnuCompiler compiler("g++", Language::CXX);
    CompileContext ctx;
    ctx.source = "main.cpp";
    ctx.output = "main.o";
    ctx.standard = "23";
    ctx.includes = {"include", "/usr/local/include"};
    ctx.definitions = {"DEBUG", "VERSION=1"};
    ctx.is_shared = true;
    
        std::vector<std::string> cmd_vec = compiler.get_compile_command(ctx);
    
        std::string cmd = kiln::join_command(cmd_vec);
    
            CHECK(cmd_vec[0] == "g++");
            CHECK(cmd_vec[1] == "-std=gnu++23");
            CHECK(std::find(cmd_vec.begin(), cmd_vec.end(), "-Iinclude") != cmd_vec.end());
            CHECK(std::find(cmd_vec.begin(), cmd_vec.end(), "-I/usr/local/include") != cmd_vec.end());
            CHECK(std::find(cmd_vec.begin(), cmd_vec.end(), "-DDEBUG") != cmd_vec.end());
            CHECK(std::find(cmd_vec.begin(), cmd_vec.end(), "-DVERSION=1") != cmd_vec.end());
            CHECK(std::find(cmd_vec.begin(), cmd_vec.end(), "-fPIC") != cmd_vec.end());
            CHECK(std::find(cmd_vec.begin(), cmd_vec.end(), "-c") != cmd_vec.end());
            CHECK(std::find(cmd_vec.begin(), cmd_vec.end(), "main.o") != cmd_vec.end());
            CHECK(std::find(cmd_vec.begin(), cmd_vec.end(), "main.cpp") != cmd_vec.end());
        }
    
        

TEST_CASE("GnuCompiler: C compile command", "[compiler]") {
    GnuCompiler compiler("gcc", Language::C);
    CompileContext ctx;
    ctx.source = "main.c";
    ctx.output = "main.o";
    ctx.standard = "11";
    
    std::vector<std::string> cmd_vec = compiler.get_compile_command(ctx);
    CHECK(cmd_vec[0] == "gcc");
    CHECK(std::find(cmd_vec.begin(), cmd_vec.end(), "-std=gnu11") != cmd_vec.end());
    CHECK(std::find(cmd_vec.begin(), cmd_vec.end(), "main.c") != cmd_vec.end());
}

TEST_CASE("GnuCompiler: Link command", "[compiler]") {
    GnuCompiler compiler("g++", Language::CXX);
    LinkContext ctx;
    ctx.output = "app";
    ctx.objects = {"main.o", "lib.o"};
    ctx.lib_dirs = {"build/lib"};
    ctx.libs = {"m", "pthread"};
    ctx.is_shared = false;
    
    std::vector<std::string> cmd_vec = compiler.get_link_command(ctx);
    CHECK(cmd_vec[0] == "g++");
    CHECK(std::find(cmd_vec.begin(), cmd_vec.end(), "-std=c++17") == cmd_vec.end()); // No standard set in ctx, so it should not be present
    CHECK(std::find(cmd_vec.begin(), cmd_vec.end(), "main.o") != cmd_vec.end());
    CHECK(std::find(cmd_vec.begin(), cmd_vec.end(), "lib.o") != cmd_vec.end());
    CHECK(std::find(cmd_vec.begin(), cmd_vec.end(), "-Lbuild/lib") != cmd_vec.end());
    CHECK(std::find(cmd_vec.begin(), cmd_vec.end(), "-lm") != cmd_vec.end());
    CHECK(std::find(cmd_vec.begin(), cmd_vec.end(), "-lpthread") != cmd_vec.end());
}
