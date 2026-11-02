// bfbc - application-side fixes for gaps in the rexglue SDK.
//
// rexglue-sdk 0.10.0 ships XeKeysConsolePrivateKeySign and
// XeKeysConsoleSignatureVerification as stubs that return 0 (FALSE). A title
// reads 0 as "the signature is invalid", so it takes an error path instead of
// the licence/disc-authentication path it intended. Battlefield: Bad Company
// runs its disc authentication through these imports and then aborts on an
// internal assertion (`KeBugCheck(0)` via rex::debug::Break) because the
// "signature" was rejected.
//
// The behaviour below mirrors Xenia (xboxkrnl_crypt.cc): fill a plausible
// retail XE_CONSOLE_CERTIFICATE into the output and report success. Defining the
// import symbols here overrides the ones rexruntime.dll exports.
//
// Upstream tracking: https://github.com/rexglue/rexglue-sdk/issues/451

#include <cstdint>
#include <cstring>

#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/types.h>

namespace {

// Field offsets within the guest's XE_CONSOLE_CERTIFICATE (Xenia layout).
constexpr uint32_t kCertConsoleId = 0x02;         // 5 bytes
constexpr uint32_t kCertConsoleType = 0x18;       // big-endian u32
constexpr uint32_t kCertManufactureDate = 0x1C;   // 8 bytes
constexpr uint32_t kCertSize = 0x1A8;

constexpr uint32_t kConsoleTypeRetail = 2;

}  // namespace

extern "C" void __imp__XeKeysConsolePrivateKeySign(PPCContext& ctx, uint8_t* base) {
  const uint32_t hash = ctx.r3.u32;
  const uint32_t out = ctx.r4.u32;
  if (hash == 0 || out == 0) {
    ctx.r3.u64 = 0;
    return;
  }

  uint8_t* cert = base + out;
  std::memset(cert, 0, kCertSize);

  // console_id: the five bytes Xenia builds for a retail console.
  cert[kCertConsoleId + 0] = 0x93;
  cert[kCertConsoleId + 1] = 0x01;
  cert[kCertConsoleId + 2] = 0x64;
  cert[kCertConsoleId + 3] = 0xE6;
  cert[kCertConsoleId + 4] = 0x07;

  // console_type, big-endian.
  cert[kCertConsoleType + 3] = static_cast<uint8_t>(kConsoleTypeRetail);

  // manufacture_date.
  cert[kCertManufactureDate + 0] = 2;
  cert[kCertManufactureDate + 1] = 0;
  cert[kCertManufactureDate + 2] = 0;
  cert[kCertManufactureDate + 3] = 5;
  cert[kCertManufactureDate + 4] = 1;
  cert[kCertManufactureDate + 5] = 1;
  cert[kCertManufactureDate + 6] = 2;
  cert[kCertManufactureDate + 7] = 2;

  REXKRNL_INFO("XeKeysConsolePrivateKeySign -> success (app override)");
  ctx.r3.u64 = 1;
}

extern "C" void __imp__XeKeysConsoleSignatureVerification(PPCContext& ctx, uint8_t* base) {
  (void)base;
  REXKRNL_INFO("XeKeysConsoleSignatureVerification -> valid (app override)");
  ctx.r3.u64 = 1;
}
