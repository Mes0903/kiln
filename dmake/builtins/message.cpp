#include "registry.hpp"
#include "../interperter.hpp"
#include "../command_parser.hpp"
#include "../genex_evaluator.hpp"
#include "../genex_parser.hpp"
#include <sstream>
#include <algorithm>
#include <stack>
#include <iostream>

namespace dmake {

namespace {

// Message log levels (higher number = more verbose)
enum class LogLevel {
    ERROR = 0,      // FATAL_ERROR, SEND_ERROR
    WARNING = 1,    // WARNING, AUTHOR_WARNING
    NOTICE = 2,     // NOTICE, (none)
    STATUS = 3,     // STATUS
    VERBOSE = 4,    // VERBOSE
    DEBUG = 5,      // DEBUG
    TRACE = 6       // TRACE
};

LogLevel parse_log_level(const std::string& level_str) {
    std::string upper = level_str;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    if (upper == "ERROR") return LogLevel::ERROR;
    if (upper == "WARNING") return LogLevel::WARNING;
    if (upper == "NOTICE") return LogLevel::NOTICE;
    if (upper == "STATUS") return LogLevel::STATUS;
    if (upper == "VERBOSE") return LogLevel::VERBOSE;
    if (upper == "DEBUG") return LogLevel::DEBUG;
    if (upper == "TRACE") return LogLevel::TRACE;
    return LogLevel::STATUS; // Default
}

LogLevel get_message_log_level(Interpreter& interp) {
    std::string level = interp.get_variable("CMAKE_MESSAGE_LOG_LEVEL");
    if (level.empty()) {
        return LogLevel::STATUS; // Default level
    }
    return parse_log_level(level);
}

bool should_show_message(LogLevel msg_level, LogLevel threshold) {
    return static_cast<int>(msg_level) <= static_cast<int>(threshold);
}

} // anonymous namespace

void register_message_builtins(Interpreter& interp) {
    interp.add_builtin("message", [](Interpreter& interp, const std::vector<std::string>& args) {
        if (args.empty()) {
            interp.set_fatal_error("message() requires at least one argument");
            return;
        }

        std::string first_arg = args[0];
        std::string mode_upper = first_arg;
        std::transform(mode_upper.begin(), mode_upper.end(), mode_upper.begin(), ::toupper);

        std::string mode;
        LogLevel log_level = LogLevel::NOTICE;
        bool mode_found = true;
        bool is_check_command = false;

        // Parse mode
        if (mode_upper == "STATUS") {
            mode = "STATUS";
            log_level = LogLevel::STATUS;
        } else if (mode_upper == "VERBOSE") {
            mode = "VERBOSE";
            log_level = LogLevel::VERBOSE;
        } else if (mode_upper == "DEBUG") {
            mode = "DEBUG";
            log_level = LogLevel::DEBUG;
        } else if (mode_upper == "TRACE") {
            mode = "TRACE";
            log_level = LogLevel::TRACE;
        } else if (mode_upper == "WARNING") {
            mode = "WARNING";
            log_level = LogLevel::WARNING;
        } else if (mode_upper == "AUTHOR_WARNING") {
            mode = "AUTHOR_WARNING";
            log_level = LogLevel::WARNING;
        } else if (mode_upper == "DEPRECATION") {
            // Check CMAKE_ERROR_DEPRECATED and CMAKE_WARN_DEPRECATED
            std::string error_deprecated = interp.get_variable("CMAKE_ERROR_DEPRECATED");
            std::string warn_deprecated = interp.get_variable("CMAKE_WARN_DEPRECATED");

            if (error_deprecated == "TRUE" || error_deprecated == "ON" || error_deprecated == "1") {
                mode = "DEPRECATION_ERROR";
                log_level = LogLevel::ERROR;
            } else if (warn_deprecated == "TRUE" || warn_deprecated == "ON" || warn_deprecated == "1") {
                mode = "DEPRECATION";
                log_level = LogLevel::WARNING;
            } else {
                // Silent - don't print anything
                return;
            }
        } else if (mode_upper == "NOTICE") {
            mode = "NOTICE";
            log_level = LogLevel::NOTICE;
        } else if (mode_upper == "SEND_ERROR") {
            mode = "SEND_ERROR";
            log_level = LogLevel::ERROR;
        } else if (mode_upper == "FATAL_ERROR") {
            mode = "FATAL_ERROR";
            log_level = LogLevel::ERROR;
        } else if (mode_upper == "CHECK_START") {
            mode = "CHECK_START";
            log_level = LogLevel::STATUS;
            is_check_command = true;
        } else if (mode_upper == "CHECK_PASS") {
            mode = "CHECK_PASS";
            log_level = LogLevel::STATUS;
            is_check_command = true;
        } else if (mode_upper == "CHECK_FAIL") {
            mode = "CHECK_FAIL";
            log_level = LogLevel::STATUS;
            is_check_command = true;
        } else if (mode_upper == "CONFIGURE_LOG") {
            // CONFIGURE_LOG - for now, just ignore (CMake 3.26+ feature for detailed logging)
            // This would write to a configure log file
            return;
        } else {
            mode_found = false;
            mode = "NOTICE";
            log_level = LogLevel::NOTICE;
        }

        // Extract message content
        std::vector<std::string> message_args;
        if (mode_found) {
            message_args.assign(args.begin() + 1, args.end());
        } else {
            message_args = args;
        }

        std::ostringstream oss;
        for (size_t i = 0; i < message_args.size(); i++) {
            oss << message_args[i];
        }
        std::string content = oss.str();

        // Handle FATAL_ERROR specially
        if (mode == "FATAL_ERROR") {
            interp.set_fatal_error(content);
            return;
        }

        // Handle SEND_ERROR - accumulate error but continue
        if (mode == "SEND_ERROR") {
            interp.print_message("SEND_ERROR", content, true);
            interp.accumulate_error(content); // Mark that generation should be skipped
            return;
        }

        // Check log level filtering (except for errors and warnings)
        LogLevel threshold = get_message_log_level(interp);
        if (!should_show_message(log_level, threshold)) {
            return; // Filtered out
        }

        // Handle CHECK commands
        if (is_check_command) {
            if (mode == "CHECK_START") {
                interp.check_start(content);
            } else if (mode == "CHECK_PASS") {
                interp.check_pass(content);
            } else if (mode == "CHECK_FAIL") {
                interp.check_fail(content);
            }
            return;
        }

        // Normal message output
        bool is_error = (mode == "WARNING" || mode == "AUTHOR_WARNING" ||
                        mode == "DEPRECATION" || mode == "DEPRECATION_ERROR");

        interp.print_message(mode, content, is_error);
    });

    // Debug builtin to evaluate genex expressions
    // Usage: dmake_eval_genex(<expression> [LANGUAGE <lang>] [DEFERRED])
    interp.add_builtin("dmake_eval_genex", [](Interpreter& interp, const std::vector<std::string>& args) {
        if (args.empty()) {
            interp.set_fatal_error("dmake_eval_genex() requires at least one argument");
            return;
        }

        std::string expr = args[0];
        std::optional<Language> compile_lang;
        bool allow_deferred = false;

        // Parse options
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "LANGUAGE" && i + 1 < args.size()) {
                std::string lang = args[++i];
                if (lang == "C") compile_lang = Language::C;
                else if (lang == "CXX") compile_lang = Language::CXX;
                else if (lang == "CUDA") compile_lang = Language::CUDA;
            } else if (args[i] == "DEFERRED") {
                allow_deferred = true;
            }
        }

        // Set up context
        GenexEvaluationContext ctx;
        ctx.build_type = interp.get_variable("CMAKE_BUILD_TYPE");
        ctx.system_name = interp.get_variable("CMAKE_SYSTEM_NAME");
        ctx.cxx_compiler_id = interp.get_variable("CMAKE_CXX_COMPILER_ID");
        ctx.c_compiler_id = interp.get_variable("CMAKE_C_COMPILER_ID");
        ctx.all_targets = &interp.get_targets();
        ctx.phase = GenexEvaluationContext::Phase::BUILD;
        ctx.allow_deferred_compile_language = allow_deferred;
        if (compile_lang) {
            ctx.compile_language = *compile_lang;
        }

        GenexEvaluator evaluator(ctx);

        std::cerr << "[dmake_eval_genex] Input: " << expr << "\n";
        std::cerr << "[dmake_eval_genex] Context:\n";
        std::cerr << "  build_type: " << ctx.build_type << "\n";
        std::cerr << "  cxx_compiler_id: " << ctx.cxx_compiler_id << "\n";
        std::cerr << "  c_compiler_id: " << ctx.c_compiler_id << "\n";
        std::cerr << "  compile_language: " << (compile_lang ? (compile_lang == Language::CXX ? "CXX" : compile_lang == Language::C ? "C" : "CUDA") : "(none)") << "\n";
        std::cerr << "  allow_deferred: " << (allow_deferred ? "true" : "false") << "\n";

        auto result = evaluator.evaluate(expr);
        if (result) {
            std::cerr << "[dmake_eval_genex] Result: '" << *result << "'\n";
        } else {
            std::cerr << "[dmake_eval_genex] Error: " << result.error() << "\n";
        }
    });
}

} // namespace dmake
