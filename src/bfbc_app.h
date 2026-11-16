// bfbc - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <rex/cvar.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>
#include <rex/input/input_system.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/gpu_plugin.h>
#include <rex/ui/keybinds.h>

// FindWindowByName()/ImGuiWindow::Active: the F4 cvar editor and the F5 menu are
// plain ImGui windows owned by the SDK, so their visibility is how the cursor
// watcher below knows when mouse look must let go of the pointer.
#include <imgui_internal.h>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "bfbc_graphics.h"
#include "bfbc_mouse.h"

// Opt-in 60 fps simulation step (see build/NOTES_timestep.md).
//
// The engine advances exactly 1/30 s of simulated time per accepted frame, and
// every fixed-step user loads that duration from one float in the image
// (0x8200D7D8): sub_824BD5C8's stepper passes it as the step, sub_82269DB8
// settles with 50 of them, sub_82708DD0 uses it as the delta fallback, and
// sub_8260C928 multiplies a frame-count difference by it to obtain seconds.
// That is why raising the swap rate alone runs the game faster instead of
// smoother - one swap still carries 1/30 s of engine time.
//
// Measured result of the patch below (2026-11-07): halving this constant does
// shorten the engine's own pacing (fixed holds and the intro sequence end
// sooner), but it does not turn 60 swaps per second into 60 fps at 1x speed.
// The per-frame simulation delta therefore comes from somewhere else, and this
// flag stays as a diagnostic switch for that work; it is off by default.
REXCVAR_DEFINE_BOOL(bfbc_sim_step_60, false, "bfbc display",
                    "Halve the engine's 1/30 s frame-step constant in guest memory. "
                    "Diagnostic only: measured 2026-11-07 it changes engine pacing (fixed "
                    "holds and the intro get shorter) but does NOT give 60 fps at 1x speed "
                    "by itself - see build/NOTES_timestep.md")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(bfbc_sim_wait_ms, 30, "bfbc display",
                     "Wait timeout for the render thread in milliseconds. "
                     "Default 30 (30fps). Set to 16676 (~16.676ms) to target 60fps.")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

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

    // The input module registers keybind_* after the SDK has already read
    // bfbc.toml, so a saved binding never reached its cvar and every launch fell
    // back to the SDK defaults. Push the file's bindings here first.
    ApplySavedKeyBinds();

    // Keyboard/mouse layout: A=Space, B=C, X=R, Y=Q, LB=1, RB=2, Start="=",
    // Back="-", the triggers on the mouse buttons, L3=Shift, R3=Ctrl and the
    // d-pad on the arrow keys. The SDK already records LMB/RMB as virtual keys;
    // the middle/side buttons and the wheel are added by the bfbc mouse driver.
    // F5/F4 can rebind any of these at any time.
    ApplyDefaultBind("keybind_a", "Space");      // jump
    ApplyDefaultBind("keybind_b", "C");          // crouch
    ApplyDefaultBind("keybind_x", "R");          // reload / use
    ApplyDefaultBind("keybind_y", "Q");          // switch weapon
    ApplyDefaultBind("keybind_left_shoulder", "1");
    ApplyDefaultBind("keybind_right_shoulder", "2");
    ApplyDefaultBind("keybind_right_trigger", "LMB");  // fire
    ApplyDefaultBind("keybind_left_trigger", "RMB");   // aim
    ApplyDefaultBind("keybind_lstick_press", "Shift");     // sprint (L3)
    ApplyDefaultBind("keybind_rstick_press", "Control");   // melee (R3)
    ApplyDefaultBind("keybind_start", "Plus");   // =
    ApplyDefaultBind("keybind_back", "Minus");   // -
    ApplyDefaultBind("keybind_dpad_up", "Up");
    ApplyDefaultBind("keybind_dpad_down", "Down");
    ApplyDefaultBind("keybind_dpad_left", "Left");
    ApplyDefaultBind("keybind_dpad_right", "Right");
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
    // The SDK's default input system always registers its own keyboard/mouse
    // driver, and a second one cannot take over: MnkInputDriver hashes every
    // device to one constant handle and InputSystem::DriverForDevice() resolves
    // that handle to the first driver that reported it. The wrapping driver was
    // therefore registered but never polled, so bfbc_mouse_* did nothing.
    // Supplying the factory is the supported way to put ours in its place.
    config.input_factory = &bfbc::CreateShapedInputSystem;

    if (config.graphics || config.gpu_plugin.empty()) {
      return;
    }

    PreloadPluginDll(config.gpu_plugin);

    // The draw scale sizes the render target cache, and the lighting switches
    // decide how scaled draws and resolves are sampled. All of them are read
    // while the plugin builds that cache, so the saved values have to be in
    // place here rather than after the window exists.
    static constexpr const char* kStartupOnlyFlags[] = {
        "draw_resolution_scale_x",
        "draw_resolution_scale_y",
        "resolution_scale",
        "draw_resolution_scaled_texture_offsets",
        "resolve_resolution_scale_fill_half_pixel_offset",
        "readback_resolve_half_pixel_offset",
        // The plugin has both an old boolean d3d12_readback_resolve (default
        // false) and the real string enum the settings UI shows. Only this one
        // accepts none/fast/some/full.
        "readback_resolve",
        "depth_float24_convert_in_pixel_shader",
        "depth_float24_round",
        "depth_transfer_not_equal_test",
        "gpu_3d_to_2d_texture",
        // Which D3D12 render target emulation the plugin builds: "rtv" is host
        // render targets with fixed-function blending, "rov" does blending and
        // depth/stencil in the pixel shader against a 10 MB EDRAM emulation.
        // The host path has to approximate the 360's 10_10_10_2 formats, which
        // leaves too few alpha bits for smooth blending (smoke and other
        // translucent particles come out hard-edged and near-opaque), so the
        // path is chosen here, before the render target cache exists.
        "render_target_path_d3d12",
        // The ROV path translates the draw's blend/depth state into a pixel
        // shader, so each new state combination costs a pipeline compile. Doing
        // that on the render thread lands as multi-second hitches the first time
        // a new effect (smoke, fire, decals) shows up, so compile off-thread.
        "async_shader_compilation",
        "texture_cache_memory_limit_render_to_texture",
        "texture_cache_memory_limit_soft",
        "texture_cache_memory_limit_hard",
    };
    for (const char* flag : kStartupOnlyFlags) {
      std::string value = ReadConfigValue(config_path_, flag);
      if (value.empty()) {
        value = DefaultStartupValue(flag);
      }
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

  // Data patch applied once the XEX is mapped into guest memory and before the
  // module launches: this is the SDK's designated hook for that. Only the frame
  // duration constant is touched, and only when it still holds 1/30 - a build
  // that moved or already patched it is reported instead of being written over.
  void OnPostLoadXexImage() override {
    if (!REXCVAR_GET(bfbc_sim_step_60)) {
      return;
    }
    rex::Runtime* rt = this->runtime();
    rex::memory::Memory* guest_memory = rt ? rt->memory() : nullptr;
    if (guest_memory == nullptr) {
      REXLOG_WARN("Timestep: guest memory is unavailable, the 1/60 patch was skipped");
      return;
    }
    // New: patch the render thread wait timeout if the cvar is set to non-default
    // Patch the render thread wait timeout if the cvar is set to a non-default value
    uint32_t wait_timeout_ms = REXCVAR_GET(bfbc_sim_wait_ms);
    if (wait_timeout_ms != 30) {
      constexpr uint32_t kWaitAddress = 0x82176428;
      constexpr uint32_t kWaitSize = sizeof(uint32_t);
      uint8_t* wait_bytes = guest_memory->TranslateVirtual(kWaitAddress);
      if (wait_bytes == nullptr) {
        REXLOG_WARN("Wait: cannot translate {:#x}, skip patch", kWaitAddress);
      } else {
        // The value is stored as negative 100ns units: -300000 = -30ms, -166760 = -16.676ms
        // Convert ms to 100ns units: ms * 10000, then negate
        int32_t wait_100ns = -((int32_t)(wait_timeout_ms * 10000));
        uint8_t* wait_step = wait_bytes;
        std::memcpy(wait_step, &wait_100ns, kWaitSize);
        REXLOG_INFO("Wait: patched timeout {:#x} -> {}ms ({} * 10000 = {} 100ns, negated {})",
                    kWaitAddress, wait_timeout_ms, wait_timeout_ms, wait_timeout_ms * 10000, wait_100ns);
      }
    }

    // Big-endian float, so the bytes are written in guest order.
    static constexpr uint8_t kOneThirtieth[4] = {0x3D, 0x08, 0x88, 0x89};
    static constexpr uint8_t kOneSixtieth[4] = {0x3C, 0x88, 0x88, 0x89};
    constexpr uint32_t kFrameStepAddress = 0x8200D7D8;
    constexpr uint32_t kFrameStepSize = sizeof(kOneThirtieth);
    uint8_t* frame_step = guest_memory->TranslateVirtual(kFrameStepAddress);
    if (frame_step == nullptr) {
      REXLOG_WARN("Timestep: cannot translate {:#x}, the 1/60 patch was skipped",
                  kFrameStepAddress);
      return;
    }
    if (std::memcmp(frame_step, kOneThirtieth, kFrameStepSize) != 0) {
      REXLOG_WARN("Timestep: {:#x} holds {:02X}{:02X}{:02X}{:02X}, not 1/30 - left untouched",
                  kFrameStepAddress, frame_step[0], frame_step[1], frame_step[2], frame_step[3]);
      return;
    }

    // The image is mapped read-only, so the page has to be made writable through
    // the guest heap; a bare store faults and is reported as a guest access
    // violation.
    rex::memory::BaseHeap* heap = guest_memory->LookupHeap(kFrameStepAddress);
    if (heap == nullptr) {
      REXLOG_WARN("Timestep: no heap covers {:#x}, the 1/60 patch was skipped",
                  kFrameStepAddress);
      return;
    }
    uint32_t old_protect = 0;
    heap->QueryProtect(kFrameStepAddress, &old_protect);
    if (!heap->Protect(kFrameStepAddress, kFrameStepSize,
                       rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite)) {
      REXLOG_WARN("Timestep: {:#x} could not be made writable, the 1/60 patch was skipped",
                  kFrameStepAddress);
      return;
    }
    std::memcpy(frame_step, kOneSixtieth, kFrameStepSize);
    heap->Protect(kFrameStepAddress, kFrameStepSize, old_protect);
    REXLOG_INFO("Timestep: frame step 1/30 -> 1/60 at {:#x} (bfbc_sim_step_60)",
                kFrameStepAddress);
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
    if (!runtime->input_system()) {
      REXLOG_WARN("Input: no input system, keyboard/mouse not available");
      return;
    }
    // The drivers, including the keyboard/mouse shaping one, were registered by
    // the factory installed in OnPreSetup; the SDK attaches the window to them
    // once it exists (OnWindowAvailable).
    REXLOG_INFO("Input: keyboard/mouse shaping active (gain={} curve={} dz={})",
                REXCVAR_GET(bfbc_mouse_gain), REXCVAR_GET(bfbc_mouse_curve),
                REXCVAR_GET(bfbc_mouse_deadzone));
    // OnConfigurePaths runs before logging exists, so report the shooter
    // bindings here - this is what the guest will actually see.
    REXLOG_INFO("Input: fire='{}' aim='{}' | a='{}' b='{}' x='{}' lb='{}' rb='{}'",
                rex::cvar::GetFlagByName("keybind_right_trigger"),
                rex::cvar::GetFlagByName("keybind_left_trigger"),
                rex::cvar::GetFlagByName("keybind_a"),
                rex::cvar::GetFlagByName("keybind_b"),
                rex::cvar::GetFlagByName("keybind_x"),
                rex::cvar::GetFlagByName("keybind_left_shoulder"),
                rex::cvar::GetFlagByName("keybind_right_shoulder"));
    REXLOG_INFO("Input: dpad='{}','{}','{}','{}'",
                rex::cvar::GetFlagByName("keybind_dpad_up"),
                rex::cvar::GetFlagByName("keybind_dpad_down"),
                rex::cvar::GetFlagByName("keybind_dpad_left"),
                rex::cvar::GetFlagByName("keybind_dpad_right"));

    for (const std::string& name : rex::cvar::ListFlags()) {
      if (name.rfind("keybind_", 0) == 0 || name.rfind("mnk_", 0) == 0) {
        REXLOG_DEBUG("Input: {} = '{}'", name, rex::cvar::GetFlagByName(name));
      }
    }

    // Shadow depth work happens in the texture/render-target cache, so record
    // its budget next to the depth conversions: an evicted shadow map looks
    // exactly like a broken depth compare.
    const auto flag = [](const char* name) -> std::string {
      return rex::cvar::GetFlagInfo(name) ? rex::cvar::GetFlagByName(name) : std::string("n/a");
    };
    REXLOG_INFO("GPU: rt limit='{}' soft='{}' hard='{}' invalid_fetch='{}' 3d2d='{}' "
                "readback='{}' async='{}'",
                flag("texture_cache_memory_limit_render_to_texture"),
                flag("texture_cache_memory_limit_soft"), flag("texture_cache_memory_limit_hard"),
                flag("gpu_allow_invalid_fetch_constants"), flag("gpu_3d_to_2d_texture"),
                flag("readback_resolve"), flag("async_shader_compilation"));
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

    // The SDK palette is green; every overlay here uses blue accents instead.
    ImVec4* c = imgui_style.Colors;
    const ImVec4 accent(0.33f, 0.63f, 1.00f, 1.00f);
    const ImVec4 accent_soft(0.47f, 0.72f, 1.00f, 1.00f);
    const ImVec4 accent_deep(0.17f, 0.36f, 0.62f, 1.00f);
    const ImVec4 frame(0.08f, 0.13f, 0.22f, 1.00f);
    const ImVec4 line(0.24f, 0.42f, 0.68f, 0.55f);

    c[ImGuiCol_Text] = ImVec4(0.90f, 0.94f, 1.00f, 1.00f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.56f, 0.64f, 0.78f, 1.00f);
    c[ImGuiCol_WindowBg] = ImVec4(0.04f, 0.07f, 0.12f, 0.94f);
    c[ImGuiCol_ChildBg] = ImVec4(0.05f, 0.09f, 0.15f, 0.60f);
    c[ImGuiCol_PopupBg] = ImVec4(0.06f, 0.10f, 0.17f, 0.96f);
    c[ImGuiCol_Border] = line;
    c[ImGuiCol_FrameBg] = frame;
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.15f, 0.27f, 0.44f, 1.00f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.35f, 0.56f, 1.00f);
    c[ImGuiCol_TitleBg] = accent_deep;
    c[ImGuiCol_TitleBgActive] = ImVec4(0.20f, 0.42f, 0.72f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed] = accent_deep;
    c[ImGuiCol_MenuBarBg] = ImVec4(0.07f, 0.12f, 0.20f, 1.00f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.05f, 0.09f, 0.15f, 0.60f);
    c[ImGuiCol_ScrollbarGrab] = accent_deep;
    c[ImGuiCol_ScrollbarGrabHovered] = accent;
    c[ImGuiCol_ScrollbarGrabActive] = accent_soft;
    c[ImGuiCol_CheckMark] = accent_soft;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = accent_soft;
    c[ImGuiCol_Button] = accent_deep;
    c[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.50f, 0.82f, 1.00f);
    c[ImGuiCol_ButtonActive] = accent;
    c[ImGuiCol_Header] = accent_deep;
    c[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.50f, 0.82f, 1.00f);
    c[ImGuiCol_HeaderActive] = accent;
    c[ImGuiCol_Separator] = line;
    c[ImGuiCol_SeparatorHovered] = accent;
    c[ImGuiCol_SeparatorActive] = accent_soft;
    c[ImGuiCol_ResizeGrip] = ImVec4(0.24f, 0.42f, 0.68f, 0.45f);
    c[ImGuiCol_ResizeGripHovered] = accent;
    c[ImGuiCol_ResizeGripActive] = accent_soft;
    c[ImGuiCol_PlotHistogram] = accent;
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.33f, 0.63f, 1.00f, 0.35f);
  }

  // Add the curated graphics menu on top of the SDK overlays. ReXApp has
  // already registered F3 (debug), F4 (all cvars), F7 (achievements) and the
  // console on backtick by the time this runs.
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    (void)drawer;
    rex::ui::RegisterBind("bfbc_bind_graphics", "F5", "Toggle bfbc graphics settings",
                          [this] {
                            if (graphics_dialog_) {
                              CloseGraphicsDialog();
                              return;
                            }
                            // The F5 field-of-view option lives in config.zz,
                            // which sits under the game data root.
                            const std::filesystem::path config_zz =
                                game_data_root().empty()
                                    ? std::filesystem::path()
                                    : game_data_root() / "dist" / "xenon" / "config.zz";
                            graphics_dialog_ = std::make_unique<bfbc::GraphicsDialog>(
                                imgui_drawer(), config_path_, config_zz,
                                [this]() -> std::pair<uint32_t, uint32_t> {
                                  if (!window()) return {0, 0};
                                  return {window()->GetActualPhysicalWidth(),
                                          window()->GetActualPhysicalHeight()};
                                },
                                [this] {
                                  // Deferred so the dialog is not destroyed
                                  // from inside its own OnDraw.
                                  app_context().CallInUIThread(
                                      [this] { CloseGraphicsDialog(); });
                                });
                            // Mouse look (mnk_mouse) locks the pointer to the
                            // window and hides it. The SDK only lifts that lock
                            // for its own overlays, so with just this dialog open
                            // the pointer stayed captured and invisible: the
                            // menu could not be clicked. The gate is global in
                            // the MnK driver, so clearing it makes the driver
                            // release the capture and show the cursor again.
                            rex::input::mnk::SetMouseLookActive(false);
                          });

    // Watches the SDK-owned overlay windows (F4 included) so the pointer is
    // released for the menu and captured again for gameplay.
    cursor_watcher_ = std::make_unique<OverlayCursorWatcher>(imgui_drawer());
    imgui_drawer()->AddDialog(cursor_watcher_.get());
  }

  void OnShutdown() override {
    rex::ui::UnregisterBind("bfbc_bind_graphics");
    cursor_watcher_.reset();
    graphics_dialog_.reset();
    // Never leave the driver holding a pointer lock for a dialog that is gone.
    rex::input::mnk::SetMouseLookActive(true);
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
  // The SDK's F4 (cvar editor) and F5 (graphics) overlays are plain ImGui
  // windows. Mouse look (mnk_mouse) captures the pointer and hides the cursor,
  // and the SDK only releases that lock for dialogs that request it - the F4
  // editor does not, so the cursor stayed invisible over it. This invisible
  // dialog runs every frame and drops the mouse-look gate while any overlay
  // window is open, then restores it when they are all closed.
  class OverlayCursorWatcher final : public rex::ui::ImGuiDialog {
   public:
    explicit OverlayCursorWatcher(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

   protected:
    void OnDraw(ImGuiIO&) override {
      const bool overlay_open =
          IsOverlayOpen("Settings##rex") || IsOverlayOpen("Graphics  (F5)") ||
          IsOverlayOpen("Debug##overlay") || IsOverlayOpen("Console##rex") ||
          IsOverlayOpen("Achievements##overlay");
      const bool want_mouse_look = !overlay_open;
      if (rex::input::mnk::IsMouseLookActive() != want_mouse_look) {
        rex::input::mnk::SetMouseLookActive(want_mouse_look);
      }
    }

   private:
    static bool IsOverlayOpen(const char* name) {
      ImGuiWindow* window = ImGui::FindWindowByName(name);
      // Active is set by this frame's Begin(); WasActive survives when this
      // dialog happens to draw before the overlay. A one-frame tail is
      // harmless, a menu that is still open being read as closed is not.
      return window != nullptr && (window->Active || window->WasActive);
    }
  };

  // Sets a keyboard/mouse binding unless bfbc.toml already stores one. The
  // config is read after OnConfigurePaths, so anything saved there has to win
  // over these defaults; the file itself is the only reliable way to tell at
  // this point, because every flag still reports Source::kDefault here.
  // Binding names go through the shared helpers in bfbc_graphics.h: the
  // runtime's parser is case-sensitive, and the MnK driver cannot fire a bare
  // modifier (holding Shift makes its required-mask check fail), so the helper
  // also expands "Shift" to "Shift+Shift". Old configs that stored "LeftCtrl"
  // or "Ctrl" are converted the same way.
  static std::string NormalizeBindValue(const std::string& raw) {
    return bfbc::NormalizeBindValue(raw);
  }

  static std::string Trim(const std::string& text) {
    const size_t first = text.find_first_not_of(" \t\r\n\"");
    if (first == std::string::npos) {
      return {};
    }
    const size_t last = text.find_last_not_of(" \t\r\n\"");
    return text.substr(first, last - first + 1);
  }

  // Pushes every keybind_* the config file stores, so a binding saved from the
  // F5 menu survives a restart instead of being replaced by a default.
  void ApplySavedKeyBinds() {
    if (config_path_.empty()) {
      return;
    }
    std::ifstream in(config_path_);
    if (!in) {
      return;
    }
    std::string line;
    while (std::getline(in, line)) {
      const size_t eq = line.find('=');
      if (eq == std::string::npos) {
        continue;
      }
      const std::string name = Trim(line.substr(0, eq));
      if (name.rfind("keybind_", 0) != 0) {
        continue;
      }
      if (rex::cvar::GetFlagInfo(name.c_str()) == nullptr) {
        continue;
      }
      const std::string value =
          bfbc::AddModifierVariants(NormalizeBindValue(line.substr(eq + 1)));
      if (rex::cvar::SetFlagByName(name.c_str(), value)) {
        REXLOG_INFO("Input: {} = '{}' (config)", name, value);
      } else {
        REXLOG_WARN("Input: saved binding {} = '{}' rejected by the runtime", name, value);
      }
    }
  }

  void ApplyDefaultBind(const char* name, const char* value) {
    if (rex::cvar::GetFlagInfo(name) == nullptr) {
      return;
    }
    bool saved_present = false;
    const std::string saved = ReadConfigValue(config_path_, name, &saved_present);
    if (saved_present) {
      REXLOG_INFO("Input: {} kept from the config ('{}')", name, saved);
      return;
    }
    const std::string normalized = bfbc::AddModifierVariants(NormalizeBindValue(value));
    if (rex::cvar::SetFlagByName(name, normalized)) {
      REXLOG_INFO("Input: {} defaulted to '{}'", name, normalized);
    } else {
      REXLOG_WARN("Input: could not set the default for {}", name);
    }
  }

  // Closes the F5 dialog and gives the pointer back to the game's mouse look.
  void CloseGraphicsDialog() {
    graphics_dialog_.reset();
    rex::input::mnk::SetMouseLookActive(true);
  }

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

  // VRAM budgets used when bfbc.toml does not set them.
  //
  // The plugin's own defaults target a 720p-native title: 384/768 MB of texture
  // cache and 128 MB for render targets. At 3x internal resolution one colour
  // render target is already ~33 MB (a 16-bit HDR one twice that), so the
  // render-target budget holds only a handful of surfaces and the game evicts
  // and re-resolves them continuously. Each eviction is a resolve plus a clear
  // on the GPU, which is exactly what the long frames behind the stutter are;
  // it also means the shadow map and the HDR chain keep getting thrown away.
  // These values are only large numbers of megabytes, not settings that change
  // the picture, so a machine with less VRAM can lower them in bfbc.toml.
  struct StartupDefault {
    const char* name;
    const char* value;
  };
  // The render-target budget is capped at 256 MB by the plugin itself (it
  // rejects larger values and keeps its default), so that is the most the
  // eviction rate can be reduced without touching the plugin.
  static constexpr StartupDefault kStartupDefaults[] = {
      // gamma_render_target_as_unorm16 is deliberately not overridden: the
      // plugin default is the correct value, and the old "fix" here was what
      // made the lighting change. Lighting/darkness is handled by the readback
      // mode below instead.
      //
      // fast: read the previous frame's resolve (delayed, copied every frame).
      // This is the setting that actually brightens the world on this driver;
      // full stalls on every resolve, none stays dark. "some" copies only on a
      // cache miss and is selectable from F5; fast remains the default.
      {"readback_resolve", "fast"},
      // ROV turns every new blend/depth state combination into a pixel shader.
      // Compiling those on the render thread is the 40-90 ms hitch seen while
      // moving into a new area, so build them on the worker threads. Kept as a
      // default because a regenerated bfbc.toml drops the key when it matches
      // the plugin default, which silently re-enabled synchronous compiles.
      {"async_shader_compilation", "true"},
      {"texture_cache_memory_limit_render_to_texture", "256"},
      {"texture_cache_memory_limit_soft", "1024"},
      {"texture_cache_memory_limit_hard", "2048"},
  };

  static std::string DefaultStartupValue(const char* name) {
    for (const StartupDefault& entry : kStartupDefaults) {
      if (std::string_view(entry.name) == name) {
        return entry.value;
      }
    }
    return {};
  }

  // Minimal reader for the flat "key = value" config the SDK writes.
  static std::string ReadConfigValue(const std::filesystem::path& path, std::string_view key,
                                     bool* found = nullptr) {
    if (found != nullptr) {
      *found = false;
    }
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
      if (found != nullptr) {
        *found = true;
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
  std::unique_ptr<OverlayCursorWatcher> cursor_watcher_;
};
