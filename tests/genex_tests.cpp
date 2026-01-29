#include <catch2/catch_test_macros.hpp>
#include "../dmake/genex_parser.hpp"
#include "../dmake/genex_evaluator.hpp"
#include "../dmake/target.hpp"
#include <map>
#include <memory>

using namespace dmake;

TEST_CASE("GenexParser - Simple literal", "[genex][parser]") {
    GenexParser parser;
    auto result = parser.parse("no genex here");
    REQUIRE(result.has_value());
    REQUIRE(result->has_genex == false);
    REQUIRE(result->nodes.size() == 1);
    REQUIRE(result->nodes[0]->type == GenexNodeType::LITERAL);
    REQUIRE(result->nodes[0]->raw_content == "no genex here");
}

TEST_CASE("GenexParser - BUILD_INTERFACE", "[genex][parser]") {
    GenexParser parser;
    auto result = parser.parse("$<BUILD_INTERFACE:/path/to/include>");
    REQUIRE(result.has_value());
    REQUIRE(result->has_genex == true);
    REQUIRE(result->nodes.size() == 1);
    REQUIRE(result->nodes[0]->type == GenexNodeType::BUILD_INTERFACE);
}

TEST_CASE("GenexParser - INSTALL_INTERFACE", "[genex][parser]") {
    GenexParser parser;
    auto result = parser.parse("$<INSTALL_INTERFACE:include>");
    REQUIRE(result.has_value());
    REQUIRE(result->has_genex == true);
    REQUIRE(result->nodes.size() == 1);
    REQUIRE(result->nodes[0]->type == GenexNodeType::INSTALL_INTERFACE);
}

TEST_CASE("GenexParser - CONFIG", "[genex][parser]") {
    GenexParser parser;
    auto result = parser.parse("$<CONFIG:Debug>");
    REQUIRE(result.has_value());
    REQUIRE(result->has_genex == true);
    REQUIRE(result->nodes.size() == 1);
    REQUIRE(result->nodes[0]->type == GenexNodeType::CONFIG);
    REQUIRE(result->nodes[0]->raw_content == "Debug");
}

TEST_CASE("GenexParser - Mixed literal and genex", "[genex][parser]") {
    GenexParser parser;
    auto result = parser.parse("prefix_$<CONFIG:Debug>_suffix");
    REQUIRE(result.has_value());
    REQUIRE(result->has_genex == true);
    REQUIRE(result->nodes.size() == 3);
    REQUIRE(result->nodes[0]->type == GenexNodeType::LITERAL);
    REQUIRE(result->nodes[0]->raw_content == "prefix_");
    REQUIRE(result->nodes[1]->type == GenexNodeType::CONFIG);
    REQUIRE(result->nodes[2]->type == GenexNodeType::LITERAL);
    REQUIRE(result->nodes[2]->raw_content == "_suffix");
}

TEST_CASE("GenexParser - Nested genex", "[genex][parser]") {
    GenexParser parser;
    auto result = parser.parse("$<$<CONFIG:Debug>:-g>");
    REQUIRE(result.has_value());
    REQUIRE(result->has_genex == true);
}

TEST_CASE("GenexParser - Validation - supported genex", "[genex][parser][validation]") {
    auto result = GenexParser::validate_genex_support("$<BUILD_INTERFACE:/path>");
    REQUIRE(result.has_value());
}

TEST_CASE("GenexParser - Validation - unsupported genex", "[genex][parser][validation]") {
    auto result = GenexParser::validate_genex_support("$<TARGET_FILE:foo>");
    REQUIRE(!result.has_value());
    REQUIRE(result.error().find("Unsupported") != std::string::npos);
}

TEST_CASE("GenexParser - Validation - malformed genex", "[genex][parser][validation]") {
    auto result = GenexParser::validate_genex_support("$<CONFIG:Debug");
    REQUIRE(!result.has_value());
    REQUIRE(result.error().find("Unmatched") != std::string::npos);
}

TEST_CASE("GenexEvaluator - BUILD_INTERFACE in BUILD phase", "[genex][evaluator]") {
    GenexEvaluationContext ctx;
    ctx.phase = GenexEvaluationContext::Phase::BUILD;
    GenexEvaluator eval(ctx);

    auto result = eval.evaluate("$<BUILD_INTERFACE:/path/to/include>");
    REQUIRE(result.has_value());
    REQUIRE(*result == "/path/to/include");
}

TEST_CASE("GenexEvaluator - INSTALL_INTERFACE in BUILD phase", "[genex][evaluator]") {
    GenexEvaluationContext ctx;
    ctx.phase = GenexEvaluationContext::Phase::BUILD;
    GenexEvaluator eval(ctx);

    auto result = eval.evaluate("$<INSTALL_INTERFACE:include>");
    REQUIRE(result.has_value());
    REQUIRE(*result == "");  // Empty in BUILD phase
}

