// bfbc - curated graphics settings overlay (F5).
//
// The SDK already ships a raw cvar editor (F4). This dialog is a
// player-facing layer on top of the same cvars: display mode, output
// resolution, internal render resolution, vsync, anisotropic filtering and
// shader compilation, plus one-click presets.
//
// Everything it changes is an ordinary cvar, so the F4 editor always shows the
// live values, and "Save" persists the selection to bfbc.toml next to the exe.
// cvars whose lifecycle is init-only cannot be applied to a running session;
// those are written to the config file and reported as "restart required".

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>

#include "bfbc_configzip.h"

namespace bfbc {

// Default text scale for every rexglue overlay (F3/F4/F5/console). ImGui 1.92
// rasterizes fonts dynamically, so raising this stays sharp instead of
// stretching a bitmap. The F5 menu exposes it as a live slider.
inline constexpr float kDefaultUiScale = 1.4f;

// Names the runtime's key parser accepts (the table VirtualKeyToString uses), so
// a captured key can go straight into a keybind_* cvar. An InputText field never
// reports modifiers, arrows or mouse buttons, which is why the binding rows
// capture presses instead of taking typed text.
inline std::string BindNameForImGuiKey(ImGuiKey key) {
  if (key >= ImGuiKey_A && key <= ImGuiKey_Z) {
    return std::string(1, static_cast<char>('A' + (key - ImGuiKey_A)));
  }
  if (key >= ImGuiKey_0 && key <= ImGuiKey_9) {
    return std::string(1, static_cast<char>('0' + (key - ImGuiKey_0)));
  }
  if (key >= ImGuiKey_F1 && key <= ImGuiKey_F12) {
    return std::string("F") + std::to_string(1 + static_cast<int>(key - ImGuiKey_F1));
  }
  if (key >= ImGuiKey_Keypad0 && key <= ImGuiKey_Keypad9) {
    return std::string("Numpad") + std::to_string(static_cast<int>(key - ImGuiKey_Keypad0));
  }
  switch (key) {
    case ImGuiKey_LeftArrow:
      return "Left";
    case ImGuiKey_RightArrow:
      return "Right";
    case ImGuiKey_UpArrow:
      return "Up";
    case ImGuiKey_DownArrow:
      return "Down";
    case ImGuiKey_LeftShift:
    case ImGuiKey_RightShift:
      return "Shift";
    case ImGuiKey_LeftCtrl:
    case ImGuiKey_RightCtrl:
      return "Control";
    case ImGuiKey_LeftAlt:
    case ImGuiKey_RightAlt:
      return "Alt";
    case ImGuiKey_Space:
      return "Space";
    case ImGuiKey_Enter:
      return "Return";
    case ImGuiKey_Tab:
      return "Tab";
    case ImGuiKey_Backspace:
      return "Backspace";
    case ImGuiKey_Delete:
      return "Delete";
    case ImGuiKey_Insert:
      return "Insert";
    case ImGuiKey_Home:
      return "Home";
    case ImGuiKey_End:
      return "End";
    case ImGuiKey_PageUp:
      return "PageUp";
    case ImGuiKey_PageDown:
      return "PageDown";
    case ImGuiKey_GraveAccent:
      return "Backtick";
    case ImGuiKey_Minus:
      return "Minus";
    case ImGuiKey_Equal:
      return "Plus";
    case ImGuiKey_LeftBracket:
      return "LBracket";
    case ImGuiKey_RightBracket:
      return "RBracket";
    case ImGuiKey_Backslash:
      return "Backslash";
    case ImGuiKey_Semicolon:
      return "Semicolon";
    case ImGuiKey_Apostrophe:
      return "Quote";
    case ImGuiKey_Comma:
      return "Comma";
    case ImGuiKey_Period:
      return "Period";
    case ImGuiKey_Slash:
      return "Slash";
    case ImGuiKey_KeypadMultiply:
      return "NumpadStar";
    case ImGuiKey_KeypadAdd:
      return "NumpadPlus";
    case ImGuiKey_KeypadSubtract:
      return "NumpadMinus";
    case ImGuiKey_KeypadDivide:
      return "NumpadSlash";
    case ImGuiKey_KeypadEnter:
      return "NumpadEnter";
    default:
      return {};
  }
}

class GraphicsDialog final : public rex::ui::ImGuiDialog {
 public:
  using OutputSizeFn = std::function<std::pair<uint32_t, uint32_t>()>;
  using CloseFn = std::function<void()>;

