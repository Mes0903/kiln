#pragma once

#include "cmake-language.hpp"
#include <expected>
#include <vector>

namespace dmake {

class Interpreter;
struct InterpreterError;

// Extracted from Interpreter::evaluate_condition() for maintainability.
// This is a self-contained recursive descent parser for CMake if() conditions.
std::expected<bool, InterpreterError> evaluate_condition(
    Interpreter& interp,
    const std::vector<Argument>& condition,
    size_t row, size_t col, size_t offset, size_t length);

} // namespace dmake
