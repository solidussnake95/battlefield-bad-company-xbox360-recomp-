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

namespace bfbc {

// Default text scale for every rexglue overlay (F3/F4/F5/console). ImGui 1.92
// rasterizes fonts dynamically, so raising this stays sharp instead of
// stretching a bitmap. The F5 menu exposes it as a live slider.
inline constexpr float kDefaultUiScale = 1.4f;

class GraphicsDialog final : public rex::ui::ImGuiDialog {
 public:
  using OutputSizeFn = std::function<std::pair<uint32_t, uint32_t>()>;
  using CloseFn = std::function<void()>;

  GraphicsDialog(rex::ui::ImGuiDrawer* drawer, std::filesystem::path config_path,
                 OutputSizeFn output_size, CloseFn on_close)
      : rex::ui::ImGuiDialog(drawer),
        config_path_(std::move(config_path)),
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
    // Mouse look drives the right stick, so this scales how much stick
    // deflection one mouse count produces.
    double mouse_sensitivity = 1.0;
    bool mouse_look = true;
    bool dirty = false;
  };

  void LoadFromCVars();
  void LoadKeyBinds();
  void DrawDisplaySection();
  void DrawRenderingSection();
  void DrawMouseSection();
  void DrawKeybindSection();
  void DrawPresets();
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
           name == "resolution_scale" || name == "resolution";
  }

  struct KeyBind {
    std::string name;   // cvar name, e.g. keybind_lstick_up
    std::string label;  // display name
    char text[64] = {}; // edited value, applied on Enter
  };

  std::filesystem::path config_path_;
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

  settings_.dirty = false;
}

inline std::vector<std::pair<std::string, std::string>> GraphicsDialog::PendingEntries() const {
  auto quote = [](const std::string& s) { return "\"" + s + "\""; };
  return {
      {"fullscreen", settings_.fullscreen ? "true" : "false"},
      {"fullscreen_exclusive", settings_.exclusive ? "true" : "false"},
      {"resolution", quote(kResolutionValues[settings_.resolution])},
      {"draw_resolution_scale_x", std::to_string(ClampScale(settings_.scale))},
      {"draw_resolution_scale_y", std::to_string(ClampScale(settings_.scale))},
      {"resolution_scale", std::to_string(ClampScale(settings_.scale))},
      {"vsync", settings_.vsync ? "true" : "false"},
      {"anisotropic_override", std::to_string(settings_.aniso)},
      {"async_shader_compilation", settings_.async_shader ? "true" : "false"},
      {"mnk_sensitivity", FormatSensitivity(settings_.mouse_sensitivity)},
      {"mnk_mouse", settings_.mouse_look ? "true" : "false"},
  };
}

inline void GraphicsDialog::Apply(bool persist) {
  restart_.clear();
  int applied = 0;
  const auto entries = PendingEntries();
  std::vector<std::pair<std::string, std::string>> restart_only_entries;

  for (const auto& [name, value] : entries) {
    if (rex::cvar::GetFlagByName(name) == value) {
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
      restart_only_entries.emplace_back(name, value);
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

inline void GraphicsDialog::DrawKeybindSection() {
  ImGui::Spacing();
  if (!ImGui::CollapsingHeader("Keyboard bindings  (type, then press Enter)")) {
    return;
  }

  ImGui::TextDisabled(
      "Comma-separated key names, for example: W, Up, Semicolon, Shift+Up.\n"
      "Empty clears the binding. Applied immediately and written to the config.");

  ImGui::BeginChild("##keybinds", ImVec2(0.0f, 210.0f), ImGuiChildFlags_Borders);
  for (size_t i = 0; i < binds_.size(); ++i) {
    KeyBind& bind = binds_[i];
    ImGui::PushID(static_cast<int>(i));
    ImGui::SetNextItemWidth(190.0f);
    const bool submitted = ImGui::InputText("##value", bind.text, sizeof(bind.text),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    ImGui::TextUnformatted(bind.label.c_str());
    if (submitted) {
      if (rex::cvar::SetFlagByName(bind.name, bind.text)) {
        WriteConfigFile({{bind.name, bind.text}});
        status_is_error_ = false;
        status_ = bind.label + " = " + (bind.text[0] ? bind.text : "(unbound)");
        REXLOG_INFO("Input: {} = '{}'", bind.name, bind.text);
      } else {
        std::snprintf(bind.text, sizeof(bind.text), "%s",
                      rex::cvar::GetFlagByName(bind.name).c_str());
        status_is_error_ = true;
        status_ = "Unknown key name, binding for " + bind.label + " left unchanged";
        REXLOG_WARN("Input: rejected value '{}' for {}", bind.text, bind.name);
      }
    }
    ImGui::PopID();
  }
  ImGui::EndChild();
}

inline void GraphicsDialog::DrawMouseSection() {
  ImGui::SeparatorText("Mouse (right stick)");

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

  ImGui::TextDisabled(
      "The title only reads an analog stick, so turning speed still follows its\n"
      "own stick curve. For a consistent feel also turn off Windows\n"
      "\"Enhance pointer precision\" in the mouse settings.");
}

inline void GraphicsDialog::DrawPresets() {  ImGui::SeparatorText("Presets");

  const auto button = [this](const char* label, int scale, int aniso) {
    if (!ImGui::Button(label)) {
      return;
    }
    settings_.scale = ClampScale(scale);
    settings_.aniso = aniso;
    settings_.vsync = true;
    settings_.async_shader = true;
    Apply(/*persist=*/false);
  };

  button("Performance", 1, 1);
  ImGui::SameLine();
  button("Balanced", 2, 3);
  ImGui::SameLine();
  button("Quality", 3, 5);

  ImGui::SameLine();
  if (ImGui::Button("Match display")) {
    uint32_t h = kGuestHeight;
    if (output_size_) {
      const auto size = output_size_();
      if (size.second) {
        h = size.second;
      }
    }
    const int scale = ClampScale(
        static_cast<int>((h + kGuestHeight - 1) / kGuestHeight));  // ceil
    settings_.scale = scale;
    settings_.aniso = 5;
    settings_.vsync = true;
    settings_.async_shader = true;
    Apply(/*persist=*/false);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Internal resolution that covers the current window without upscaling.");
  }
}

inline void GraphicsDialog::DrawActions() {
  ImGui::Separator();
  if (ImGui::Button("Apply")) {
    Apply(/*persist=*/false);
  }
  ImGui::SameLine();
  if (ImGui::Button("Save")) {
    Apply(/*persist=*/true);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Writes to %s so init-only settings survive a restart.",
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
    ImGui::TextDisabled("Unapplied changes - press Apply or Save.");
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
  DrawPresets();
  DrawActions();

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::TextDisabled(
      "F4: all cvars   |   F3: debug/stats   |   `: console   |   black bars in cutscenes are "
      "the game's own 2.35:1 letterbox");

  ImGui::End();
}

}  // namespace bfbc
