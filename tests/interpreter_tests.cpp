#include <catch2/catch_test_macros.hpp>
#include "dmake/interperter.hpp"
#include "dmake/cmake-language.hpp"
#include <sstream>

TEST_CASE("Interpreter variable substitution", "[interpreter]") {
    std::stringstream output;
    dmake::Interpreter interpreter("", &output);

    interpreter.add_builtin("message", [&](const std::vector<dmake::Argument>& args) {
        // A simple implementation for testing purposes
        for (const auto& arg : args) {
            output << interpreter.evaluate_argument(arg);
        }
        output << std::endl;
    });

    std::string content = R"(
        set(MY_VAR "Hello")
        message("${MY_VAR} World")
    )";

    dmake::Parser parser(content);
    auto ast_or_error = parser.parse();
    REQUIRE(ast_or_error.has_value());

    interpreter.interpret(ast_or_error.value());

    REQUIRE(output.str() == "Hello World\n");
}
