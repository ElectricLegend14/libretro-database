#include "coop/ini.hpp"

#include <cctype>

namespace coop {

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

std::string toLower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

IniFile parseIni(std::string_view text) {
    IniFile ini;
    auto warn = [&](int line, const char* msg) {
        ini.warnings.push_back("line " + std::to_string(line) + ": " + msg);
    };

    std::string current;
    int lineNo = 0;
    while (!text.empty()) {
        ++lineNo;
        auto nl = text.find('\n');
        auto line = trim(text.substr(0, nl));
        text.remove_prefix(nl == std::string_view::npos ? text.size() : nl + 1);

        if (line.empty() || line.front() == ';' || line.front() == '#') continue;
        if (line.front() == '[') {
            if (line.back() != ']') {
                warn(lineNo, "malformed section header");
                continue;
            }
            current = toLower(trim(line.substr(1, line.size() - 2)));
            ini.sections[current];
            continue;
        }
        auto eq = line.find('=');
        if (eq == std::string_view::npos) {
            warn(lineNo, "expected 'key = value'");
            continue;
        }
        if (current.empty()) {
            warn(lineNo, "option outside of any [section]");
            continue;
        }
        auto value = trim(line.substr(eq + 1));
        if (auto c = value.find(" ;"); c != std::string_view::npos) value = trim(value.substr(0, c));
        ini.sections[current][toLower(trim(line.substr(0, eq)))] = {std::string(value), lineNo};
    }
    return ini;
}

}  // namespace coop
