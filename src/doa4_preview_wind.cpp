// Stage-select preview wind hook. Design: docs/hooks.md.

#include "generated/doa4/doa4retail_init.h"

#include <rex/hook.h>
#include <rex/memory/utils.h>

REX_EXTERN(__imp__sub_82566E80);

namespace {

constexpr uint32_t kGameMode = 0x830318FBu;        // byte_830318FB, 3 = stage-select preview
constexpr uint32_t kActivatedStage = 0x82E76830u;  // dword_82E76830
constexpr uint32_t kStageCursor = 0x83035CC4u;     // stagesel_cursor_a[2]
constexpr uint32_t kSideCurrent = 0x82FCC967u;     // stage_side_current

}  // namespace

// sub_82566E80 (stage activation) runs the wind setup sub_8257E840 for every
// mode but 3, so a previewed stage keeps the title stage's wind (KYO's fast
// swing makes the MK awnings rock). Run the setup as a fight does.
REX_HOOK_RAW(sub_82566E80) {
  __imp__sub_82566E80(ctx, base);
  if (base[kGameMode] != 3) return;
  // sub_8257E840 picks the stage by cursor; only run it when that is this stage.
  const uint32_t stage = rex::memory::load_and_swap<uint32_t>(base + kActivatedStage);
  if (base[kStageCursor + (base[kSideCurrent] & 1)] != stage) return;
  const uint64_t ret = ctx.r3.u64;
  sub_8257E840(ctx, base);
  ctx.r3.u64 = ret;
}
