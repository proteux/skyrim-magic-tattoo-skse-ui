// Magic Tattoos UI
// Copyright (C) 2026 Jack Whitehouse (proteux)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "Version.h"
#include "game/MtfBridge.h"
#include "game/OverlayVisibility.h"
#include "io/UiSettings.h"
#include "render/DdsCache.h"
#include "ui/App.h"

namespace {
    void InitLogging() {
        auto dir = SKSE::log::log_directory();
        if (!dir) {
            return;
        }
        try {
            auto path = *dir / "MagicTattoosUI.log";
            auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
            auto log = std::make_shared<spdlog::logger>("global", std::move(sink));
            log->set_level(spdlog::level::info);
            log->flush_on(spdlog::level::info);
            log->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
            spdlog::set_default_logger(std::move(log));
        } catch (...) {
            // A Documents path the ANSI code page can't hold: run without a log rather than not at all.
        }
    }

    void __stdcall RenderSection() { mtfui::ui::App::Get().DrawSection(); }
    void __stdcall RenderWindow() { mtfui::ui::App::Get().DrawWindow(); }

    // The framework repeats an event many times in a row, from several threads. Only a change is acted on, and on
    // the game thread, where the menu also renders.
    void __stdcall OnMenuEvent(SKSEMenuFramework::Model::EventType type) {
        static std::atomic<int> last{ -1 };
        if (type != SKSEMenuFramework::Model::kOpenMenu && type != SKSEMenuFramework::Model::kCloseMenu) {
            return;
        }
        if (last.exchange(type) == type) {
            return;
        }
        mtfui::game::MtfBridge::RunOnGame([type] {
            auto& app = mtfui::ui::App::Get();
            if (type == SKSEMenuFramework::Model::kOpenMenu) {
                app.OnMenuOpen();
            } else {
                app.OnMenuClose();
            }
        });
    }

    void RegisterMenu() {
        if (!SKSEMenuFramework::IsInstalled()) {
            logger::error("SKSE Menu Framework is not installed; the Magic Tattoos menu is unavailable");
            return;
        }
        // The window's pause flag is fixed at registration, before App::Init has run.
        mtfui::io::UiSettings early;
        early.Load(mtfui::ui::GameDataDir() / "SKSE" / "Plugins" / "MagicTattoosUI" / "settings.json");

        SKSEMenuFramework::SetSection("Magic Tattoos");
        SKSEMenuFramework::AddSectionItem("Editor", RenderSection);
        auto* window = SKSEMenuFramework::AddWindow(RenderWindow, early.pauseGame);
        mtfui::ui::App::Get().SetWindow(window);
        SKSEMenuFramework::AddEvent(OnMenuEvent, 0.0f);
        logger::info("Registered with SKSE Menu Framework {}", SKSEMenuFramework::GetMenuFrameworkVersion());
    }

    void LogEnvironment() {
        const auto runtime = REL::Module::get().version();
        logger::info("Game runtime {}", runtime.string());
        auto* data = RE::TESDataHandler::GetSingleton();
        const bool mtf = data && data->LookupModByName(mtfui::game::kMtfPlugin);
        logger::info("{}: {}", mtfui::game::kMtfPlugin, mtf ? "loaded" : "not loaded");
        const bool papyrusUtil = GetModuleHandleW(L"PapyrusUtil.dll") != nullptr;
        logger::info("PapyrusUtil.dll: {}", papyrusUtil ? "loaded" : "not found");
    }

    void OnMessage(SKSE::MessagingInterface::Message* msg) {
        switch (msg->type) {
        case SKSE::MessagingInterface::kPostLoad:
            RegisterMenu();
            break;
        case SKSE::MessagingInterface::kDataLoaded: {
            LogEnvironment();
            mtfui::game::MtfBridge::Get().OnDataLoaded();
            auto* device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::Renderer::GetDevice());
            mtfui::render::DdsCache::Get().Init(device);
            try {
                mtfui::ui::App::Get().Init();
            } catch (const std::exception& e) {
                logger::critical("Magic Tattoos UI failed to start: {}", e.what());
            }
            break;
        }
        case SKSE::MessagingInterface::kPostLoadGame:
            // data is a bool: false when the load failed, in which case the old world is still there.
            if (msg->data == nullptr) {
                logger::warn("Save failed to load; leaving state as it was");
                break;
            }
            mtfui::ui::App::Get().OnGameLoaded();
            // The body is rebuilt on load, and MTF repaints the player once its tick resumes.
            mtfui::game::WatchOverlaysFor(std::chrono::seconds(15));
            break;
        case SKSE::MessagingInterface::kNewGame:
            mtfui::ui::App::Get().OnGameLoaded();
            mtfui::game::WatchOverlaysFor(std::chrono::seconds(15));
            break;
        case SKSE::MessagingInterface::kSaveGame:
            mtfui::ui::App::Get().OnSaving();
            break;
        default:
            break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    InitLogging();
    logger::info("Magic Tattoos UI {} starting", mtfui::kVersion);
    SKSE::Init(skse);
    if (!SKSE::GetMessagingInterface()->RegisterListener(OnMessage)) {
        logger::error("Could not register for SKSE messages");
        return false;
    }
    return true;
}
