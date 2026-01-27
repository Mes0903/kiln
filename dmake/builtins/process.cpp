#include "registry.hpp"
#include "../interperter.hpp"
#include "../command_parser.hpp"
#include "../utils.hpp"
#include <iostream>

namespace dmake {

void register_process_builtins(Interpreter& interp) {
    interp.add_builtin("execute_process", [](Interpreter& interp, const std::vector<std::string>& args) {
        std::cout << ")" << std::endl;
        CommandParser parser("execute_process");
        std::vector<std::vector<std::string>> commands;
        std::string working_dir;
        std::string timeout;
        std::string result_variable;
        std::string results_variable;
        std::string output_variable;
        std::string error_variable;
        std::string input_file;
        std::string output_file;
        std::string error_file;
        std::string command_echo;
        std::string encoding;
        bool output_quiet = false;
        bool error_quiet = false;
        bool output_strip_trailing_whitespace = false;
        bool error_strip_trailing_whitespace = false;
        bool echo_output_variable = false;
        bool echo_error_variable = false;
        bool command_error_is_fatal = false;

        parser.add_multi_list("COMMAND", commands);
        parser.add_value("WORKING_DIRECTORY", working_dir);
        parser.add_value("TIMEOUT", timeout);
        parser.add_value("RESULT_VARIABLE", result_variable);
        parser.add_value("RESULTS_VARIABLE", results_variable);
        parser.add_value("OUTPUT_VARIABLE", output_variable);
        parser.add_value("ERROR_VARIABLE", error_variable);
        parser.add_value("INPUT_FILE", input_file);
        parser.add_value("OUTPUT_FILE", output_file);
        parser.add_value("ERROR_FILE", error_file);
        parser.add_value("COMMAND_ECHO", command_echo);
        parser.add_value("ENCODING", encoding);
        parser.add_flag("OUTPUT_QUIET", output_quiet);
        parser.add_flag("ERROR_QUIET", error_quiet);
        parser.add_flag("OUTPUT_STRIP_TRAILING_WHITESPACE", output_strip_trailing_whitespace);
        parser.add_flag("ERROR_STRIP_TRAILING_WHITESPACE", error_strip_trailing_whitespace);
        parser.add_flag("ECHO_OUTPUT_VARIABLE", echo_output_variable);
        parser.add_flag("ECHO_ERROR_VARIABLE", echo_error_variable);
        parser.add_flag("COMMAND_ERROR_IS_FATAL", command_error_is_fatal);

        PARSE_OR_RETURN(parser, interp, args);

        if (commands.empty()) {
            interp.set_fatal_error("execute_process requires at least one COMMAND");
            return;
        }

        ProcessOptions options;
        options.working_dir = working_dir;
        options.input_file = input_file;
        options.output_file = output_file;
        options.error_file = error_file;
        options.output_quiet = output_quiet;
        options.error_quiet = error_quiet;

        if (!timeout.empty()) {
            try {
                options.timeout = std::stod(timeout);
            } catch (...) {
                interp.set_fatal_error("execute_process invalid TIMEOUT: " + timeout);
                return;
            }
        }

        if (!output_variable.empty()) options.output_variable = &output_variable;
        if (!error_variable.empty()) options.error_variable = &error_variable;

        PipelineResult res = execute_pipeline(commands, options);

        auto strip_trailing = [](std::string& s) {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
                s.pop_back();
            }
        };

        if (output_strip_trailing_whitespace) {
            strip_trailing(res.captured_stdout);
        }
        if (error_strip_trailing_whitespace) {
            strip_trailing(res.captured_stderr);
        }

        if (!output_variable.empty()) {
            interp.set_variable(output_variable, res.captured_stdout);
        }
        if (!error_variable.empty()) {
            interp.set_variable(error_variable, res.captured_stderr);
        }

        if (!result_variable.empty()) {
            interp.set_variable(result_variable, res.exit_codes.empty() ? "-1" : std::to_string(res.exit_codes.back()));
        }

        if (!results_variable.empty()) {
            std::string results;
            for (size_t i = 0; i < res.exit_codes.size(); ++i) {
                if (i > 0) results += ";";
                results += std::to_string(res.exit_codes[i]);
            }
            interp.set_variable(results_variable, results);
        }

        if (command_error_is_fatal) {
            for (int code : res.exit_codes) {
                if (code != 0) {
                    interp.set_fatal_error("execute_process failed with exit code " + std::to_string(code));
                    return;
                }
            }
        }
    });
}

} // namespace dmake
