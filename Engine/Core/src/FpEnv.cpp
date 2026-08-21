// pme/core/FpEnv.cpp — 설계 문서 §22.2
#include "pme/core/FpEnv.h"

#include <cfenv>
#if defined(_MSC_VER)
#  include <float.h>
#endif
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
#  include <xmmintrin.h>   // _mm_getcsr / _mm_setcsr (MXCSR: FTZ bit 15, DAZ bit 6)
#  define PME_HAS_MXCSR 1
#endif

namespace pme {

Json FpEnvStatus::toJson() const {
    return Json{{"roundToNearest", roundToNearest}, {"flushToZero", flushToZero}, {"denormalsAreZero", denormalsAreZero}, {"ok", ok()}};
}

FpEnvStatus fpEnvStatus() {
    FpEnvStatus s;
    s.roundToNearest = (std::fegetround() == FE_TONEAREST);
#ifdef PME_HAS_MXCSR
    unsigned csr = _mm_getcsr();
    s.flushToZero = (csr & 0x8000u) != 0;
    s.denormalsAreZero = (csr & 0x0040u) != 0;
#endif
    return s;
}

FpEnvStatus normalizeFpEnv() {
    std::fesetround(FE_TONEAREST);
#ifdef PME_HAS_MXCSR
    unsigned csr = _mm_getcsr();
    csr &= ~0x8000u; // FTZ off
    csr &= ~0x0040u; // DAZ off
    csr = (csr & ~0x6000u); // rounding control bits 13-14 = 00 (nearest)
    _mm_setcsr(csr);
#endif
#if defined(_MSC_VER)
    unsigned int current = 0;
    _controlfp_s(&current, _RC_NEAR, _MCW_RC);
    _controlfp_s(&current, _DN_SAVE, _MCW_DN);
#endif
    return fpEnvStatus();
}

} // namespace pme