  GraphicsDialog(rex::ui::ImGuiDrawer* drawer, std::filesystem::path config_path,
                 std::filesystem::path config_zz_path, OutputSizeFn output_size,
                 CloseFn on_close)
      : rex::ui::ImGuiDialog(drawer),
        config_path_(std::move(config_path)),
        config_zz_path_(std::move(config_zz_path)),
        output_size_(std::move(output_size)),
        on_close_(std::move(on_close)) {
    LoadFromCVars();
    LoadKeyBinds();
  }

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  // The guest renders 1280x720 by default; the scale multiplies the guest
  // render targets, which is the only knob that adds real detail.
  static constexpr int kGuestWidth = 1280;
  static constexpr int kGuestHeight = 720;
  static constexpr int kMinScale = 1;
  static constexpr int kMaxScale = 3;

  static constexpr const char* kResolutionValues[] = {"",    "720p",  "900p",
                                                     "1080p", "1440p", "2160p"};
  static constexpr const char* kResolutionLabels[] = {
      "Auto (desktop)", "1280x720", "1600x900", "1920x1080", "2560x1440", "3840x2160"};
  static constexpr int kResolutionCount =
      static_cast<int>(sizeof(kResolutionValues) / sizeof(kResolutionValues[0]));

  static constexpr const char* kAnisoLabels[] = {"Game default", "1x", "2x",
                                                 "4x",           "8x", "16x"};
  static constexpr int kAnisoCount =
      static_cast<int>(sizeof(kAnisoLabels) / sizeof(kAnisoLabels[0]));

  static constexpr const char* kScaleLabels[] = {"1x  (720p source)", "2x",
                                                 "3x  (supersampling)"};

  struct Settings {
    bool fullscreen = true;
    bool exclusive = false;
    int resolution = 0;
    int scale = 1;
    int aniso = 0;
    bool vsync = true;
    bool async_shader = true;
    // Guest frame cap in frames per second; 0 leaves the guest uncapped
    // (bfbc_frame.h, applied by the VdSwap wrapper).
    int fps_limit = 0;
    // Field of view written to config.zz (engine setting, next launch); 0 keeps
    // the value the game ships with.
    int fov = 0;
    // Mouse look drives the right stick, so this scales how much stick
    // deflection one mouse count produces.
    double mouse_sensitivity = 1.0;
    bool mouse_look = true;
    // Shaping applied to the right stick after the SDK's MnK driver (bfbc_mouse.h).
    double mouse_gain = 1.0;
    double mouse_curve = 1.0;
    double mouse_deadzone = 0.0;
    // Smallest stick deflection emitted for any movement, and whether the poll
    // interval is folded out (see bfbc_mouse.h).
    double mouse_floor = 7849.0;
    bool mouse_time_normalize = true;
    bool mouse_debug = false;
    bool dirty = false;
  };

  void LoadFromCVars();
  void LoadKeyBinds();
  // Set when a binding field differs from the cvar; Apply pushes them all.
  bool binds_dirty_ = false;
  // Row waiting for a key press, plus a one-frame guard so the click that starts
  // the capture does not become the binding.
  int capture_index_ = -1;
  bool capture_skip_frame_ = false;
  void DrawDisplaySection();
  void DrawRenderingSection();
  void DrawMouseSection();
  void DrawKeybindSection();
  void ApplyKeyBinds();
  void DrawActions();

  // "keybind_lstick_up" -> "lstick up" for display.
  static std::string PrettifyBindName(std::string suffix) {
    for (char& c : suffix) {
      if (c == '_') {
        c = ' ';
      }
    }
    return suffix;
  }

  // Applies the dialog state to the cvars. When `persist` is true the values
  // are also merged into the config file so init-only settings survive a
  // restart. Fills `restart_` with what still needs one.
  void Apply(bool persist);

  std::vector<std::pair<std::string, std::string>> PendingEntries() const;
  bool WriteConfigFile(const std::vector<std::pair<std::string, std::string>>& entries);

  static int ClampScale(int value) { return std::clamp(value, kMinScale, kMaxScale); }

  // The plugin registers its renderer switches from its own DLL, so a flag can
  // legitimately be absent (older/newer plugin). Only touch what exists.
  static bool HasFlag(const std::string& name) {
    return rex::cvar::GetFlagInfo(name) != nullptr;
  }

  // Entries are written to the config in TOML form, where strings are quoted;
  // the registry itself stores the bare value. Compare and set the bare form.
  static std::string Unquote(const std::string& value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
      return value.substr(1, value.size() - 2);
    }
    return value;
  }

