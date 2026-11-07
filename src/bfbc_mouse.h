// bfbc - mouse response shaping on top of the SDK's keyboard/mouse pad emulation.
//
// The title only ever reads an analog stick, so a true raw-mouse path would mean
// patching its look code. What *is* controllable is the stick signal the SDK's
// MnkInputDriver produces: how much deflection one mouse count buys (gain), how
// the response curves away from the centre, and how much travel is dead. This
// driver wraps the SDK driver and rewrites the right stick on the way out, so it
// works without touching guest code and stays live while the game runs.
//
// It has to be the *only* driver exposing the keyboard/mouse device, otherwise
// the runtime resolves the device handle to the SDK's unwrapped driver and the
// shaping never runs. CreateShapedInputSystem() below is therefore installed
// through RuntimeConfig::input_factory: it registers this driver in place of the
// SDK's MnkInputDriver instead of next to it.

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <rex/cvar.h>
#include <rex/input/input_driver.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/logging.h>
#include <rex/system/interfaces/input.h>
#include <rex/ui/window.h>

// Defined in src/bfbc_mouse.cpp.
REXCVAR_DECLARE(double, bfbc_mouse_gain);
REXCVAR_DECLARE(double, bfbc_mouse_vertical);
REXCVAR_DECLARE(double, bfbc_mouse_curve);
REXCVAR_DECLARE(double, bfbc_mouse_deadzone);
REXCVAR_DECLARE(double, bfbc_mouse_floor);
REXCVAR_DECLARE(bool, bfbc_mouse_time_normalize);
REXCVAR_DECLARE(double, bfbc_mouse_smooth);
REXCVAR_DECLARE(bool, bfbc_mouse_dither);
REXCVAR_DECLARE(bool, bfbc_mouse_debug);

namespace bfbc {

// X_RESULT_SUCCESS / X_STATUS_SUCCESS are macros that expand to the unqualified
// types, so they only compile inside namespace rex. Use these instead.
inline constexpr rex::X_RESULT kResultSuccess = 0;

// Last poll's numbers, written by the driver and read by the F5 menu so the
// mouse can be tuned while looking at what it is actually sending.
struct MouseTelemetry {
  std::atomic<int32_t> raw_x{0};
  std::atomic<int32_t> raw_y{0};
  std::atomic<int32_t> out_x{0};
  std::atomic<int32_t> out_y{0};
  std::atomic<int32_t> polls{0};
};

// Defined in src/bfbc_mouse.cpp.
MouseTelemetry& MouseState();

class MouseShapeDriver final : public rex::input::InputDriver {
 public:
  MouseShapeDriver(rex::ui::Window* window, size_t window_z_order)
      : rex::input::InputDriver(window, window_z_order),
        inner_(window, window_z_order) {}

  rex::X_STATUS Setup() override { return inner_.Setup(); }

  void EnumerateDevices(std::vector<rex::input::DeviceInfo>& out) override {
    inner_.EnumerateDevices(out);
  }

  rex::X_RESULT GetDeviceState(rex::input::DeviceId id,
                               rex::input::X_INPUT_STATE* out_state) override {
    // The SDK driver gates itself on is_active(); the wrapped instance cannot,
    // because InputSystem hands the callback to this driver, not to it. Mirror
    // the gate here so an overlay that captures input still sees the game idle.
    if (!is_active()) {
      if (out_state) {
        std::memset(out_state, 0, sizeof(*out_state));
      }
      return kResultSuccess;
    }

    const rex::X_RESULT result = inner_.GetDeviceState(id, out_state);
    if (out_state) {
      ShapeRightStick(out_state);
    }
    return result;
  }

  rex::X_RESULT GetDeviceCapabilities(rex::input::DeviceId id, uint32_t flags,
                                      rex::input::X_INPUT_CAPABILITIES* out_caps) override {
    return inner_.GetDeviceCapabilities(id, flags, out_caps);
  }

  rex::X_RESULT SetDeviceVibration(rex::input::DeviceId id,
                                   rex::input::X_INPUT_VIBRATION* vibration) override {
    return inner_.SetDeviceVibration(id, vibration);
  }

  rex::X_RESULT GetDeviceKeystroke(rex::input::DeviceId id, uint32_t flags,
                                   rex::input::X_INPUT_KEYSTROKE* out_keystroke) override {
    return inner_.GetDeviceKeystroke(id, flags, out_keystroke);
  }

  void OnWindowAvailable(rex::ui::Window* window) override { inner_.OnWindowAvailable(window); }

 private:
  static constexpr float kFullScale = 32767.0f;
  // Below this the decayed rate is indistinguishable from the pad's centre, so
  // the stick is released instead of leaving a permanent slow drift.
  static constexpr float kSmoothCutoff = 0.02f;

