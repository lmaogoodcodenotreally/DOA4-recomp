// Preview camera-cut hook (no-op overlay skip). Design: docs/hooks.md.

#include "generated/doa4/doa4retail_init.h"

#include <rex/hook.h>

REX_HOOK_RAW(sub_823B7890) {
  (void)ctx;
  (void)base;
}