  // The GPU plugin's string flags read back in config form ("fast"), while the
  // runtime's own read back bare, so normalise before comparing.
  static std::string ReadFlagValue(const char* name) {
    return Unquote(rex::cvar::GetFlagByName(name));
  }

  static bool ReadFlagBool(const char* name) { return ReadFlagValue(name) == "true"; }

  // The cvar registry formats doubles as "%.6f", so match that when comparing
  // stored and pending values.
  static std::string FormatSensitivity(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.6f", value);
    return buffer;
  }

  static bool IsInitOnly(const std::string& name) {
    const rex::cvar::FlagEntry* info = rex::cvar::GetFlagInfo(name);
    return info && info->lifecycle == rex::cvar::Lifecycle::kInitOnly;
  }

  // The GPU plugin reads these while it builds its render target cache and the
  // window, both of which happen once at startup. The runtime happily accepts
  // new values later, which is why this menu used to report "applied" while
  // the picture never changed: what is stored here is the value the next
  // launch will start with.
  static bool IsRestartOnly(const std::string& name) {
    return name == "draw_resolution_scale_x" || name == "draw_resolution_scale_y" ||
           name == "resolution_scale" || name == "resolution" ||
           name == "draw_resolution_scaled_texture_offsets" ||
           name == "resolve_resolution_scale_fill_half_pixel_offset" ||
           name == "readback_resolve_half_pixel_offset" || name == "d3d12_readback_resolve";
  }

  struct KeyBind {
    std::string name;   // cvar name, e.g. keybind_lstick_up
    std::string label;  // display name
    char text[64] = {}; // edited value, applied on Enter
  };

  std::filesystem::path config_path_;
  // game/dist/xenon/config.zz - where the engine reads Render.ForceFov from.
  std::filesystem::path config_zz_path_;
  int stored_fov_ = 0;
  OutputSizeFn output_size_;
  CloseFn on_close_;

  std::vector<KeyBind> binds_;
  Settings settings_;
  std::string status_;
  std::vector<std::string> restart_;
  bool status_is_error_ = false;
};

// ---------------------------------------------------------------------------
// State <-> cvars
// ---------------------------------------------------------------------------

inline void GraphicsDialog::LoadFromCVars() {
  settings_.fullscreen = rex::cvar::Query<bool>("fullscreen");
  settings_.exclusive = rex::cvar::Query<bool>("fullscreen_exclusive");
  settings_.vsync = rex::cvar::Query<bool>("vsync");
  settings_.async_shader = rex::cvar::Query<bool>("async_shader_compilation");
  settings_.fps_limit = std::clamp(rex::cvar::Query<int32_t>("bfbc_fps_limit"), 0, 480);
  settings_.scale = ClampScale(rex::cvar::Query<int32_t>("draw_resolution_scale_x"));
  settings_.aniso = std::clamp(rex::cvar::Query<int32_t>("anisotropic_override"), 0,
                               kAnisoCount - 1);

  const std::string value = rex::cvar::Query<std::string>("resolution");
  settings_.resolution = 0;
  for (int i = 0; i < kResolutionCount; ++i) {
    if (value == kResolutionValues[i]) {
      settings_.resolution = i;
      break;
    }
  }

  settings_.mouse_sensitivity = rex::cvar::Query<double>("mnk_sensitivity");
  if (!(settings_.mouse_sensitivity > 0.0)) {
    settings_.mouse_sensitivity = 1.0;
  }
  settings_.mouse_look = rex::cvar::Query<bool>("mnk_mouse");
  settings_.mouse_gain = std::clamp(rex::cvar::Query<double>("bfbc_mouse_gain"), 0.05, 8.0);
  settings_.mouse_curve = std::clamp(rex::cvar::Query<double>("bfbc_mouse_curve"), 0.2, 4.0);
  settings_.mouse_deadzone =
      std::clamp(rex::cvar::Query<double>("bfbc_mouse_deadzone"), 0.0, 0.9);
  settings_.mouse_floor = std::clamp(rex::cvar::Query<double>("bfbc_mouse_floor"), 0.0, 20000.0);
  settings_.mouse_time_normalize = rex::cvar::Query<bool>("bfbc_mouse_time_normalize");
  settings_.mouse_debug = rex::cvar::Query<bool>("bfbc_mouse_debug");

  // The field of view is an engine setting inside config.zz, not a cvar, so it
  // can only be read back from the archive.
  const configzip::FovState fov = configzip::ReadFov(config_zz_path_);
  settings_.fov = fov.present
                      ? std::clamp(fov.degrees, configzip::kFovMin, configzip::kFovMax)
                      : 0;
  stored_fov_ = settings_.fov;

  settings_.dirty = false;
}

