#include "core/tsv.hpp"

namespace imbk {

std::string tsvEscape(std::string_view s) {
    std::string out;
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '\t') out += "\\t";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

std::string tsvUnescape(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 == s.size()) {
            out += s[i];
            continue;
        }
        const char n = s[++i];
        out += n == 'n' ? '\n' : n == 't' ? '\t' : n;
    }
    return out;
}

std::map<std::string, std::string> parseTsv(std::string_view content, std::vector<std::string>& errors) {
    std::map<std::string, std::string> out;
    if (content.starts_with("\xEF\xBB\xBF")) content.remove_prefix(3); // UTF-8 BOM from Windows editors
    std::size_t lineNo = 0;
    while (!content.empty()) {
        ++lineNo;
        const auto nl = content.find('\n');
        auto line = content.substr(0, nl);
        content.remove_prefix(nl == std::string_view::npos ? content.size() : nl + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line.front() == '#') continue;
        const auto tab = line.find('\t');
        if (tab == std::string_view::npos) {
            errors.push_back("line " + std::to_string(lineNo) + ": missing tab");
            continue;
        }
        const std::string id(line.substr(0, tab));
        if (!out.emplace(id, tsvUnescape(line.substr(tab + 1))).second)
            errors.push_back("line " + std::to_string(lineNo) + ": duplicate id " + id);
    }
    return out;
}

} // namespace imbk
