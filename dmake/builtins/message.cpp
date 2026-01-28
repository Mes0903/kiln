#include "registry.hpp"
#include "../interperter.hpp"
#include "../command_parser.hpp"
#include <sstream>
#include <algorithm>
#include <stack>

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
}

} // namespace dmake