inline std::vector<std::pair<std::string, std::string>> GraphicsDialog::PendingEntries() const {
  auto quote = [](const std::string& s) { return "\"" + s + "\""; };
  std::vector<std::pair<std::string, std::string>> entries = {
      {"fullscreen", settings_.fullscreen ? "true" : "false"},
      {"fullscreen_exclusive", settings_.exclusive ? "true" : "false"},
      {"resolution", quote(kResolutionValues[settings_.resolution])},
      {"draw_resolution_scale_x", std::to_string(ClampScale(settings_.scale))},
      {"draw_resolution_scale_y", std::to_string(ClampScale(settings_.scale))},
      {"resolution_scale", std::to_string(ClampScale(settings_.scale))},
      {"vsync", settings_.vsync ? "true" : "false"},
      {"anisotropic_override", std::to_string(settings_.aniso)},
      {"async_shader_compilation", settings_.async_shader ? "true" : "false"},
      {"bfbc_fps_limit", std::to_string(settings_.fps_limit)},
      {"mnk_sensitivity", FormatSensitivity(settings_.mouse_sensitivity)},
      {"mnk_mouse", settings_.mouse_look ? "true" : "false"},
      {"bfbc_mouse_gain", FormatSensitivity(settings_.mouse_gain)},
      {"bfbc_mouse_curve", FormatSensitivity(settings_.mouse_curve)},
      {"bfbc_mouse_deadzone", FormatSensitivity(settings_.mouse_deadzone)},
      {"bfbc_mouse_floor", FormatSensitivity(settings_.mouse_floor)},
      {"bfbc_mouse_time_normalize", settings_.mouse_time_normalize ? "true" : "false"},
      {"bfbc_mouse_debug", settings_.mouse_debug ? "true" : "false"},
  };

  return entries;
}

inline void GraphicsDialog::Apply(bool persist) {
  restart_.clear();
  int applied = 0;
  const auto entries = PendingEntries();
  std::vector<std::pair<std::string, std::string>> restart_only_entries;

  // The field of view has no cvar behind it: the engine reads Render.ForceFov
  // from config.zz at boot, so Apply and Save both write the archive and the
  // value only takes effect after a restart.
  std::string fov_error;
  if (settings_.fov != stored_fov_) {
    if (!configzip::WriteFov(config_zz_path_, settings_.fov, &fov_error)) {
      fov_error = "Field of view could not be written: " + fov_error;
    } else {
      restart_.push_back("Render.ForceFov (config.zz)");
    }
  }

  for (const auto& [name, config_value] : entries) {
    const std::string value = Unquote(config_value);
    if (ReadFlagValue(name.c_str()) == value) {
      continue;
    }
    if (IsInitOnly(name)) {
      restart_.push_back(name);
      continue;
    }
    if (!rex::cvar::SetFlagByName(name, value)) {
      restart_.push_back(name);
      continue;
    }
    if (IsRestartOnly(name)) {
      // Take the value so the UI reflects it, but persist it for the next
      // launch instead of pretending the running session changed.
      restart_only_entries.emplace_back(name, config_value);
      restart_.push_back(name);
      continue;
    }
    ++applied;
  }

  bool saved = true;
  if (persist) {
    saved = WriteConfigFile(entries);
  } else if (!restart_only_entries.empty()) {
    saved = WriteConfigFile(restart_only_entries);
  }

  if (!restart_.empty()) {
    status_is_error_ = !saved;
    status_ = saved ? "Saved. The settings listed below take effect after a restart."
                    : "Those settings could not be written to the config file.";
  } else if (persist) {
    status_is_error_ = !saved;
    status_ = saved ? "Saved to " + config_path_.filename().string() + "." : "Config write failed.";
  } else if (applied > 0) {
    status_is_error_ = false;
    status_ = "Applied.";
  } else {
    status_is_error_ = false;
    status_ = "Nothing to apply.";
  }

  if (!fov_error.empty()) {
    status_is_error_ = true;
    status_ = fov_error;
  }

  settings_.dirty = false;
  if (applied > 0) {
    REXLOG_INFO("Graphics: applied {} cvar(s){}", applied, persist ? ", saved to config" : "");
  }
  LoadFromCVars();  // Pick up normalized values / new defaults.
}

