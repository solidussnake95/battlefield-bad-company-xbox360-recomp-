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

REXCVAR_DEFINE_DOUBLE(bfbc_mouse_smooth, 0.2, "bfbc input",
                      "How much of the previous poll's turn rate is kept, so the camera does not "
                      "step once per 30 Hz frame; 0 sends every poll's rate unchanged")
    .range(0.0, 0.9);

REXCVAR_DEFINE_BOOL(bfbc_mouse_dither, false, "bfbc input",
                    "Emit the game's minimum deflection on every Nth poll so aiming can be slower "
                    "than its deadzone allows");

REXCVAR_DEFINE_BOOL(bfbc_mouse_debug, false, "bfbc input",
                    "Log raw and shaped right-stick values while polling");

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

  if (!tool_mode) {
#if defined(_WIN32)
    if (REXCVAR_GET(input_backend) == "xinput") {
      auto pad_driver = std::make_unique<rex::input::xinput::XinputInputDriver>(nullptr, 0);
      if (pad_driver->Setup() == kStatusSuccess) {
        input->AddDriver(std::move(pad_driver));
        pads = "xinput";
      } else {
        REXLOG_WARN("Input: XInput driver failed to initialise");
      }
    }
#endif
    if (REXCVAR_GET(input_backend) == "sdl") {
      auto pad_driver = std::make_unique<rex::input::sdl::SDLInputDriver>(nullptr, 0);
      if (pad_driver->Setup() == kStatusSuccess) {
        input->AddDriver(std::move(pad_driver));
        pads = "sdl";
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
  input->SetDeviceAssignment(std::make_unique<rex::input::SlotAssignment>());

  REXLOG_INFO("Input: custom input system built (pads={}, keyboard/mouse=shaped{})", pads,
              tool_mode ? ", tool mode" : "");
  return input;
}

}  // namespace bfbc
