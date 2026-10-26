#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace coop {

struct IniValue {
    std::string value;
    int line = 0;
};

using IniSection = std::map<std::string, IniValue>;  // lower-cased key -> value

struct IniFile {
    std::map<std::string, IniSection> sections;      // lower-cased section name
    std::vector<std::string> warnings;               // syntax problems, "line N: ..."
};

// Minimal INI reader shared by coop.ini and offsets.ini.
// Comments start a line with ';' or '#'; inline comments need " ;" so values
// like "#FF0000" survive.
IniFile parseIni(std::string_view text);

std::string_view trim(std::string_view s);
std::string toLower(std::string_view s);

}  // namespace coop
