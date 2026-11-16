// bfbc - cvar definitions for the mouse shaping layer (see bfbc_mouse.h) and the
// input system that installs it.
//
// Declared here rather than in the header because the REXCVAR_DEFINE_* macros
// create file-scope storage; the flags are registered during static
// initialisation, i.e. before bfbc.toml is read, so saved values apply.

#include "bfbc_mouse.h"

#include <string>

#include <rex/input/device_assignment.h>
#include <rex/input/flags.h>
#include <rex/input/input_system.h>
#include <rex/input/nop/nop_input_driver.h>
#include <rex/input/sdl/sdl_input_driver.h>
#if defined(_WIN32)
#include <rex/input/xinput/xinput_input_driver.h>
#endif

REXCVAR_DEFINE_DOUBLE(bfbc_mouse_gain, 2.0, "bfbc input",
                      "Multiplier on the right-stick deflection produced by mouse movement");

REXCVAR_DEFINE_DOUBLE(bfbc_mouse_vertical, 1.8, "bfbc input",
                      "Extra multiplier on the vertical (look up/down) stick output; the game's "
                      "own vertical turn rate is lower than its horizontal one");

REXCVAR_DEFINE_DOUBLE(bfbc_mouse_curve, 1.0, "bfbc input",
                      "Stick response curve: below 1 turns faster near the centre, above 1 finer");

REXCVAR_DEFINE_DOUBLE(bfbc_mouse_deadzone, 0.0, "bfbc input",
                      "Stick travel discarded before shaping, 0 disables it");

REXCVAR_DEFINE_DOUBLE(bfbc_mouse_floor, 7849.0, "bfbc input",
                      "Smallest stick deflection sent for any mouse movement, so the game's own "
                      "deadzone does not swallow slow aiming (0 disables it)")
    .range(0.0, 20000.0);

REXCVAR_DEFINE_BOOL(bfbc_mouse_time_normalize, true, "bfbc input",
                    "Scale mouse movement by the poll interval so the turn rate does not depend on "
                    "frame rate");

REXCVAR_DEFINE_STRING(bfbc_mouse_x1, "x", "bfbc input",
                      "Pad action on mouse button 4 (side/back); same values as bfbc_mouse_middle")
    .allowed({"none", "a", "b", "x", "y", "lb", "rb", "lt", "rt", "start", "back",
              "dpad_up", "dpad_down", "dpad_left", "dpad_right"});

REXCVAR_DEFINE_STRING(bfbc_mouse_x2, "y", "bfbc input",
                      "Pad action on mouse button 5 (side/forward); same values as bfbc_mouse_middle")
    .allowed({"none", "a", "b", "x", "y", "lb", "rb", "lt", "rt", "start", "back",
              "dpad_up", "dpad_down", "dpad_left", "dpad_right"});

REXCVAR_DEFINE_STRING(bfbc_mouse_wheel, "none", "bfbc input",
                      "Pad action on the mouse wheel (none disables it); same values as "
                      "bfbc_mouse_middle")
    .allowed({"none", "a", "b", "x", "y", "lb", "rb", "lt", "rt", "start", "back",
              "dpad_up", "dpad_down", "dpad_left", "dpad_right"});

REXCVAR_DEFINE_DOUBLE(bfbc_mouse_smooth, 0.2, "bfbc input",
                      "How much of the previous poll's turn rate is kept, so the camera does not "
                      "step once per 30 Hz frame; 0 sends every poll's rate unchanged")
    .range(0.0, 0.9);

REXCVAR_DEFINE_BOOL(bfbc_mouse_dither, false, "bfbc input",
                    "Emit the game's minimum deflection on every Nth poll so aiming can be slower "
                    "than its deadzone allows");

REXCVAR_DEFINE_BOOL(bfbc_mouse_debug, false, "bfbc input",
                    "Log raw and shaped right-stick values while polling");

// Which pad backends to open. "both" (default) runs XInput for Xbox pads and
// SDL for PlayStation pads at once; the SDK's own input_backend cannot express
// that because it only accepts "xinput"/"sdl" and substitutes its default for
// any other value.
REXCVAR_DEFINE_STRING(bfbc_pad_backend, "both", "bfbc input",
                      "Pad backends to open: both (default, XInput + SDL), xinput, or sdl")
    .allowed({"xinput", "sdl", "both"});

