// bfbc - frame pacing and per-frame guest work (see bfbc_frame.h).
//
// The guest swaps by calling the xboxkrnl VdSwap import, which rexruntime
// exports as __imp__VdSwap. Defining that symbol here replaces the runtime's
// copy for the recompiled call sites (the same mechanism bfbc_patches.cpp uses
// for the disc-authentication imports), and the original is reachable through
// the runtime module's export table. That gives a per-frame point on the guest
// render thread, which is what pacing needs: ReXApp has no per-frame callback,
// and the presenter only paces the UI overlay.
//
// How the frame rate actually works (measured, not assumed):
//
//  * With the shipped `Game.MaxFps` unset (the game's own 30) the engine paces
//    itself to a locked 30.0 fps, 33.2-33.5 ms per frame.
//  * `Game.MaxFps 60` changes nothing: the engine's sleep is quantised to its
//    own 30 Hz tick, so 60 lands back on 33 ms.
//  * `Game.MaxFps 0` removes that sleep entirely - the loop then swaps as fast
//    as it can (about 500 fps on the menu), which is far too fast to use.
//
// So the frame rate is not reachable from the engine's settings alone. What is
// reachable is the gap between those two facts: lifting the engine cap and
// pacing the swap here, on the host, at whatever rate is asked for. This pacer
// is what turns `Game.MaxFps 0` into a usable 60 or 120 fps.
//
// Note that this only changes how often the engine steps. Whether the result
// plays at the right speed depends on the simulation advancing by elapsed time,
// which is why the value is a cvar the player can set back to 0 (unpaced) or 30.

#include "bfbc_frame.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#endif

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

// Registered so that bfbc.toml files written by older builds still parse (an
// unknown key there would be reported as an error); superseded by bfbc_pace_fps.
REXCVAR_DEFINE_INT32(bfbc_fps_limit, 0, "bfbc display",
                     "Deprecated and unused: use bfbc_pace_fps")
    .range(0, 480);

REXCVAR_DEFINE_DOUBLE(bfbc_fov_scale, 1.0, "bfbc display",
                      "Scale applied to the loaded scene field of view; 1.0 keeps the game value")
    .range(0.5, 2.0);

REXCVAR_DEFINE_DOUBLE(bfbc_pace_fps, 60.0, "bfbc display",
                      "Frame rate to hold the guest to, in frames per second; 0 disables pacing. "
                      "Needs 'Game.MaxFps 0' in game/dist/xenon/config.zz, otherwise the engine "
                      "keeps its own 30 fps pacing and this does nothing")
    .range(0.0, 480.0);