// Flat "key = value" merge; the SDK writes configs in exactly this shape, so
// other keys and comments are preserved verbatim.
inline bool GraphicsDialog::WriteConfigFile(
    const std::vector<std::pair<std::string, std::string>>& entries) {
  if (config_path_.empty()) {
    return false;
  }

  std::vector<std::string> kept;
  if (std::filesystem::exists(config_path_)) {
    std::ifstream in(config_path_);
    std::string line;
    while (std::getline(in, line)) {
      std::string key;
      const size_t eq = line.find('=');
      if (eq != std::string::npos) {
        key = line.substr(0, eq);
        const size_t first = key.find_first_not_of(" \t");
        const size_t last = key.find_last_not_of(" \t");
        key = (first == std::string::npos) ? std::string() : key.substr(first, last - first + 1);
      }
      bool ours = false;
      for (const auto& [name, value] : entries) {
        if (key == name) {
          ours = true;
          break;
        }
      }
      if (!ours) {
        kept.push_back(line);
      }
    }
  }

  // Drop trailing blank lines left behind by removed keys.
  while (!kept.empty() && kept.back().find_first_not_of(" \t\r") == std::string::npos) {
    kept.pop_back();
  }

  std::ofstream out(config_path_, std::ios::trunc);
  if (!out) {
    REXLOG_ERROR("Graphics: cannot write {}", config_path_.string());
    return false;
  }
  if (!kept.empty()) {
    out << "# Auto-generated cvar configuration\n";
    for (const auto& line : kept) {
      if (!line.empty() && line[0] == '#') {
        continue;  // Avoid duplicating the generated header.
      }
      out << line << "\n";
    }
  } else {
    out << "# Auto-generated cvar configuration\n";
  }
  out << "\n# bfbc graphics menu\n";
  for (const auto& [name, value] : entries) {
    out << name << " = " << value << "\n";
  }
  REXLOG_INFO("Graphics: wrote {}", config_path_.string());
  return static_cast<bool>(out);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

inline void GraphicsDialog::DrawDisplaySection() {
  ImGui::SeparatorText("Display");

  int mode = settings_.fullscreen ? (settings_.exclusive ? 2 : 1) : 0;
  static const char* kModeLabels[] = {"Windowed", "Borderless fullscreen", "Exclusive fullscreen"};
  if (ImGui::Combo("Display mode", &mode, kModeLabels, 3)) {
    settings_.fullscreen = mode != 0;
    settings_.exclusive = mode == 2;
    settings_.dirty = true;
  }

  if (ImGui::Combo("Output resolution", &settings_.resolution, kResolutionLabels,
                   kResolutionCount)) {
    settings_.dirty = true;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
        "Window size, and the guest video mode preset. The guest still renders\n"
        "1280x720 unless the internal resolution below is raised.");
  }

  if (output_size_) {
    const auto [w, h] = output_size_();
    if (w && h) {
      ImGui::TextDisabled("Window: %u x %u (%.3f:1)", w, h, static_cast<double>(w) / h);
    }
  }
}

