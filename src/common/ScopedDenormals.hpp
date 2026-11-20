#pragma once
#include <xmmintrin.h>
#include <pmmintrin.h>

namespace Fraudize {

// RAII helper to enable Flush-To-Zero (FTZ) and Denormals-Are-Zero (DAZ)
// Prevents CPU core throttling and overheating on decaying audio tails
struct ScopedDenormalsZero {
    unsigned int oldMxcsr;
    ScopedDenormalsZero() {
        oldMxcsr = _mm_getcsr();
        // Bit 15: Flush-To-Zero (FTZ)
        // Bit 6:  Denormals-Are-Zero (DAZ)
        _mm_setcsr(oldMxcsr | 0x8040);
    }
    ~ScopedDenormalsZero() {
        _mm_setcsr(oldMxcsr);
    }
};

} // namespace Fraudize