namespace bfbc {
namespace {

using Clock = std::chrono::steady_clock;

using VdSwapFn = void (*)(PPCContext& ctx, uint8_t* base);

// Resolved once: rexruntime.dll owns the real import implementation.
VdSwapFn ResolveRealVdSwap() {
#if defined(_WIN32)
  HMODULE runtime = GetModuleHandleA("rexruntime.dll");
  if (!runtime) {
    return nullptr;
  }
  return reinterpret_cast<VdSwapFn>(GetProcAddress(runtime, "__imp__VdSwap"));
#else
  return nullptr;
#endif
}

// --- Pacing -----------------------------------------------------------------
//
// The deadline is a running accumulator, not "now + period", so the cost of
// waking up does not accumulate into drift and every frame gets the same slot.
// If the guest is more than a frame late (a load, a hitch) the accumulator
// restarts from now instead of emitting a burst of catch-up frames, which would
// look worse than the hitch itself.
Clock::time_point pace_deadline_{};
double pace_period_ = 0.0;
bool pace_armed_ = false;

double PacePeriodSeconds() {
  const double fps = REXCVAR_GET(bfbc_pace_fps);
  if (!(fps >= 1.0)) {
    return 0.0;
  }
  return 1.0 / fps;
}

#if defined(_WIN32)
// A waitable timer created with CREATE_WAITABLE_TIMER_HIGH_RESOLUTION resolves
// to well under a millisecond, unlike the default 15.6 ms clock tick, so the
// sleep below actually ends where it is asked to. Returns null on systems that
// do not support the flag; the caller then falls back to a spin.
HANDLE PaceTimer() {
  static HANDLE timer = CreateWaitableTimerExW(
      nullptr, nullptr,
      CREATE_WAITABLE_TIMER_HIGH_RESOLUTION | CREATE_WAITABLE_TIMER_MANUAL_RESET,
      TIMER_ALL_ACCESS);
  return timer;
}
#endif

void SleepUntil(Clock::time_point deadline) {
#if defined(_WIN32)
  const HANDLE timer = PaceTimer();
  for (;;) {
    const auto remaining = deadline - Clock::now();
    if (remaining <= Clock::duration::zero()) {
      return;
    }
    const auto remaining_us =
        std::chrono::duration_cast<std::chrono::microseconds>(remaining).count();
    if (timer != nullptr && remaining_us > 300) {
      // Leave the last 300 us to the spin: the timer can overshoot.
      LARGE_INTEGER due;
      due.QuadPart = -(static_cast<LONGLONG>(remaining_us - 300) * 10);  // 100 ns units
      if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
        WaitForSingleObject(timer, static_cast<DWORD>(remaining_us / 1000 + 20));
        continue;
      }
    }
    std::this_thread::yield();
  }
#else
  std::this_thread::sleep_until(deadline);
#endif
}

void PaceFrame() {
  const double period_seconds = PacePeriodSeconds();
  if (period_seconds <= 0.0) {
    pace_armed_ = false;
    return;
  }
  const auto period = std::chrono::duration_cast<Clock::duration>(
      std::chrono::duration<double>(period_seconds));
  const auto now = Clock::now();
  if (!pace_armed_ || std::fabs(period_seconds - pace_period_) > 1e-9) {
    pace_period_ = period_seconds;
    pace_deadline_ = now + period;
    pace_armed_ = true;
  } else {
    pace_deadline_ += period;
    if (pace_deadline_ + period < now) {
      pace_deadline_ = now + period;
    }
  }
  if (now < pace_deadline_) {
    SleepUntil(pace_deadline_);
  }
}

// --- Diagnostics ------------------------------------------------------------
//
// The guest swap happens once per presented frame, so counting the calls gives
// the real frame rate of the emulated game (the engine's own perf overlay is not
// compiled into the retail build). The gap between swaps is logged with the rate
// because it is what actually decides whether the picture looks smooth: a rate
// that never dips can still stutter if individual frames are unevenly spaced.
//
// An older version of this counter reported frames / (time since the window's
// first frame) without excluding that frame, so it over-reported by one frame
// per window and logged a locked 30.0 fps engine as 30.5. That is where the
// "the engine runs at 30.5 Hz" figure in the project notes came from.
Clock::time_point rate_window_{};
Clock::time_point last_swap_{};
uint32_t rate_frames_ = 0;
bool gap_valid_ = false;
double gap_min_ms_ = 0.0;
double gap_max_ms_ = 0.0;

void SampleFrameRate() {
  const auto now = Clock::now();

  if (rate_frames_ == 0) {
    rate_window_ = now;
    last_swap_ = now;
    rate_frames_ = 1;
    gap_valid_ = false;
    return;
  }

  const double gap_ms = std::chrono::duration<double, std::milli>(now - last_swap_).count();
  if (!gap_valid_) {
    gap_min_ms_ = gap_max_ms_ = gap_ms;
    gap_valid_ = true;
  } else {
    gap_min_ms_ = std::min(gap_min_ms_, gap_ms);
    gap_max_ms_ = std::max(gap_max_ms_, gap_ms);
  }
  last_swap_ = now;
  ++rate_frames_;

  const auto elapsed = std::chrono::duration<double>(now - rate_window_).count();
  if (elapsed >= 2.0) {
    REXLOG_INFO("Frame: {:.1f} fps (cap {:.0f}, gap {:.1f}-{:.1f} ms)", (rate_frames_ - 1) / elapsed,
                REXCVAR_GET(bfbc_pace_fps), gap_min_ms_, gap_max_ms_);
    rate_frames_ = 0;
  }
}

}  // namespace

void OnGuestFrame() {
  PaceFrame();
  SampleFrameRate();
}

}  // namespace bfbc

// The guest's per-frame swap. Runs the runtime's implementation first so
// presentation and the frame's timing behave exactly as before, then applies
// the app's own per-frame work.
extern "C" void __imp__VdSwap(PPCContext& ctx, uint8_t* base) {
  static const bfbc::VdSwapFn real = bfbc::ResolveRealVdSwap();
  if (!real) {
    static bool logged = false;
    if (!logged) {
      logged = true;
      REXLOG_ERROR("Frame: rexruntime does not export VdSwap, presentation is disabled");
    }
    return;
  }
  real(ctx, base);
  bfbc::OnGuestFrame();
}
