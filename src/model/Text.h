#pragma once

#include <functional>
#include <string>

namespace mtfui::model {
    // True when every whitespace-separated word of needle appears in haystack, ignoring case.
    bool MatchesSearch(const std::string& haystack, const std::string& needle);

    // Turns a catalog slider format such as "{0}% aroused" into a printf format with exactly one %d.
    std::string SliderPrintfFormat(const std::string& catalogFormat);

    // base, or base_2, base_3... (kept within maxLength) until taken returns false.
    std::string NextFreeName(const std::string& base, const std::function<bool(const std::string&)>& taken, std::size_t maxLength = 32);
}
