#pragma once

namespace dmake {

class Interpreter;

void register_message_builtins(Interpreter& interp);
void register_variable_builtins(Interpreter& interp);
void register_list_builtins(Interpreter& interp);
void register_target_builtins(Interpreter& interp);
void register_project_builtins(Interpreter& interp);
void register_file_builtins(Interpreter& interp);
void register_find_package_builtins(Interpreter& interp);
void register_math_builtins(Interpreter& interp);

} // namespace dmake
