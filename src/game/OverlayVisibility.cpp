#include "game/OverlayVisibility.h"

#include "io/UiSettings.h"

#include <thread>

namespace mtfui::game {
    namespace {
        bool IsOverlaySlot(std::string name) {
            name = io::ToLower(name);
            return name.find("[ovl") != std::string::npos || name.find("[sovl") != std::string::npos;
        }

        bool HoldsTexture(RE::BSGeometry* geom) {
            auto* shader = geom->GetGeometryRuntimeData().shaderProperty.get();
            auto* material = shader ? static_cast<RE::BSLightingShaderMaterialBase*>(shader->material) : nullptr;
            if (!material || !material->textureSet) {
                return false;
            }
            const char* path = material->textureSet->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse);
            const std::string tex = io::ToLower(path ? path : "");
            return !tex.empty() && !tex.ends_with("default.dds");
        }

        constexpr auto kCheckEvery = std::chrono::milliseconds(500);
    }

    // MTF draws from Papyrus at its own pace and its tick event only reaches Papyrus listeners, so the player's
    // slots are checked on a schedule instead. A check walks the player's 3D once on the main thread.
    void WatchOverlayVisibility() {
        std::thread([] {
            for (;;) {
                std::this_thread::sleep_for(kCheckEvery);
                if (auto* tasks = SKSE::GetTaskInterface()) {
                    tasks->AddTask([] { ShowDrawnOverlays(RE::PlayerCharacter::GetSingleton()); });
                }
            }
        }).detach();
    }

    int ShowDrawnOverlays(RE::Actor* actor) {
        auto* root = actor ? actor->Get3D(false) : nullptr;
        if (!root) {
            return 0;
        }
        int shown = 0;
        RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* geom) {
            if (geom->GetAppCulled() && IsOverlaySlot(geom->name.c_str()) && HoldsTexture(geom)) {
                geom->SetAppCulled(false);
                ++shown;
            }
            return RE::BSVisit::BSVisitControl::kContinue;
        });
        if (shown > 0) {
            logger::info("Showed {} overlay slot(s) RaceMenu had hidden on {:08X}", shown, actor->GetFormID());
        }
        return shown;
    }
}