TEST_CASE("GenexEvaluator - CONFIG matching", "[genex][evaluator]") {
    GenexEvaluationContext ctx;
    ctx.build_type = "Debug";
    GenexEvaluator eval(ctx);

    auto result = eval.evaluate("$<CONFIG:Debug>");
    REQUIRE(result.has_value());
    REQUIRE(*result == "1");

    result = eval.evaluate("$<CONFIG:Release>");
    REQUIRE(result.has_value());
    REQUIRE(*result == "0");
}

TEST_CASE("GenexEvaluator - CONFIG case-insensitive", "[genex][evaluator]") {
    GenexEvaluationContext ctx;
    ctx.build_type = "Debug";
    GenexEvaluator eval(ctx);

    auto result = eval.evaluate("$<CONFIG:debug>");
    REQUIRE(result.has_value());
    REQUIRE(*result == "1");
}

TEST_CASE("GenexEvaluator - Nested CONFIG conditional", "[genex][evaluator]") {
    GenexEvaluationContext ctx;
    ctx.build_type = "Debug";
    GenexEvaluator eval(ctx);

    auto result = eval.evaluate("$<$<CONFIG:Debug>:-g>");
    REQUIRE(result.has_value());
    REQUIRE(*result == "-g");

    ctx.build_type = "Release";
    GenexEvaluator eval2(ctx);
    result = eval2.evaluate("$<$<CONFIG:Debug>:-g>");
    REQUIRE(result.has_value());
    REQUIRE(*result == "");
}

TEST_CASE("GenexEvaluator - BOOL expression", "[genex][evaluator]") {
    GenexEvaluationContext ctx;
    GenexEvaluator eval(ctx);

    // Truthy values
    REQUIRE(eval.evaluate("$<BOOL:ON>").value() == "1");
    REQUIRE(eval.evaluate("$<BOOL:YES>").value() == "1");
    REQUIRE(eval.evaluate("$<BOOL:TRUE>").value() == "1");
    REQUIRE(eval.evaluate("$<BOOL:1>").value() == "1");
    REQUIRE(eval.evaluate("$<BOOL:Y>").value() == "1");
    REQUIRE(eval.evaluate("$<BOOL:anything>").value() == "1");

    // Falsy values
    REQUIRE(eval.evaluate("$<BOOL:>").value() == "0");
    REQUIRE(eval.evaluate("$<BOOL:0>").value() == "0");
    REQUIRE(eval.evaluate("$<BOOL:OFF>").value() == "0");
    REQUIRE(eval.evaluate("$<BOOL:NO>").value() == "0");
    REQUIRE(eval.evaluate("$<BOOL:FALSE>").value() == "0");
    REQUIRE(eval.evaluate("$<BOOL:N>").value() == "0");
    REQUIRE(eval.evaluate("$<BOOL:IGNORE>").value() == "0");
    REQUIRE(eval.evaluate("$<BOOL:NOTFOUND>").value() == "0");
}

TEST_CASE("GenexEvaluator - NOT expression", "[genex][evaluator]") {
    GenexEvaluationContext ctx;
    GenexEvaluator eval(ctx);

    REQUIRE(eval.evaluate("$<NOT:ON>").value() == "0");
    REQUIRE(eval.evaluate("$<NOT:OFF>").value() == "1");
    REQUIRE(eval.evaluate("$<NOT:0>").value() == "1");
    REQUIRE(eval.evaluate("$<NOT:1>").value() == "0");
}

TEST_CASE("GenexEvaluator - AND expression", "[genex][evaluator]") {
    GenexEvaluationContext ctx;
    GenexEvaluator eval(ctx);

    REQUIRE(eval.evaluate("$<AND:1,1>").value() == "1");
    REQUIRE(eval.evaluate("$<AND:1,0>").value() == "0");
    REQUIRE(eval.evaluate("$<AND:0,1>").value() == "0");
    REQUIRE(eval.evaluate("$<AND:0,0>").value() == "0");
    REQUIRE(eval.evaluate("$<AND:1,1,1>").value() == "1");
    REQUIRE(eval.evaluate("$<AND:1,1,0>").value() == "0");
}

TEST_CASE("GenexEvaluator - OR expression", "[genex][evaluator]") {
    GenexEvaluationContext ctx;
    GenexEvaluator eval(ctx);

    REQUIRE(eval.evaluate("$<OR:1,1>").value() == "1");
    REQUIRE(eval.evaluate("$<OR:1,0>").value() == "1");
    REQUIRE(eval.evaluate("$<OR:0,1>").value() == "1");
    REQUIRE(eval.evaluate("$<OR:0,0>").value() == "0");
    REQUIRE(eval.evaluate("$<OR:0,0,1>").value() == "1");
}

TEST_CASE("GenexEvaluator - STREQUAL expression", "[genex][evaluator]") {
    GenexEvaluationContext ctx;
    GenexEvaluator eval(ctx);

    REQUIRE(eval.evaluate("$<STREQUAL:foo,foo>").value() == "1");
    REQUIRE(eval.evaluate("$<STREQUAL:foo,bar>").value() == "0");
    REQUIRE(eval.evaluate("$<STREQUAL:,>").value() == "1");
}

