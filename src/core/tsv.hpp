#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace imbk {

// "<id>\t<text>" files: one record per line, with \\, \t and \n escaped inside the text.
std::string tsvEscape(std::string_view s);
std::string tsvUnescape(std::string_view s);

// Parses id -> text. Empty lines and lines starting with '#' are ignored; problems are appended to `errors`.
std::map<std::string, std::string> parseTsv(std::string_view content, std::vector<std::string>& errors);

} // namespace imbk
