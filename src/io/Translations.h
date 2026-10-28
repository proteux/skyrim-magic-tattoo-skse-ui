#pragma once

#include <format>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace mtfui::io {
    // UI text in Skyrim's Interface/Translations format: one "$KEY<TAB>text" per line, UTF-16 LE with a BOM (UTF-8 is
    // accepted too). "\n", "\t" and "\\" in the text are escapes. Lines that don't start with '$' are ignored.
    // Only keys ending in "_Fmt" may be passed to ImGui as a printf format; all other text goes through "%s".
    std::map<std::string, std::string, std::less<>> ParseTranslations(std::string_view bytes);

    // The printf conversions in text, in order ("%d", "%.1f"...), without "%%".
    std::vector<std::string> PrintfSpecs(std::string_view text);

    struct TranslationLoad {
        int loaded = 0;
        std::vector<std::string> rejected;  // "_Fmt" keys whose printf conversions differ from the English text
        std::vector<std::string> unknown;   // keys English doesn't have
    };

    class Translations {
    public:
        static Translations& Get();

        // English is the base every lookup falls back to.
        void SetEnglish(std::string_view bytes);
        // Layers another language over English. A string that would break a printf-style call is left in English.
        TranslationLoad Overlay(std::string_view bytes);

        // Translated text, else English, else the key itself. key must outlive the returned pointer.
        const char* Text(const char* key) const;
        const char* English(const char* key) const;
        const auto& EnglishTable() const { return english_; }

    private:
        std::map<std::string, std::string, std::less<>> english_;
        std::map<std::string, std::string, std::less<>> active_;
    };

    std::string FormatTranslated(const char* key, std::format_args args);
}

namespace mtfui {
    inline const char* T(const char* key) { return io::Translations::Get().Text(key); }

    // std::format with the translated text, which may number its placeholders ("{1} ... {0}") to reorder them. A
    // translation that doesn't format falls back to English.
    template <class... Args>
    std::string TF(const char* key, const Args&... args) {
        return io::FormatTranslated(key, std::make_format_args(args...));
    }
}
