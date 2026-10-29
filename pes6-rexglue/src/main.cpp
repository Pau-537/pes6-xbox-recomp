// pes6 - ReXGlue Recompiled Project

#include "generated/default/pes6_init.h"

#include "pes6_app.h"

void ClampWait(PPCRegister& r8) {
    if (r8.s32 > 1) r8.u64 = 1;   // present every vblank (60 Hz) instead of every second one
}

REX_DEFINE_APP(pes6, Pes6App::Create)