  // The guest does not turn by the stick value itself, it turns at a speed
  // derived from how far the stick is from centre - and it ignores everything
  // inside its own deadzone, which the runtime advertises as the XInput
  // threshold (7849 of 32767). Passing the frame's mouse delta straight through
  // therefore left a hole: slow aiming produced no movement at all, and a flick
  // jumped from nothing to fast.
  //
  // The shaped value is the turn *rate* the guest should use this frame, so it
  // has to follow mouse speed, not mouse distance. `value` is the per-poll
  // deflection, already normalised by the poll interval; `gain` converts it to
  // the [0, 1] range the stick covers. Anything the guest would round off as
  // "inside the deadzone" is lifted to `floor`, and with dithering enabled a
  // rate below that floor is emitted every Nth poll instead, which is the only
  // way to aim slower than the game's own deadzone allows.
  static float ShapeAxis(float value, float gain, float curve, float deadzone, float floor,
                         float& residual, bool dither) {
    if (value == 0.0f) {
      residual = 0.0f;
      return 0.0f;
    }
    const float magnitude = std::fabs(value);
    if (magnitude <= deadzone) {
      residual = 0.0f;
      return 0.0f;
    }
    const float span = 1.0f - deadzone;
    float t = (span > 0.0f) ? (magnitude - deadzone) / span : magnitude;
    t = std::min(1.0f, t * gain);
    if (curve != 1.0f) {
      t = std::pow(t, curve);
    }
    if (t <= 0.0f) {
      residual = 0.0f;
      return 0.0f;
    }
    if (floor <= 0.0f || t >= floor) {
      residual = 0.0f;
      // Same mapping the layer always used above the game's deadzone: the
      // deflection spans [floor, 1] so a movement never falls into the hole.
      return std::copysign(std::min(floor + (1.0f - floor) * t, 1.0f), value);
    }
    if (!dither) {
      return std::copysign(floor, value);
    }
    // Below the deadzone: accumulate the wanted rate and spend it in whole
    // floor-sized steps, so the average turn rate follows the mouse instead of
    // being pinned at the floor.
    residual += t;
    if (residual >= floor) {
      residual -= floor;
      return std::copysign(floor, value);
    }
    return 0.0f;
  }

  // The guest polls once per frame, so the same physical mouse speed arrives as
  // a smaller delta at a higher frame rate and would turn slower. Scaling by the
  // measured poll interval makes the turn rate depend on how fast the mouse is
  // moving, not on the frame rate. Clamped so a hitch cannot become a snap.
  static float PollIntervalScale() {
    static auto last = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - last).count();
    last = now;
    if (!(dt > 0.0f)) {
      return 1.0f;
    }
    return std::clamp(dt, 1.0f / 240.0f, 1.0f / 20.0f) * 60.0f;
  }

