#pragma once

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include <Windows.h>
#include <DDSTextureLoader.h>
#include <d3d11.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// COM headers define `interface` as a macro. D3D and shell headers need it, so they come first.
#ifdef interface
#    undef interface
#endif
// Win32 maps GetObject to GetObjectW, which breaks RE::BSScript::Variable::GetObject.
#ifdef GetObject
#    undef GetObject
#endif

#include <nlohmann/json.hpp>
#include <spdlog/sinks/basic_file_sink.h>

#include "SKSEMenuFramework.h"

using namespace std::literals;

namespace logger = SKSE::log;
