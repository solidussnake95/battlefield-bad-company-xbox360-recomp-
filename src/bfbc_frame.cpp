// bfbc - frame rate cap and per-frame guest patching (see bfbc_frame.h).
//
// The guest swaps by calling the xboxkrnl VdSwap import, which rexruntime
// exports as __imp__VdSwap. Defining that symbol here replaces the runtime's
// copy for the recompiled call sites (the same mechanism bfbc_patches.cpp uses
// for the disc-authentication imports), and the original is reachable through
// the runtime module's export table. That gives a per-frame point on the guest
// render thread, which is what a frame limiter needs: ReXApp has no per-frame
// callback, and the presenter only paces the UI overlay.

#include "bfbc_frame.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#endif

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

REXCVAR_DEFINE_INT32(bfbc_fps_limit, 0, "bfbc display",
                     "Cap the guest frame rate in frames per second; 0 leaves it uncapped")
    .range(0, 480);

REXCVAR_DEFINE_DOUBLE(bfbc_fov_scale, 1.0, "bfbc display",
                      "Scale applied to the loaded scene field of view; 1.0 keeps the game value")
    .range(0.5, 2.0);

namespace bfbc {
namespace {

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

std::chrono::steady_clock::time_point next_frame_{};
bool have_schedule_ = false;

// Sleeps out whatever is left of the frame so the guest runs at the requested
// rate. The schedule is advanced by one interval per presented frame, so slow
// frames are not compensated by a burst of fast ones; a session that falls far
// behind resyncs instead of trying to catch up.
void PaceFrame() {
  const int32_t limit = REXCVAR_GET(bfbc_fps_limit);
  if (limit <= 0) {
    have_schedule_ = false;
    return;
  }

  const auto interval = std::chrono::microseconds(1000000 / limit);
  const auto now = std::chrono::steady_clock::now();
  if (!have_schedule_) {
    next_frame_ = now + interval;
    have_schedule_ = true;
    return;
  }

  next_frame_ += interval;
  if (next_frame_ > now) {
    // Sleep the bulk of the wait, then busy-wait the last ~2ms so the cap
    // lands within the requested interval despite the 15 ms default timer
    // resolution on Windows.
    const auto coarse_deadline = next_frame_ - std::chrono::milliseconds(2);
    if (coarse_deadline > now) {
      std::this_thread::sleep_until(coarse_deadline);
    }
    while (std::chrono::steady_clock::now() < next_frame_) {
      std::this_thread::yield();
    }
  } else if (now - next_frame_ > interval * 2) {
    next_frame_ = now;
  }
}

}  // namespace

void OnGuestFrame() {
  PaceFrame();
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