inline void GraphicsDialog::DrawRenderingSection() {
  ImGui::SeparatorText("Rendering");

  int scale = ClampScale(settings_.scale);
  if (ImGui::Combo("Internal resolution", &scale, kScaleLabels, kMaxScale)) {
    settings_.scale = ClampScale(scale);
    settings_.dirty = true;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
        "Applied at the next launch: the render target cache is sized once at\n"
        "startup, so this cannot change the running session.");
  }

  const int internal_w = kGuestWidth * ClampScale(settings_.scale);
  const int internal_h = kGuestHeight * ClampScale(settings_.scale);
  ImGui::TextDisabled("Guest %dx%d  x%d  =  %dx%d", kGuestWidth, kGuestHeight,
                      ClampScale(settings_.scale), internal_w, internal_h);
  if (output_size_) {
    const auto [w, h] = output_size_();
    if (w && h && static_cast<uint32_t>(internal_h) < h) {
      ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f),
                         "Below the window height: the image is upscaled, not native.");
    }
  }

  if (ImGui::Checkbox("VSync", &settings_.vsync)) {
    settings_.dirty = true;
  }

  int fps_limit = settings_.fps_limit;
  if (ImGui::InputInt("Frame rate limit", &fps_limit, 5, 30)) {
    settings_.fps_limit = std::clamp(fps_limit, 0, 480);
    settings_.dirty = true;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
        "Frames per second the guest is allowed to present; 0 is uncapped. Applied\n"
        "live at the guest's swap, so it needs no restart. VSync still caps at the\n"
        "display rate whenever that is the lower of the two.");
  }
  if (settings_.fps_limit != 0) {
    ImGui::SameLine();
    ImGui::TextDisabled("(%.2f ms)", 1000.0 / settings_.fps_limit);
  }

  bool override_fov = settings_.fov != 0;
  if (ImGui::Checkbox("Override field of view", &override_fov)) {
    settings_.fov = override_fov ? 85 : 0;
    settings_.dirty = true;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
        "Writes Render.ForceFov into config.zz: the engine reads the field of view\n"
        "at boot, so this takes effect after a restart. 60 is the shipped value.");
  }
  ImGui::BeginDisabled(!override_fov);
  int fov = settings_.fov != 0 ? settings_.fov : 85;
  if (ImGui::SliderInt("Field of view", &fov, configzip::kFovMin, configzip::kFovMax,
                       "%d deg")) {
    settings_.fov = std::clamp(fov, configzip::kFovMin, configzip::kFovMax);
    settings_.dirty = true;
  }
  ImGui::EndDisabled();
  if (settings_.fov != 0) {
    ImGui::SameLine();
    ImGui::TextDisabled("(restart to apply)");
  }

  if (ImGui::Combo("Anisotropic filtering", &settings_.aniso, kAnisoLabels, kAnisoCount)) {
    settings_.dirty = true;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Overrides the filter level requested by the game for eligible textures.");
  }

  if (ImGui::Checkbox("Async shader compilation", &settings_.async_shader)) {
    settings_.dirty = true;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Compiles shaders in parallel; disables itself on drivers that need sync.");
  }
}

inline void GraphicsDialog::LoadKeyBinds() {
  binds_.clear();
  binds_dirty_ = false;
  for (const std::string& name : rex::cvar::ListFlags()) {
    if (name.rfind("keybind_", 0) != 0) {
      continue;
    }
    KeyBind bind;
    bind.name = name;
    bind.label = PrettifyBindName(name.substr(8));
    std::snprintf(bind.text, sizeof(bind.text), "%s",
                  rex::cvar::GetFlagByName(name).c_str());
    binds_.push_back(std::move(bind));
  }
  std::sort(binds_.begin(), binds_.end(),
            [](const KeyBind& a, const KeyBind& b) { return a.label < b.label; });
}

