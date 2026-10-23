#include "game/Camera.h"

namespace mtfui::game {
    void SetVanityCameraHeld(bool hold) {
        static std::optional<float> saved;
        constexpr auto kName = "fAutoVanityModeDelay:Camera";
        auto* setting = RE::INISettingCollection::GetSingleton()->GetSetting(kName);
        if (!setting) {
            setting = RE::INIPrefSettingCollection::GetSingleton()->GetSetting(kName);
        }
        if (!setting) {
            logger::warn("{} not found; the idle camera may orbit while editing", kName);
            return;
        }
        if (hold && !saved) {
            saved = setting->GetFloat();
            setting->SetFloat(1.0e9f);
        } else if (!hold && saved) {
            setting->SetFloat(*saved);
            saved.reset();
        }
    }
}
