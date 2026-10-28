#include "model/Text.h"

#include "io/UiSettings.h"

namespace mtfui::model {
    bool MatchesSearch(const std::string& haystack, const std::string& needle) {
        if (needle.empty()) {
            return true;
        }
        const auto h = io::ToLower(haystack);
        const auto n = io::ToLower(needle);
        std::size_t start = 0;
        while (start < n.size()) {
            auto end = n.find(' ', start);
            if (end == std::string::npos) {
                end = n.size();
            }
            if (end > start && h.find(n.substr(start, end - start)) == std::string::npos) {
                return false;
            }
            start = end + 1;
        }
        return true;
    }

    std::string SliderPrintfFormat(const std::string& catalogFormat) {
        if (catalogFormat.empty()) {
            return "%d";
        }
        // The placeholder is {0}, or a printf-style %d in catalogs written that way. Every other % is escaped, so
        // the result has exactly one conversion.
        const std::string token = catalogFormat.find("{0}") != std::string::npos ? "{0}" : "%d";
        std::string out;
        bool placed = false;
        for (std::size_t i = 0; i < catalogFormat.size(); ++i) {
            if (!placed && catalogFormat.compare(i, token.size(), token) == 0) {
                out += "%d";
                placed = true;
                i += token.size() - 1;
            } else if (catalogFormat[i] == '%') {
                out += "%%";
            } else {
                out += catalogFormat[i];
            }
        }
        return placed ? out : "%d " + out;
    }

    std::string NextFreeName(const std::string& base, const std::function<bool(const std::string&)>& taken, std::size_t maxLength) {
        std::string name = base.substr(0, maxLength);
        for (int i = 2; taken(name); ++i) {
            const std::string suffix = "_" + std::to_string(i);
            name = base.substr(0, maxLength - suffix.size()) + suffix;
        }
        return name;
    }
}