TEST_CASE("GenexEvaluator - IF expression", "[genex][evaluator]") {
    GenexEvaluationContext ctx;
    GenexEvaluator eval(ctx);

    REQUIRE(eval.evaluate("$<IF:1,true_val,false_val>").value() == "true_val");
    REQUIRE(eval.evaluate("$<IF:0,true_val,false_val>").value() == "false_val");
}

TEST_CASE("GenexEvaluator - TARGET_EXISTS", "[genex][evaluator]") {
    std::map<std::string, std::shared_ptr<Target>> targets;
    targets["mylib"] = std::make_shared<Target>("mylib", TargetType::STATIC_LIBRARY, "/src", "/build");

    GenexEvaluationContext ctx;
    ctx.all_targets = &targets;
    GenexEvaluator eval(ctx);

    REQUIRE(eval.evaluate("$<TARGET_EXISTS:mylib>").value() == "1");
    REQUIRE(eval.evaluate("$<TARGET_EXISTS:notexist>").value() == "0");
}

TEST_CASE("GenexEvaluator - COMPILE_LANGUAGE", "[genex][evaluator]") {
    GenexEvaluationContext ctx;
    ctx.compile_language = Language::CXX;
    GenexEvaluator eval(ctx);

    REQUIRE(eval.evaluate("$<COMPILE_LANGUAGE:CXX>").value() == "1");
    REQUIRE(eval.evaluate("$<COMPILE_LANGUAGE:C>").value() == "0");

    ctx.compile_language = Language::C;
    GenexEvaluator eval2(ctx);
    REQUIRE(eval2.evaluate("$<COMPILE_LANGUAGE:C>").value() == "1");
    REQUIRE(eval2.evaluate("$<COMPILE_LANGUAGE:CXX>").value() == "0");
}

TEST_CASE("GenexEvaluator - PLATFORM_ID", "[genex][evaluator]") {
    GenexEvaluationContext ctx;
    ctx.system_name = "Linux";
    GenexEvaluator eval(ctx);

    REQUIRE(eval.evaluate("$<PLATFORM_ID:Linux>").value() == "1");
    REQUIRE(eval.evaluate("$<PLATFORM_ID:Windows>").value() == "0");
}

TEST_CASE("GenexEvaluator - CXX_COMPILER_ID", "[genex][evaluator]") {
    GenexEvaluationContext ctx;
    ctx.cxx_compiler_id = "GNU";
    GenexEvaluator eval(ctx);

    REQUIRE(eval.evaluate("$<CXX_COMPILER_ID:GNU>").value() == "1");
    REQUIRE(eval.evaluate("$<CXX_COMPILER_ID:Clang>").value() == "0");
}

TEST_CASE("GenexEvaluator - C_COMPILER_ID", "[genex][evaluator]") {
    GenexEvaluationContext ctx;
    ctx.c_compiler_id = "GNU";
    GenexEvaluator eval(ctx);

    REQUIRE(eval.evaluate("$<C_COMPILER_ID:GNU>").value() == "1");
    REQUIRE(eval.evaluate("$<C_COMPILER_ID:Clang>").value() == "0");
}

TEST_CASE("GenexEvaluator - Property list evaluation", "[genex][evaluator]") {
    GenexEvaluationContext ctx;
    ctx.build_type = "Debug";
    ctx.phase = GenexEvaluationContext::Phase::BUILD;
    GenexEvaluator eval(ctx);

    std::vector<std::string> values = {
        "$<BUILD_INTERFACE:/include>",
        "$<INSTALL_INTERFACE:include>",
        "$<$<CONFIG:Debug>:/debug/include>",
        "/always/include"
    };

    auto result = eval.evaluate_property_list(values);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 3);  // INSTALL_INTERFACE and non-matching CONFIG are removed
    REQUIRE((*result)[0] == "/include");
    REQUIRE((*result)[1] == "/debug/include");
    REQUIRE((*result)[2] == "/always/include");
}

TEST_CASE("GenexEvaluator - Complex nested expression", "[genex][evaluator]") {
    GenexEvaluationContext ctx;
    ctx.build_type = "Debug";
    ctx.cxx_compiler_id = "GNU";
    GenexEvaluator eval(ctx);

    // $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU>>:-g -O0>
    auto result = eval.evaluate("$<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU>>:-g -O0>");
    REQUIRE(result.has_value());
    REQUIRE(*result == "-g -O0");

    ctx.cxx_compiler_id = "Clang";
    GenexEvaluator eval2(ctx);
    result = eval2.evaluate("$<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU>>:-g -O0>");
    REQUIRE(result.has_value());
    REQUIRE(*result == "");
}

TEST_CASE("GenexParser - Extract genex types", "[genex][parser]") {
    auto result = GenexParser::extract_genex_types("$<BUILD_INTERFACE:/path> $<CONFIG:Debug>");
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 2);
    REQUIRE(result->count(GenexNodeType::BUILD_INTERFACE) == 1);
    REQUIRE(result->count(GenexNodeType::CONFIG) == 1);
}
