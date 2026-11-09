// Skip-intro disclaimer hook. Design: docs/hooks.md.

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
