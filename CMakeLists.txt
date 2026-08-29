cmake_minimum_required(VERSION 3.25)
project(LumenBox LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)          # 工具链 < VS17.7 时临时降为 20，并在本行注释
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
FetchContent_Declare(catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.7.1)
FetchContent_MakeAvailable(json catch2)

enable_testing()
add_executable(core_tests tests/sanity.cpp tests/test_config.cpp tests/test_rules.cpp
  tests/test_classifier.cpp tests/test_journal.cpp tests/test_organizer.cpp
  tests/test_drop_target.cpp tests/test_autostart.cpp
  src/core/config.cpp src/core/rules.cpp src/core/classifier.cpp
  src/core/journal.cpp src/core/organizer.cpp src/core/autostart.cpp
  src/ui/drop_target.cpp)
target_include_directories(core_tests PRIVATE src)
target_link_libraries(core_tests PRIVATE Catch2::Catch2WithMain shell32 ole32 nlohmann_json::nlohmann_json)
target_compile_definitions(core_tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(core_tests PRIVATE /utf-8)
add_executable(compo_probe tools/compo_probe.cpp)
target_link_libraries(compo_probe PRIVATE windowsapp)
target_compile_definitions(compo_probe PRIVATE UNICODE _UNICODE)
target_compile_options(compo_probe PRIVATE /utf-8)
add_executable(drag_probe tools/drag_probe.cpp)
target_link_libraries(drag_probe PRIVATE ole32 shell32)
target_compile_definitions(drag_probe PRIVATE UNICODE _UNICODE)
target_compile_options(drag_probe PRIVATE /utf-8)
add_test(NAME core COMMAND core_tests)

add_executable(lumenbox WIN32
  src/main.cpp
  src/core/config.cpp
  src/core/rules.cpp
  src/core/classifier.cpp
  src/core/journal.cpp
  src/core/organizer.cpp
  src/core/autostart.cpp
  src/ui/glass_window.cpp
  src/ui/card_visual.cpp
  src/ui/desktop_grid.cpp
  src/ui/icons.cpp
  src/ui/drop_target.cpp
  src/ui/rules_dialog.cpp
  src/ui/note_window.cpp
  src/ui/tray.cpp
  src/shell/desktop_layer.cpp
  src/anim/spring_demo.cpp)
target_include_directories(lumenbox PRIVATE src)
target_link_libraries(lumenbox PRIVATE d2d1 dwrite dcomp dwmapi shell32 ole32 windowsapp nlohmann_json::nlohmann_json)
target_compile_definitions(lumenbox PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(lumenbox PRIVATE /W4 /permissive- /utf-8)
target_link_options(lumenbox PRIVATE /ENTRY:wWinMainCRTStartup)
