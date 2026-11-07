// bfbc - per-frame guest work (frame rate measurement, FOV patch).
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

// Scale applied to the loaded scene field of view; 1.0 leaves it untouched.
REXCVAR_DECLARE(double, bfbc_fov_scale);

namespace bfbc {

// Called from the VdSwap wrapper after the real import has run: logs the
// measured guest frame rate and applies the FOV patch.
void OnGuestFrame();

}  // namespace bfbc
