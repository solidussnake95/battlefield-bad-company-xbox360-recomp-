// bfbc - frame pacing and per-frame guest patches.
//
// The host presents when the guest swaps, and the guest calls the xboxkrnl
// VdSwap import exactly once per presented frame. Wrapping that import (see
// bfbc_frame.cpp) gives the app a real per-frame point on the guest thread:
// there is no per-frame callback in ReXApp, and the presenter paces only the
// UI overlay, not the guest.
//
// The same point is used for the field-of-view patch, which has to run while
// the guest render thread is inside the swap rather than from the UI thread.

#pragma once

#include <cstdint>

#include <rex/cvar.h>

// Frame rate cap in frames per second; 0 leaves the guest uncapped.
REXCVAR_DECLARE(int32_t, bfbc_fps_limit);

// Scale applied to the loaded scene field of view; 1.0 leaves it untouched.
REXCVAR_DECLARE(double, bfbc_fov_scale);

namespace bfbc {

// Called from the VdSwap wrapper after the real import has run: sleeps out the
// rest of the frame when a cap is active and applies the FOV patch.
void OnGuestFrame();

}  // namespace bfbc
