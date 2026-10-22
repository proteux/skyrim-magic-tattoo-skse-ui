#include "Version.h"
#include "game/MtfBridge.h"
#include "io/UiSettings.h"
#include "render/DdsCache.h"
#include "ui/App.h"

namespace {
    void InitLogging() {
        auto dir = SKSE::log::log_directory();
        if (!dir) {
            return;
        }
        auto path = *dir / "MagicTattoosUI.log";
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
        auto log = std::make_shared<spdlog::logger>("global", std::move(sink));
        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::info);
        log->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        spdlog::set_default_logger(std::move(log));
    }

    void __stdcall RenderSection() { mtfui::ui::App::Get().DrawSection(); }
    void __stdcall RenderWindow() { mtfui::ui::App::Get().DrawWindow(); }

    void __stdcall OnMenuEvent(SKSEMenuFramework::Model::EventType type) {
        auto& app = mtfui::ui::App::Get();
        if (type == SKSEMenuFramework::Model::kOpenMenu) {
            app.OnMenuOpen();
        } else if (type == SKSEMenuFramework::Model::kCloseMenu) {
            app.OnMenuClose();
        }
    }

    void RegisterMenu() {
        if (!SKSEMenuFramework::IsInstalled()) {
            logger::error("SKSE Menu Framework is not installed; the Magic Tattoos menu is unavailable");
            return;
        }
        // The window's pause flag is fixed at registration, before App::Init has run.
        mtfui::io::UiSettings early;
        early.Load("Data/SKSE/Plugins/MagicTattoosUI/settings.json");

        SKSEMenuFramework::SetSection("Magic Tattoos");
        SKSEMenuFramework::AddSectionItem("Editor", RenderSection);
        auto* window = SKSEMenuFramework::AddWindow(RenderWindow, early.pauseGame);
        mtfui::ui::App::Get().SetWindow(window);
        SKSEMenuFramework::AddEvent(OnMenuEvent, 0.0f);
        logger::info("Registered with SKSE Menu Framework {}", SKSEMenuFramework::GetMenuFrameworkVersion());
    }

    void OnMessage(SKSE::MessagingInterface::Message* msg) {
        switch (msg->type) {
        case SKSE::MessagingInterface::kPostLoad:
            RegisterMenu();
            break;
        case SKSE::MessagingInterface::kDataLoaded: {
            mtfui::game::MtfBridge::Get().OnDataLoaded();
            auto* device = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::Renderer::GetDevice());
            mtfui::render::DdsCache::Get().Init(device);
            mtfui::ui::App::Get().Init();
            break;
        }
        case SKSE::MessagingInterface::kPostLoadGame:
        case SKSE::MessagingInterface::kNewGame:
            mtfui::ui::App::Get().OnGameLoaded();
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
