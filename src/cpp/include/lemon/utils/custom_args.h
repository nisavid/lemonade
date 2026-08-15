#pragma once

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace lemon {
namespace utils {

inline bool is_negative_numeric_value(const std::string& token) {
    if (token.size() < 2 || token[0] != '-' || token[1] == '-') {
        return false;
    }

    size_t pos = 1;
    bool saw_digit = false;

    while (pos < token.size() && std::isdigit(static_cast<unsigned char>(token[pos]))) {
        saw_digit = true;
        ++pos;
    }

    if (pos < token.size() && token[pos] == '.') {
        ++pos;
        while (pos < token.size() && std::isdigit(static_cast<unsigned char>(token[pos]))) {
            saw_digit = true;
            ++pos;
        }
    }

    if (!saw_digit) {
        return false;
    }

    if (pos < token.size() && (token[pos] == 'e' || token[pos] == 'E')) {
        ++pos;
        if (pos < token.size() && (token[pos] == '+' || token[pos] == '-')) {
            ++pos;
        }

        bool saw_exponent_digit = false;
        while (pos < token.size() && std::isdigit(static_cast<unsigned char>(token[pos]))) {
            saw_exponent_digit = true;
            ++pos;
        }
        if (!saw_exponent_digit) {
            return false;
        }
    }

    return pos == token.size();
}

inline bool is_custom_arg_flag(const std::string& token) {
    return !token.empty() && token[0] == '-' && !is_negative_numeric_value(token);
}

inline std::vector<std::string> parse_custom_args(const std::string& custom_args_str, bool keep_quotes = false) {
    std::vector<std::string> result;
    if (custom_args_str.empty()) {
        return result;
    }

    std::string current_arg;
    bool in_quotes = false;
    char quote_char = '\0';

    for (size_t i = 0; i < custom_args_str.size(); ++i) {
        char c = custom_args_str[i];
        if (in_quotes && c == '\\' && i + 1 < custom_args_str.size() &&
            (custom_args_str[i + 1] == quote_char || custom_args_str[i + 1] == '\\')) {
            current_arg += custom_args_str[++i];
        } else if (!in_quotes && (c == '"' || c == '\'')) {
            in_quotes = true;
            quote_char = c;
        } else if (in_quotes && c == quote_char) {
            in_quotes = false;
            if (keep_quotes) {
                current_arg = quote_char + current_arg + quote_char;
            }
            quote_char = '\0';
        } else if (!in_quotes && c == ' ') {
            if (!current_arg.empty()) {
                result.push_back(current_arg);
                current_arg.clear();
            }
        } else {
            current_arg += c;
        }
    }

    if (!current_arg.empty()) {
        result.push_back(current_arg);
    }

    return result;
}

using CustomArgsMap = std::map<std::string, std::vector<std::vector<std::string>>>;

inline CustomArgsMap build_custom_args_map(const std::vector<std::string>& tokens) {
    CustomArgsMap result;
    std::string last_flag;  // Track the most recently seen flag independently of map ordering

    for (const auto& token : tokens) {
        if (is_custom_arg_flag(token)) {
            // This is a flag; start a new entry
            result[token].push_back({});
            last_flag = token;
        } else if (!last_flag.empty()) {
            // Append to the most recently seen flag
            result[last_flag].back().push_back(token);
        }
    }

    return result;
}

inline std::string validate_custom_args(const std::string& custom_args_str, const std::set<std::string>& reserved_flags) {
    std::vector<std::string> custom_args = parse_custom_args(custom_args_str);

    for (const auto& arg : custom_args) {
        std::string flag = arg;
        size_t eq_pos = flag.find('=');
        if (eq_pos != std::string::npos) {
            flag = flag.substr(0, eq_pos);
        }

        if (is_custom_arg_flag(flag) && reserved_flags.find(flag) != reserved_flags.end()) {
            std::string reserved_list;
            for (const auto& reserved_flag : reserved_flags) {
                if (!reserved_list.empty()) {
                    reserved_list += ", ";
                }
                reserved_list += reserved_flag;
            }

            return "Argument '" + flag + "' is managed by Lemonade and cannot be overridden.\n"
                   "Reserved arguments: " + reserved_list;
        }
    }

    return "";
}

inline std::string quote_custom_arg_value(const std::string& value) {
    if (value.find_first_of(" \t\"\\'") == std::string::npos) {
        return value;
    }

    std::string escaped;
    escaped.reserve(value.size());
    for (char c : value) {
        if (c == '\\' || c == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(c);
    }
    return "\"" + escaped + "\"";
}

inline bool custom_args_has_flag(const std::vector<std::string>& tokens,
                                 const std::string& flag) {
    for (const auto& arg : tokens) {
        std::string token = arg;
        size_t eq_pos = token.find('=');
        if (eq_pos != std::string::npos) {
            token = token.substr(0, eq_pos);
        }
        if (token == flag) {
            return true;
        }
    }
    return false;
}

inline std::string map_to_args_string(const CustomArgsMap& m) {
    std::string result;
    bool first = true;
    for (const auto& [flag, occurrences] : m) {
        for (const auto& values : occurrences) {
            if (!first) result += " ";
            first = false;
            result += flag;
            for (const auto& v : values) {
                result += " " + quote_custom_arg_value(v);
            }
        }
    }
    return result;
}

// Given a flag like "--flag" or "--no-flag", return the negation key.
// "--no-<name>" ↔ "--<name>". Returns empty string if no negation exists.
inline std::string negate_flag(const std::string& flag) {
    if (flag.size() >= 5 && flag.compare(0, 5, "--no-") == 0) {
        return "--" + flag.substr(5);
    }
    if (flag.size() >= 3 && flag.compare(0, 2, "--") == 0) {
        return "--no-" + flag.substr(2);
    }
    return "";
}

inline CustomArgsMap merge_args_maps(
    const CustomArgsMap& target,
    const CustomArgsMap& incoming) {
    CustomArgsMap merged = target;

    // Remove binary-flag negations from incoming that conflict with target.
    // Only flags without arguments are considered binary flags.
    for (const auto& [flag, occurrences] : incoming) {
        bool is_binary = std::all_of(
            occurrences.begin(), occurrences.end(),
            [](const std::vector<std::string>& values) {
                return values.empty();
            });
        if (is_binary) {
            std::string neg = negate_flag(flag);
            if (!neg.empty() && merged.count(neg)) {
                // Target has the opposite binary flag — skip this incoming flag
                continue;
            }
        }
        if (!merged.count(flag)) {
            merged[flag] = occurrences;
        }
    }
    return merged;
}

} // namespace utils
} // namespace lemon
