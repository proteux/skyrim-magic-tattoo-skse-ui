#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace mtfui::io {
    // Serializes like PapyrusUtil's JsonUtil (jsoncpp StyledStreamWriter, tab indent,
    // sorted keys, CRLF, no trailing newline), so presets saved by the UI and by MTF
    // produce identical files.
    std::string WriteStyled(const nlohmann::json& value);

    // Formats a double the way jsoncpp does ("%.17g", ".0" appended to integral values).
    std::string FormatDouble(double value);
}
