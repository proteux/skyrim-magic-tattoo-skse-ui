#include "io/Translations.h"

#include <algorithm>
#include <cstdint>

namespace mtfui::io {
    namespace {
        void AppendUtf8(std::string& out, std::uint32_t cp) {
            if (cp < 0x80) {
                out += static_cast<char>(cp);
            } else if (cp < 0x800) {
                out += static_cast<char>(0xC0 | (cp >> 6));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                out += static_cast<char>(0xE0 | (cp >> 12));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
                out += static_cast<char>(0xF0 | (cp >> 18));
                out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
        }

        std::string Utf16LeToUtf8(std::string_view bytes) {
            std::string out;
            out.reserve(bytes.size() / 2);
            auto unit = [&](std::size_t i) {
                return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i]) | (static_cast<unsigned char>(bytes[i + 1]) << 8));
            };
            for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
                std::uint32_t cp = unit(i);
                if (cp >= 0xD800 && cp < 0xDC00 && i + 3 < bytes.size()) {
                    const auto low = unit(i + 2);
                    if (low >= 0xDC00 && low < 0xE000) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        i += 2;
                    }
                }
                AppendUtf8(out, cp);
            }
            return out;
        }

        // Decodes one UTF-8 sequence at s[i] and advances i past it. Malformed bytes decode as themselves.
        char32_t NextCodepoint(std::string_view s, std::size_t& i) {
            const auto c = static_cast<unsigned char>(s[i++]);
            const int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
            if (extra == 0 || i + extra > s.size()) {
                return c;
            }
            char32_t cp = c & (0x3F >> extra);
            for (int k = 0; k < extra; ++k) {
                cp = (cp << 6) | (static_cast<unsigned char>(s[i++]) & 0x3F);
            }
            return cp;
        }

        std::string Unescape(std::string_view s) {
            std::string out;
            out.reserve(s.size());
            for (std::size_t i = 0; i < s.size(); ++i) {
                if (s[i] != '\\' || i + 1 == s.size()) {
                    out += s[i];
                    continue;
                }
                switch (s[++i]) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case '\\': out += '\\'; break;
                    default:
                        out += '\\';
                        out += s[i];
                        break;
                }
            }
            return out;
        }
    }

    std::map<std::string, std::string, std::less<>> ParseTranslations(std::string_view bytes) {
        std::string text;
        if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF && static_cast<unsigned char>(bytes[1]) == 0xFE) {
            text = Utf16LeToUtf8(bytes.substr(2));
        } else {
            if (bytes.size() >= 3 && bytes.substr(0, 3) == "\xEF\xBB\xBF") {
                bytes.remove_prefix(3);
            }
            text = bytes;
        }
        std::map<std::string, std::string, std::less<>> out;
        std::string_view rest = text;
        while (!rest.empty()) {
            const auto eol = rest.find('\n');
            std::string_view line = rest.substr(0, eol);
            rest = eol == std::string_view::npos ? std::string_view{} : rest.substr(eol + 1);
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }
            const auto tab = line.find('\t');
            if (line.empty() || line.front() != '$' || tab == std::string_view::npos) {
                continue;
            }
            auto key = line.substr(0, tab);
            while (!key.empty() && key.back() == ' ') {
                key.remove_suffix(1);
            }
            out[std::string(key)] = Unescape(line.substr(tab + 1));
        }
        return out;
    }

    std::vector<std::string> PrintfSpecs(std::string_view text) {
        std::vector<std::string> out;
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] != '%') {
                continue;
            }
            if (i + 1 < text.size() && text[i + 1] == '%') {
                ++i;
                continue;
            }
            const auto end = text.find_first_of("diouxXeEfFgGaAcspn", i + 1);
            if (end == std::string_view::npos) {
                out.emplace_back(text.substr(i));
                break;
            }
            out.emplace_back(text.substr(i, end - i + 1));
            i = end;
        }
        return out;
    }

    Translations& Translations::Get() {
        static Translations instance;
        return instance;
    }

    void Translations::SetEnglish(std::string_view bytes) {
        english_ = ParseTranslations(bytes);
        active_.clear();
    }

    TranslationLoad Translations::Overlay(std::string_view bytes) {
        TranslationLoad result;
        for (auto& [key, text] : ParseTranslations(bytes)) {
            const auto en = english_.find(key);
            if (en == english_.end()) {
                result.unknown.push_back(key);
                continue;
            }
            // ImGui uses these as printf formats; a conversion English doesn't have would read garbage.
            if (key.ends_with("_Fmt") && PrintfSpecs(text) != PrintfSpecs(en->second)) {
                result.rejected.push_back(key);
                continue;
            }
            active_[key] = std::move(text);
            ++result.loaded;
        }
        return result;
    }

    const char* Translations::Text(const char* key) const {
        if (const auto it = active_.find(std::string_view(key)); it != active_.end()) {
            return it->second.c_str();
        }
        return English(key);
    }

    const char* Translations::English(const char* key) const {
        const auto it = english_.find(std::string_view(key));
        return it != english_.end() ? it->second.c_str() : key;
    }

    std::vector<char32_t> Translations::ActiveCodepoints() const {
        std::vector<char32_t> out;
        for (const auto& [key, text] : active_) {
            for (std::size_t i = 0; i < text.size();) {
                if (const auto cp = NextCodepoint(text, i); cp > 0x7E) {
                    out.push_back(cp);
                }
            }
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }

    void Translations::ReplaceInActive(char32_t from, std::string_view to) {
        for (auto& [key, text] : active_) {
            std::string out;
            out.reserve(text.size());
            for (std::size_t i = 0; i < text.size();) {
                const auto start = i;
                if (NextCodepoint(text, i) == from) {
                    out += to;
                } else {
                    out.append(text, start, i - start);
                }
            }
            text = std::move(out);
        }
    }

    std::string_view PlainStandIn(char32_t cp) {
        switch (cp) {
            case 0x00A0:  // no-break space
            case 0x202F:  // narrow no-break space
            case 0x2009:  // thin space
                return " ";
            case 0x2018:
            case 0x2019:
                return "'";
            case 0x201C:
            case 0x201D:
            case 0x201E:
                return "\"";
            case 0x2010:
            case 0x2011:
            case 0x2013:
            case 0x2014:
            case 0x2212:
                return "-";
            case 0x2026:
                return "...";
            default:
                return {};
        }
    }

    std::string FormatTranslated(const char* key, std::format_args args) {
        const auto& tr = Translations::Get();
        try {
            return std::vformat(tr.Text(key), args);
        } catch (const std::format_error&) {
        }
        try {
            return std::vformat(tr.English(key), args);
        } catch (const std::format_error&) {
            return tr.English(key);
        }
    }
}