inline void GraphicsDialog::DrawMouseSection() {
  ImGui::SeparatorText("Mouse (right stick)");

  ImGui::TextDisabled(
      "Left button fires and right button aims by default; both are ordinary\n"
      "bindings (keybind_right_trigger / keybind_left_trigger) listed below.");

  float sensitivity = static_cast<float>(settings_.mouse_sensitivity);
  if (ImGui::SliderFloat("Sensitivity", &sensitivity, 0.25f, 4.0f, "%.2f")) {
    settings_.mouse_sensitivity = static_cast<double>(sensitivity);
    settings_.dirty = true;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
        "How much stick deflection each mouse count produces. Applies live;\n"
        "Save writes it to bfbc.toml.");
  }

  if (ImGui::Checkbox("Mouse look", &settings_.mouse_look)) {
    settings_.dirty = true;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Off: the right stick comes from the keybind_rstick_* keys only.");
  }

  float gain = static_cast<float>(settings_.mouse_gain);
  if (ImGui::SliderFloat("Mouse gain", &gain, 0.25f, 4.0f, "%.2f")) {
    settings_.mouse_gain = static_cast<double>(gain);
    settings_.dirty = true;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
        "Stick deflection per mouse count, applied after the SDK driver.\n"
        "Above 1 the stick saturates sooner, so quick flicks turn faster.");
  }

  float curve = static_cast<float>(settings_.mouse_curve);
  if (ImGui::SliderFloat("Response curve", &curve, 0.40f, 2.00f, "%.2f")) {
    settings_.mouse_curve = static_cast<double>(curve);
    settings_.dirty = true;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
        "1.00 = linear. Below 1 small movements turn more, above 1 gives\n"
        "finer aim near the centre.");
  }

  float deadzone = static_cast<float>(settings_.mouse_deadzone);
  if (ImGui::SliderFloat("Deadzone", &deadzone, 0.0f, 0.40f, "%.2f")) {
    settings_.mouse_deadzone = static_cast<double>(deadzone);
    settings_.dirty = true;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
        "Stick travel ignored before shaping. 0 keeps everything the driver\n"
        "reported; the game's own deadzone still applies on top.");
  }

  float floor = static_cast<float>(settings_.mouse_floor);
  if (ImGui::SliderFloat("Deadzone compensation", &floor, 0.0f, 20000.0f, "%.0f")) {
    settings_.mouse_floor = static_cast<double>(floor);
    settings_.dirty = true;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
        "The title ignores stick deflection inside its own deadzone (7849). This is\n"
        "the smallest deflection sent for any mouse movement, so slow aiming is not\n"
        "swallowed. Raise it if fine aim still feels dead, lower it for more precision.");
  }

  if (ImGui::Checkbox("Frame-rate independent aim", &settings_.mouse_time_normalize)) {
    settings_.dirty = true;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
        "Scales mouse movement by the poll interval, so the same hand speed turns the\n"
        "same amount at 60, 120 or 144 fps instead of getting slower as fps rises.");
  }

  if (ImGui::Checkbox("Log stick values (tuning)", &settings_.mouse_debug)) {
    settings_.dirty = true;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Writes raw vs. shaped right-stick values to the log, 10 per second.");
  }

  if (ImGui::Button("Preset: PC aim")) {
    // The guest turns at a speed taken from the stick, and its own curve makes
    // the first part of a movement weak. A curve below 1 pre-compensates that,
    // and the extra gain keeps a normal sweep from feeling slow.
    settings_.mouse_gain = 2.0;
    settings_.mouse_curve = 0.75;
    settings_.mouse_deadzone = 0.0;
    settings_.mouse_floor = 7849.0;
    settings_.mouse_time_normalize = true;
    settings_.dirty = true;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
        "gain 2.00, curve 0.75, floor 7849, frame-rate independent.\n"
        "Every mouse movement clears the game's deadzone and short movements\n"
        "are not softened by its acceleration. Tune from here.");
  }
  ImGui::SameLine();
  ImGui::TextDisabled("(applies live)");

  ImGui::TextDisabled(
      "The title only reads an analog stick, so turning speed still follows its\n"
      "own stick curve. For a consistent feel also turn off Windows\n"
      "\"Enhance pointer precision\" in the mouse settings.");
}

inline void GraphicsDialog::ApplyKeyBinds() {
  std::vector<std::pair<std::string, std::string>> changed;
  for (KeyBind& bind : binds_) {
    const std::string current = rex::cvar::GetFlagByName(bind.name);
    if (current == bind.text) {
      continue;
    }
    if (rex::cvar::SetFlagByName(bind.name, bind.text)) {
      changed.emplace_back(bind.name, std::string(bind.text));
    } else {
      // Put the rejected text back to what the input system actually has.
      std::snprintf(bind.text, sizeof(bind.text), "%s", current.c_str());
      status_is_error_ = true;
      status_ = "Unknown key name, binding for " + bind.label + " left unchanged";
      REXLOG_WARN("Input: rejected value '{}' for {}", bind.text, bind.name);
    }
  }
  if (!changed.empty()) {
    WriteConfigFile(changed);
    status_is_error_ = false;
    status_ = std::to_string(changed.size()) +
              (changed.size() == 1 ? " binding applied and saved" : " bindings applied and saved");
    REXLOG_INFO("Input: {} binding(s) applied", changed.size());
  }
  binds_dirty_ = false;
}

