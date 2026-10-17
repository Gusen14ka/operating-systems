#include "Config.h"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>

namespace {

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r");
    if (begin == std::string::npos) return "";
    const auto end = s.find_last_not_of(" \t\r");
    return s.substr(begin, end - begin + 1);
}

// "xterm -e" -> {"xterm", "-e"}
std::vector<std::string> splitWords(const std::string& s) {
    std::istringstream in(s);
    std::vector<std::string> words;
    for (std::string w; in >> w;) words.push_back(w);
    return words;
}

// Начинается ли строка с ключа как с отдельного слова:
// "terminal = x" и "terminal=x" — да, "terminals" — нет.
bool startsWithKey(const std::string& line, const std::string& key) {
    if (line.compare(0, key.size(), key) != 0) return false;
    if (line.size() == key.size()) return true;
    const char next = line[key.size()];
    return next == ' ' || next == '\t' || next == '=';
}

}  // namespace

Config parseConfig(std::istream& in) {
    Config cfg;
    std::string raw;
    int lineNo = 0;

    while (std::getline(in, raw)) {
        ++lineNo;
        const std::string line = trim(raw);
        const std::string where = "строка " + std::to_string(lineNo) + ": ";

        if (line.empty() || line[0] == '#') {
            continue;
        }

        if (startsWithKey(line, "add_event")) {
            std::string error;
            if (auto ev = parseEvent(line, error)) {
                cfg.events.push_back(std::move(*ev));
            } else {
                cfg.warnings.push_back(where + error);
            }
            continue;
        }

        if (startsWithKey(line, "terminal")) {
            const auto eq = line.find('=');
            const auto words = (eq == std::string::npos) ? std::vector<std::string>{}
                                                         : splitWords(line.substr(eq + 1));
            if (words.empty()) {
                cfg.warnings.push_back(where + "ожидается 'terminal = <команда> [флаги]'");
            } else {
                cfg.terminal = words;  // при повторе ключа побеждает последний
            }
            continue;
        }

        cfg.warnings.push_back(where + "неизвестная запись '" + line + "'");
    }
    return cfg;
}

std::optional<Config> loadConfig(const std::string& path, std::string& error) {
    std::ifstream file(path);
    if (!file) {
        // errno выставляет open() внутри ifstream; на Linux это надёжно
        error = "не удалось открыть " + path + ": " + std::strerror(errno);
        return std::nullopt;
    }
    return parseConfig(file);
}
