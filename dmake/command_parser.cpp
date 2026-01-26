#include "command_parser.hpp"

namespace dmake {

CommandParser::CommandParser(std::string cmd_name, std::string subcommand)
    : cmd_name_(std::move(cmd_name)), subcommand_(std::move(subcommand)) {}

void CommandParser::add_positional(std::string& var, std::string label, bool required) {
    positionals_.push_back({&var, std::move(label), required});
}

void CommandParser::add_flag(std::string keyword, bool& var) {
    keywords_[keyword] = {ArgType::FLAG, &var, keyword};
    var = false; // Initialize to false
}

void CommandParser::add_value(std::string keyword, std::string& var) {
    keywords_[keyword] = {ArgType::VALUE, &var, keyword};
}

void CommandParser::add_list(std::string keyword, std::vector<std::string>& var) {
    keywords_[keyword] = {ArgType::LIST, &var, keyword};
}

void CommandParser::add_multi_list(std::string keyword, std::vector<std::vector<std::string>>& var) {
    keywords_[keyword] = {ArgType::MULTI_LIST, &var, keyword};
}

void CommandParser::add_default_list(std::vector<std::string>& var) {
    default_list_ = &var;
}

std::expected<void, std::string> CommandParser::parse(std::span<const std::string> args) {
    size_t pos_idx = 0;
    KeywordInfo* active_kw = nullptr;

    for (const auto& arg : args) {
        auto it = keywords_.find(arg);
        if (it != keywords_.end()) {
            if (it->second.type == ArgType::FLAG) {
                *static_cast<bool*>(it->second.target) = true;
                // Flags don't change the active keyword state
            } else {
                active_kw = &it->second;
                if (active_kw->type == ArgType::MULTI_LIST) {
                    auto* vec = static_cast<std::vector<std::vector<std::string>>*>(active_kw->target);
                    vec->emplace_back();
                }
            }
            continue;
        }

        // Not a keyword
        if (pos_idx < positionals_.size() && !active_kw) {
            *positionals_[pos_idx].var = arg;
            positionals_[pos_idx].set = true;
            pos_idx++;
        } else if (active_kw) {
            if (active_kw->type == ArgType::VALUE) {
                auto* val = static_cast<std::string*>(active_kw->target);
                if (!val->empty()) {
                    return std::unexpected(cmd_name_ + "() keyword " + active_kw->keyword + " given multiple values");
                }
                *val = arg;
            } else if (active_kw->type == ArgType::LIST) {
                auto* vec = static_cast<std::vector<std::string>*>(active_kw->target);
                vec->push_back(arg);
            } else if (active_kw->type == ArgType::MULTI_LIST) {
                auto* vec = static_cast<std::vector<std::vector<std::string>>*>(active_kw->target);
                if (vec->empty()) vec->emplace_back(); // Should not happen with current logic but for safety
                vec->back().push_back(arg);
            }
        } else if (default_list_) {
            default_list_->push_back(arg);
        } else {
            return std::unexpected(cmd_name_ + "() given unknown argument: " + arg);
        }
    }

    // Validation
    for (const auto& pos : positionals_) {
        if (pos.required && !pos.set) {
            return std::unexpected(cmd_name_ + "() missing required positional argument: " + pos.label);
        }
    }

    return {};
}

std::string CommandParser::get_syntax() const {
    std::string syntax = cmd_name_ + "(";
    bool first = true;

    if (!subcommand_.empty()) {
        syntax += subcommand_;
        first = false;
    }

    for (const auto& pos : positionals_) {
        if (!first) syntax += " ";
        if (!pos.required) syntax += "[";
        syntax += "<" + pos.label + ">";
        if (!pos.required) syntax += "]";
        first = false;
    }

    for (const auto& [kw, info] : keywords_) {
        if (!first) syntax += " ";
        syntax += "[" + kw;
        if (info.type == ArgType::VALUE) syntax += " <val>";
        else if (info.type == ArgType::LIST || info.type == ArgType::MULTI_LIST) syntax += " <args>...";
        syntax += "]";
        first = false;
    }

    if (default_list_) {
        if (!first) syntax += " ";
        syntax += "[<args>...]";
    }

    syntax += ")";
    return syntax;
}

} // namespace dmake