inline void GraphicsDialog::DrawKeybindSection() {
  ImGui::Spacing();
  if (!ImGui::CollapsingHeader("Keyboard / mouse bindings  (click, then press)")) {
    return;
  }

  ImGui::TextDisabled(
      "Click a binding, then press the key or mouse button you want: letters, F1-F12,\n"
      "mouse buttons, Shift / Ctrl / Alt and the arrow keys all work. Escape cancels,\n"
      "Clear removes the binding. Changes stay pending until Apply bindings.");

  ImGui::BeginChild("##keybinds", ImVec2(0.0f, 230.0f), ImGuiChildFlags_Borders);
  for (size_t i = 0; i < binds_.size(); ++i) {
    KeyBind& bind = binds_[i];
    ImGui::PushID(static_cast<int>(i));
    const bool capturing = capture_index_ == static_cast<int>(i);
    const std::string shown =
        capturing ? std::string("press a key...")
                  : (bind.text[0] ? std::string(bind.text) : std::string("(none)"));
    if (ImGui::Button((shown + "##bind").c_str(), ImVec2(170.0f, 0.0f)) && !capturing) {
      capture_index_ = static_cast<int>(i);
      capture_skip_frame_ = true;
      status_.clear();
      status_is_error_ = false;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) {
      bind.text[0] = '\0';
      binds_dirty_ = true;
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(bind.label.c_str());
    if (rex::cvar::GetFlagByName(bind.name) != bind.text) {
      ImGui::SameLine();
      ImGui::TextDisabled("*");
    }
    ImGui::PopID();
  }
  ImGui::EndChild();

  // The capture reads ImGui's raw input state: that is the only place the mouse
  // buttons and the modifier/arrow keys are visible at all.
  if (capture_index_ >= 0) {
    ImGui::TextDisabled("Waiting for a key or mouse button - Escape cancels.");
    if (capture_skip_frame_) {
      capture_skip_frame_ = false;  // the click that opened the capture
    } else {
      std::string name;
      bool cancel = false;
      if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        cancel = true;
      } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        name = "LMB";
      } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        name = "RMB";
      } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
        name = "MMB";
      } else {
        for (ImGuiKey key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END;
             key = static_cast<ImGuiKey>(static_cast<int>(key) + 1)) {
          if (!ImGui::IsKeyPressed(key, false)) {
            continue;
          }
          name = BindNameForImGuiKey(key);
          if (!name.empty()) {
            break;
          }
        }
      }
      if (cancel) {
        capture_index_ = -1;
      } else if (!name.empty()) {
        KeyBind& bind = binds_[static_cast<size_t>(capture_index_)];
        std::snprintf(bind.text, sizeof(bind.text), "%s", name.c_str());
        binds_dirty_ = true;
        capture_index_ = -1;
      }
    }
  }

  ImGui::BeginDisabled(!binds_dirty_);
  if (ImGui::Button("Apply bindings")) {
    ApplyKeyBinds();
  }
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    ImGui::SetTooltip(
        "Pushes every edited binding to the input system and writes it to\n"
        "the config. Rows marked * differ from what is active.");
  }
  ImGui::SameLine();
  if (binds_dirty_) {
    ImGui::TextDisabled("Pending binding changes");
  } else if (!status_.empty()) {
    ImGui::TextDisabled("%s", status_.c_str());
  }
}

inline void GraphicsDialog::DrawActions() {
  ImGui::Separator();
  // A live-only Apply left the menu state in the cvars but not in the file, so
  // the next launch silently went back to the previous values. One button now
  // does both; "Revert" is the way to throw edits away.
  if (ImGui::Button("Apply & save")) {
    Apply(/*persist=*/true);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
        "Applies the live settings and writes every value to %s, so the menu\n"
        "state survives a restart. Field of view goes into config.zz instead.",
        config_path_.filename().string().c_str());
  }
  ImGui::SameLine();
  if (ImGui::Button("Revert")) {
    LoadFromCVars();
    LoadKeyBinds();
    restart_.clear();
    status_.clear();
    status_is_error_ = false;
  }
  ImGui::SameLine();
  if (ImGui::Button("Close (F5)") && on_close_) {
    on_close_();
    return;
  }

  if (!status_.empty()) {
    ImGui::Spacing();
    ImGui::TextColored(status_is_error_ ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f)
                                        : ImVec4(0.55f, 0.85f, 0.55f, 1.0f),
                       "%s", status_.c_str());
  }
  if (!restart_.empty()) {
    ImGui::TextWrapped("Requires restart:");
    for (const auto& name : restart_) {
      ImGui::BulletText("%s", name.c_str());
    }
  }
  if (settings_.dirty) {
    ImGui::TextDisabled("Unapplied changes - press Apply & save.");
  }
}

inline void GraphicsDialog::OnDraw(ImGuiIO& io) {
  (void)io;
  ImGui::SetNextWindowSize(ImVec2(620.0f, 870.0f), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Graphics  (F5)", nullptr, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }

  DrawDisplaySection();
  ImGui::Spacing();
  DrawRenderingSection();
  ImGui::Spacing();
  DrawMouseSection();
  DrawKeybindSection();
  ImGui::Spacing();
  DrawActions();

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::TextDisabled(
      "F4: all cvars   |   F3: debug/stats   |   `: console   |   black bars in cutscenes are "
      "the game's own 2.35:1 letterbox");

  ImGui::End();
}

}  // namespace bfbc
