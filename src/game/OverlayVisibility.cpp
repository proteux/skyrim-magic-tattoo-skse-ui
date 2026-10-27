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

        std::mutex g_mutex;
        std::chrono::steady_clock::time_point g_until;
        bool g_watching = false;
    }

    void WatchOverlaysFor(std::chrono::milliseconds window) {
        std::scoped_lock lock(g_mutex);
        g_until = std::max(g_until, std::chrono::steady_clock::now() + window);
        if (g_watching) {
            return;
        }
        g_watching = true;
        std::thread([] {
            for (;;) {
                std::this_thread::sleep_for(kCheckEvery);
                if (auto* tasks = SKSE::GetTaskInterface()) {
                    tasks->AddTask([] { ShowDrawnOverlays(RE::PlayerCharacter::GetSingleton()); });
                }
                std::scoped_lock lock(g_mutex);
                if (std::chrono::steady_clock::now() >= g_until) {
                    g_watching = false;
                    return;
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