// Pad actions for the mouse buttons the SDK driver does not map, and the wheel.
REXCVAR_DEFINE_STRING(bfbc_mouse_middle, "b", "bfbc input",
                      "Pad action on the middle mouse button (none, a, b, x, y, lb, rb, "
                      "lt, rt, start, back, dpad_*, lstick_*, rstick_*)")
    .allowed({"none", "a", "b", "x", "y", "lb", "rb", "lt", "rt", "start", "back",
              "dpad_up", "dpad_down", "dpad_left", "dpad_right"});

namespace bfbc {
namespace {

// X_STATUS_SUCCESS is an unqualified macro; see bfbc_mouse.h.
constexpr rex::X_STATUS kStatusSuccess = 0;

}  // namespace

MouseTelemetry& MouseState() {
  static MouseTelemetry state;
  return state;
}

std::unique_ptr<rex::system::IInputSystem> CreateShapedInputSystem(bool tool_mode) {
  auto input = std::make_unique<rex::input::InputSystem>(nullptr);
  std::string pads = "none";
  bool merge_devices = false;

  if (!tool_mode) {
    // "xinput" and "sdl" pin one backend. "both" opens both, so a PlayStation
    // pad and an Xbox pad work without restarting, and hot-plug still works:
    // InputSystem re-enumerates every driver when devices arrive or leave.
    // (input_backend is not consulted: the runtime validates it against its two
    // values and silently substitutes "sdl" for anything else, so "auto" can
    // never reach this code.)
    const std::string backend = REXCVAR_GET(bfbc_pad_backend);
    const bool want_xinput = backend != "sdl";
    const bool want_sdl = backend != "xinput";
    merge_devices = want_xinput && want_sdl;
#if defined(_WIN32)
    if (want_xinput) {
      auto pad_driver = std::make_unique<rex::input::xinput::XinputInputDriver>(nullptr, 0);
      if (pad_driver->Setup() == kStatusSuccess) {
        input->AddDriver(std::move(pad_driver));
        pads = "xinput";
      } else {
        REXLOG_WARN("Input: XInput driver failed to initialise");
      }
    }
#endif
    if (want_sdl) {
      auto pad_driver = std::make_unique<rex::input::sdl::SDLInputDriver>(nullptr, 0);
      if (pad_driver->Setup() == kStatusSuccess) {
        input->AddDriver(std::move(pad_driver));
        pads = pads == "none" ? "sdl" : pads + "+sdl";
      } else {
        REXLOG_WARN("Input: SDL driver failed to initialise");
      }
    }

    // The shaping driver stands in for the SDK's MnkInputDriver, so the
    // keyboard/mouse device has exactly one owner.
    auto shaped_driver = std::make_unique<MouseShapeDriver>(nullptr, 0);
    if (shaped_driver->Setup() == kStatusSuccess) {
      input->AddDriver(std::move(shaped_driver));
    } else {
      REXLOG_WARN("Input: keyboard/mouse driver failed to initialise");
    }
  }

  // Same ordering as the SDK default: the NOP stand-in sits behind the real
  // drivers so a title always sees a controller.
  const uint8_t nop_index = tool_mode ? 0 : 1;
  input->AddDriver(std::make_unique<rex::input::nop::NopInputDriver>(nullptr, nop_index));
  // With both backends open the same physical pad can be reported twice (SDL
  // sees an Xbox pad through RAWINPUT while XInput sees it too). Feeding every
  // device to one guest user merges those copies instead of leaving a phantom
  // second player, and this title only ever polls user 0. A pinned backend
  // keeps the ordinal-per-user slots.
  if (merge_devices) {
    input->SetDeviceAssignment(std::make_unique<rex::input::SharedAssignment>());
  } else {
    input->SetDeviceAssignment(std::make_unique<rex::input::SlotAssignment>());
  }

  REXLOG_INFO("Input: custom input system built (pads={}{}, keyboard/mouse=shaped{})", pads,
              merge_devices ? ", merged into user 0" : "", tool_mode ? ", tool mode" : "");
  return input;
}

}  // namespace bfbc
