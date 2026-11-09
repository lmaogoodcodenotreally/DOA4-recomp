// skip_intro, disclaimer part (the boot movie part lives in the SDK's
// xfile.cpp, which ends the first .sfd opened).
//
// Boot runs sub_82403518: sub_82403610 (the warning/disclaimer screen) and
// then movie 18 (ninja_vi_hd.sfd). sub_82403610 starts the screen and loops a
// frame at a time until its ~54.6 s timer ends (sub_823A7370) or
// sub_82404AD0(pad) sees A/Start on pad 0-3. With skip_intro on, that press
// check answers "pressed" while the disclaimer runs, so the game takes its
// own button exit before the first frame. sub_82404AD0's only other caller
// is the low-refresh-rate warning (sub_82406458), which is left alone.

#include "generated/doa4/doa4retail_init.h"

#include <rex/cvar.h>
#include <rex/hook.h>

REXCVAR_DECLARE(bool, skip_intro);

REX_EXTERN(__imp__sub_82403610);
REX_EXTERN(__imp__sub_82404AD0);

namespace {
// Only the boot thread runs the disclaimer.
thread_local bool g_in_disclaimer = false;
}  // namespace

REX_HOOK_RAW(sub_82403610) {
  g_in_disclaimer = REXCVAR_GET(skip_intro);
  __imp__sub_82403610(ctx, base);
  g_in_disclaimer = false;
}

REX_HOOK_RAW(sub_82404AD0) {
  if (g_in_disclaimer) {
    ctx.r3.u64 = 1;
    return;
  }
  __imp__sub_82404AD0(ctx, base);
}