  void ShapeRightStick(rex::input::X_INPUT_STATE* state) {
    // Proves on the first poll that this driver, not the SDK's, owns the
    // keyboard/mouse device - the reason the shaping used to never run.
    static std::atomic<bool> announced{false};
    if (!announced.exchange(true, std::memory_order_relaxed)) {
      REXLOG_INFO("MouseShape: right-stick shaping is live (gain={} curve={} dz={} smooth={})",
                  REXCVAR_GET(bfbc_mouse_gain), REXCVAR_GET(bfbc_mouse_curve),
                  REXCVAR_GET(bfbc_mouse_deadzone), REXCVAR_GET(bfbc_mouse_smooth));
    }

    const float gain = static_cast<float>(std::clamp(REXCVAR_GET(bfbc_mouse_gain), 0.05, 64.0));
    const float curve = static_cast<float>(std::clamp(REXCVAR_GET(bfbc_mouse_curve), 0.2, 4.0));
    const float deadzone =
        static_cast<float>(std::clamp(REXCVAR_GET(bfbc_mouse_deadzone), 0.0, 0.9));
    // Never emit a deflection the guest would round off as "inside the deadzone".
    const float floor =
        static_cast<float>(std::clamp(REXCVAR_GET(bfbc_mouse_floor) / kFullScale, 0.0, 0.9));
    const bool dither = REXCVAR_GET(bfbc_mouse_dither);
    const bool debug = REXCVAR_GET(bfbc_mouse_debug);

    const int16_t raw_x = static_cast<int16_t>(state->gamepad.thumb_rx);
    const int16_t raw_y = static_cast<int16_t>(state->gamepad.thumb_ry);

    static float residual_x = 0.0f;
    static float residual_y = 0.0f;
    // The shaped value is a turn rate, and the guest applies it once per frame,
    // so a mouse that accelerates within one 33 ms frame turns the camera in a
    // single visible step. Carrying part of the previous rate into this frame
    // spreads that step over two or three frames; when the mouse stops the held
    // rate decays instead of snapping the stick to centre, which is the other
    // half of the stepping artefact.
    static float held_x = 0.0f;
    static float held_y = 0.0f;

    int16_t out_x = raw_x;
    int16_t out_y = raw_y;
    float wanted_x = 0.0f;
    float wanted_y = 0.0f;
    if (raw_x != 0 || raw_y != 0) {
      // Fold the poll interval out so a fast frame rate does not soften the aim.
      float scale = 1.0f;
      if (REXCVAR_GET(bfbc_mouse_time_normalize)) {
        scale = PollIntervalScale();
      }

      const float shaped_x =
          ShapeAxis(static_cast<float>(raw_x) / (kFullScale * scale), gain, curve, deadzone,
                    floor, residual_x, dither);
      const float shaped_y =
          ShapeAxis(static_cast<float>(raw_y) / (kFullScale * scale), gain, curve, deadzone,
                    floor, residual_y, dither);
      // The game turns noticeably slower per stick unit on the vertical axis, so
      // the vertical weight multiplies the result instead of the gain: a gain
      // change would mostly be swallowed by the deadzone floor above, which is
      // why an even horizontal/vertical gain never fixed the up/down weight.
      const float vertical =
          static_cast<float>(std::clamp(REXCVAR_GET(bfbc_mouse_vertical), 0.1, 8.0));
      wanted_x = shaped_x;
      wanted_y = std::clamp(shaped_y * vertical, -1.0f, 1.0f);
    }

    // 0 disables the smoothing entirely, so the raw per-poll rate is what the
    // old behaviour sent.
    const float keep = static_cast<float>(std::clamp(REXCVAR_GET(bfbc_mouse_smooth), 0.0, 0.9));
    if (keep > 0.0f) {
      held_x = keep * held_x + (1.0f - keep) * wanted_x;
      held_y = keep * held_y + (1.0f - keep) * wanted_y;
      if (std::fabs(held_x) < kSmoothCutoff) {
        held_x = 0.0f;
      }
      if (std::fabs(held_y) < kSmoothCutoff) {
        held_y = 0.0f;
      }
      wanted_x = held_x;
      wanted_y = held_y;
    } else {
      held_x = 0.0f;
      held_y = 0.0f;
    }

    if (wanted_x != 0.0f || wanted_y != 0.0f) {
      out_x = static_cast<int16_t>(std::lround(std::clamp(wanted_x, -1.0f, 1.0f) * kFullScale));
      out_y = static_cast<int16_t>(std::lround(std::clamp(wanted_y, -1.0f, 1.0f) * kFullScale));
      state->gamepad.thumb_rx = out_x;
      state->gamepad.thumb_ry = out_y;
    }

    // Publish for the F5 menu (it runs on the UI thread).
    MouseTelemetry& telemetry = MouseState();
    telemetry.raw_x.store(raw_x, std::memory_order_relaxed);
    telemetry.raw_y.store(raw_y, std::memory_order_relaxed);
    telemetry.out_x.store(out_x, std::memory_order_relaxed);
    telemetry.out_y.store(out_y, std::memory_order_relaxed);
    telemetry.polls.fetch_add(1, std::memory_order_relaxed);

    if (debug) {
      // Throttled: the guest polls the pad every frame, and the point is to see
      // how a given mouse movement comes out, not to log thousands of lines.
      static auto last_log = std::chrono::steady_clock::now();
      const auto now = std::chrono::steady_clock::now();
      if (now - last_log >= std::chrono::milliseconds(100)) {
        last_log = now;
        // Triggers and buttons are logged too: they prove a mouse button made it
        // through the driver into the pad state the guest reads.
        REXLOG_INFO(
            "MouseShape: raw=({},{}) shaped=({},{}) lt={} rt={} buttons=0x{:04X} "
            "gain={:.2f} curve={:.2f} dz={:.2f}",
            static_cast<int>(raw_x), static_cast<int>(raw_y), static_cast<int>(out_x),
            static_cast<int>(out_y), static_cast<unsigned>(state->gamepad.left_trigger),
            static_cast<unsigned>(state->gamepad.right_trigger),
            static_cast<unsigned>(state->gamepad.buttons), gain, curve, deadzone);
      }
    }
  }

  rex::input::mnk::MnkInputDriver inner_;
};

// Builds the runtime's input system the way rex::input::CreateDefaultInputSystem
// does - SDL or XInput pads, then the NOP fallback - but registers
// MouseShapeDriver where the SDK would register its bare MnkInputDriver.
//
// Install through RuntimeConfig::input_factory in OnPreSetup. Two keyboard/mouse
// drivers cannot coexist: MnkInputDriver uses one constant device handle, and
// InputSystem::DriverForDevice() resolves that handle to the first driver that
// reported it, so the second instance's GetDeviceState() is never reached.
std::unique_ptr<rex::system::IInputSystem> CreateShapedInputSystem(bool tool_mode);

}  // namespace bfbc
