// bfbc - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <rex/cvar.h>
#include <rex/input/input_system.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/gpu_plugin.h>
#include <rex/ui/keybinds.h>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "bfbc_graphics.h"

class BfbcApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<BfbcApp>(new BfbcApp(ctx, "bfbc",
        PPCImageConfig));
  }

  // Remember where bfbc.toml lives (the same file the SDK settings editor and
  // the graphics menu save to), and fill in the launch arguments the game
  // needs when bfbc.exe is started without any (double-click in Explorer).
  //
  // Without --game_data_root the SDK aborts before the window exists and the
  // process disappears silently; without --gpu_plugin the guest boots with no
  // graphics system. Everything here only supplies defaults: values given on
  // the command line or in bfbc.toml still win, because the config is loaded
  // after this hook and command-line values outrank code-level ones.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    config_path_ = paths.config_path;

    if (paths.game_data_root.empty()) {
      // bfbc.exe lives in build\, so the game data is normally ..\game;
      // also accept a game\ folder copied next to the exe.
      const std::filesystem::path exe_dir = paths.config_path.parent_path();
      const std::filesystem::path candidates[] = {exe_dir / ".." / "game", exe_dir / "game"};
      for (const std::filesystem::path& candidate : candidates) {
        std::error_code ec;
        if (std::filesystem::is_directory(candidate, ec)) {
          paths.game_data_root = candidate.lexically_normal();
          break;
        }
      }
      if (!paths.game_data_root.empty()) {
        REXLOG_INFO("Game data root defaulted to {}", paths.game_data_root.string());
      }
    }

    // The Xenos plugin is what rexgpu-xenos.dll provides; an empty value means
    // no GPU emulation is loaded at all.
    if (rex::cvar::GetFlagByName("gpu_plugin").empty()) {
      rex::cvar::SetFlagByName("gpu_plugin", "xenos");
    }

    // The title opens GAME:\dvd-prb and GAME:\XenonGameSettings.cfg for write.
    // The VFS rejects that unless this is set, so enable it whenever the value
    // still comes from the compiled-in default.
    if (rex::cvar::GetFlagSource("allow_game_relative_writes") == rex::cvar::Source::kDefault) {
      rex::cvar::SetFlagByName("allow_game_relative_writes", "true");
    }

    // The SDK ships a keyboard/mouse -> Xbox 360 pad driver, but leaves it off.
    // Enable it (and mouse look for the right stick) unless bfbc.toml or the
    // command line already decided; keybinds and sensitivity stay tunable from
    // the F4 cvar editor.
    if (rex::cvar::GetFlagSource("mnk_mode") == rex::cvar::Source::kDefault) {
      if (!rex::cvar::SetFlagByName("mnk_mode", "true")) {
        REXLOG_WARN("Keyboard/mouse emulation could not be enabled");
      }
    }
    if (rex::cvar::GetFlagSource("mnk_mouse") == rex::cvar::Source::kDefault) {
      rex::cvar::SetFlagByName("mnk_mouse", "true");
    }
  }

  // The GPU plugin registers its cvars from its own DLL initialisers, and the
  // SDK loads that DLL during SetupPresentation -- after bfbc.toml has already
  // been read. Config values for plugin-owned flags therefore used to be
  // dropped, and the render target cache was built at 1x no matter what the F5
  // menu saved (the menu could set the cvars later, but the cache is created
  // once and never rebuilt).
  //
  // Loading the DLL here registers those flags first, so the saved values can
  // be pushed in before the plugin constructs the graphics system. The SDK
  // then skips its own load because config.graphics is already set.
  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (config.graphics || config.gpu_plugin.empty()) {
      return;
    }

    PreloadPluginDll(config.gpu_plugin);

    static constexpr const char* kStartupOnlyFlags[] = {"draw_resolution_scale_x",
                                                       "draw_resolution_scale_y",
                                                       "resolution_scale"};
    for (const char* flag : kStartupOnlyFlags) {
      const std::string value = ReadConfigValue(config_path_, flag);
      if (value.empty()) {
        continue;
      }
      if (rex::cvar::SetFlagByName(flag, value)) {
        REXLOG_INFO("Graphics: {} = {} (before GPU init)", flag, value);
      } else {
        REXLOG_WARN("Graphics: {} = {} rejected by the runtime", flag, value);
      }
    }

    config.graphics = rex::system::LoadGpuPlugin(config.gpu_plugin);
  }

  // Keyboard and mouse as an Xbox 360 pad. The SDK ships the driver, but only
  // the SDL (pad) driver is registered by default, so add it here once the
  // runtime and its input system exist. Bindings and sensitivity stay cvars:
  // keybind_* and mnk_sensitivity are editable from the F4 editor.
  void OnPostSetup() override {
    rex::Runtime* runtime = this->runtime();
    rex::ui::Window* game_window = window();
    if (!runtime || !game_window) {
      return;
    }
    auto* input = static_cast<rex::input::InputSystem*>(runtime->input_system());
    if (!input) {
      REXLOG_WARN("Input: no input system, keyboard/mouse not available");
      return;
    }
    input->AddDriver(std::make_unique<rex::input::mnk::MnkInputDriver>(game_window, 0));
    REXLOG_INFO("Input: keyboard/mouse driver registered");

    for (const std::string& name : rex::cvar::ListFlags()) {
      if (name.rfind("keybind_", 0) == 0 || name.rfind("mnk_", 0) == 0) {
        REXLOG_DEBUG("Input: {} = '{}'", name, rex::cvar::GetFlagByName(name));
      }
    }
  }

  // Every overlay (F3 stats, F4 cvars, F5 graphics, console) uses the SDK's
  // 13 px base font, which is unreadable on a 1440p display. ImGui 1.92 has a
  // single global text-scale factor, so raise it once here for all overlays.
  // The F5 menu also exposes a live slider (UI text size) on top of this.
  static constexpr float kOverlayFontScale = 1.4f;

  void OnConfigureStyle(ImGuiStyle& imgui_style, rex::ui::Style& ui_style) override {
    (void)ui_style;
    if (imgui_style.FontScaleMain < kOverlayFontScale) {
      imgui_style.FontScaleMain = kOverlayFontScale;
    }
  }

  // Add the curated graphics menu on top of the SDK overlays. ReXApp has
  // already registered F3 (debug), F4 (all cvars), F7 (achievements) and the
  // console on backtick by the time this runs.
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    (void)drawer;
    rex::ui::RegisterBind("bfbc_bind_graphics", "F5", "Toggle bfbc graphics settings",
                          [this] {
                            if (graphics_dialog_) {
                              graphics_dialog_.reset();
                              return;
                            }
                            graphics_dialog_ = std::make_unique<bfbc::GraphicsDialog>(
                                imgui_drawer(), config_path_,
                                [this]() -> std::pair<uint32_t, uint32_t> {
                                  if (!window()) return {0, 0};
                                  return {window()->GetActualPhysicalWidth(),
                                          window()->GetActualPhysicalHeight()};
                                },
                                [this] {
                                  // Deferred so the dialog is not destroyed
                                  // from inside its own OnDraw.
                                  app_context().CallInUIThread(
                                      [this] { graphics_dialog_.reset(); });
                                });
                          });
  }

  void OnShutdown() override {
    rex::ui::UnregisterBind("bfbc_bind_graphics");
    graphics_dialog_.reset();
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostSetup() override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;

 private:
  // Loads rexgpu-<name>.dll only to run its cvar registration. The SDK loads
  // the same module again (reference-counted) to create the graphics system.
  static void PreloadPluginDll(const std::string& name) {
#if defined(_WIN32)
    const std::wstring file = L"rexgpu-" + std::wstring(name.begin(), name.end()) + L".dll";
    if (::LoadLibraryW(file.c_str())) {
      REXLOG_DEBUG("Graphics: pre-loaded {} to register its cvars", name);
    } else {
      REXLOG_WARN("Graphics: could not pre-load {} (error {}); saved resolution scale may be "
                  "ignored",
                  name, static_cast<unsigned long>(::GetLastError()));
    }
#else
    (void)name;
#endif
  }

  // Minimal reader for the flat "key = value" config the SDK writes.
  static std::string ReadConfigValue(const std::filesystem::path& path, std::string_view key) {
    if (path.empty()) {
      return {};
    }
    std::ifstream in(path);
    if (!in) {
      return {};
    }
    std::string line;
    while (std::getline(in, line)) {
      const size_t eq = line.find('=');
      if (eq == std::string::npos) {
        continue;
      }
      std::string name = line.substr(0, eq);
      const size_t first = name.find_first_not_of(" \t\r");
      const size_t last = name.find_last_not_of(" \t\r");
      if (first == std::string::npos) {
        continue;
      }
      name = name.substr(first, last - first + 1);
      if (name != key) {
        continue;
      }
      std::string value = line.substr(eq + 1);
      const size_t vfirst = value.find_first_not_of(" \t\r\"");
      const size_t vlast = value.find_last_not_of(" \t\r\"");
      if (vfirst == std::string::npos) {
        return {};
      }
      return value.substr(vfirst, vlast - vfirst + 1);
    }
    return {};
  }

  std::filesystem::path config_path_;
  std::unique_ptr<bfbc::GraphicsDialog> graphics_dialog_;
};
