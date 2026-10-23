cmake_minimum_required(VERSION 3.21)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" MTFUI_VERSION)
string(STRIP "${MTFUI_VERSION}" MTFUI_VERSION)

project(MagicTattoosUI VERSION ${MTFUI_VERSION} DESCRIPTION "Magic Tattoos Framework configuration UI" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)

find_package(nlohmann_json CONFIG REQUIRED)

configure_file(src/Version.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/Version.h" @ONLY)

# Game-independent model and IO, shared by the plugin and the offline tests.
add_library(mtfui_core STATIC
    src/io/StyledJson.cpp
    src/io/JsonStore.cpp
    src/io/DdsHeader.cpp
    src/io/UiSettings.cpp
    src/model/Preset.cpp
    src/model/PluginCatalog.cpp
    src/model/VisualPacks.cpp
    src/model/Waveform.cpp
    src/model/Colors.cpp
    src/model/Describe.cpp
    src/model/Lint.cpp
    src/model/Catalogs.cpp
    src/model/Usage.cpp
    src/model/Text.cpp
)
target_include_directories(mtfui_core PUBLIC src "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(mtfui_core PUBLIC nlohmann_json::nlohmann_json)

find_package(CommonLibSSE CONFIG REQUIRED)
find_package(directxtk CONFIG REQUIRED)
find_package(directxtex CONFIG REQUIRED)

add_commonlibsse_plugin(${PROJECT_NAME}
    NAME "MagicTattoosUI"
    AUTHOR "white"
    USE_ADDRESS_LIBRARY
    SOURCES
        src/main.cpp
        src/game/Papyrus.cpp
        src/game/MtfBridge.cpp
        src/game/Targets.cpp
        src/game/LivePreview.cpp
        src/game/Camera.cpp
        src/render/DdsCache.cpp
        src/render/Preview2D.cpp
        src/ui/App.cpp
        src/ui/AppEditor.cpp
        src/ui/AppLayout.cpp
        src/ui/Widgets.cpp
        src/ui/PresetsTab.cpp
        src/ui/Inspector.cpp
        src/ui/TattoosTab.cpp
        src/ui/GeneralTab.cpp
        src/ui/PluginsTab.cpp
        src/ui/TargetBar.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE src include "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(${PROJECT_NAME} PRIVATE mtfui_core Microsoft::DirectXTK Microsoft::DirectXTex d3d11)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/pch.h)

if(BUILD_TESTING)
    add_executable(mtfui_tests tests/main.cpp)
    target_link_libraries(mtfui_tests PRIVATE mtfui_core)
    add_test(NAME mtfui_tests COMMAND mtfui_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
endif()
