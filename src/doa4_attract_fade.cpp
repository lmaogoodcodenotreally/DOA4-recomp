// Attract mode stuck black after the intro.
//
// sub_82381D10 is the scene-script FADE command: it starts a screen transition
// on record 0 (byte_83047974, state dword_83047978) only when none is running
// (state 2 = held, 4 = done); a command that arrives while one runs is
// dropped and the script moves on. The attract intro ends with a 90-frame
// fade to black that holds (record hold byte 0x8304798E, fade-to-colour flag
// bit 1 of byte_8304790C), and the next scene's script clears it. On the
// console that scene's load always outlasts the fade; here it can be ready
// first, its clear is dropped, the fade then holds black, and the whole demo
// plays out unseen until the next title. A command dropped during a holding
// fade-to-colour is therefore deferred and re-run, unchanged, on the first
// transition tick after that fade completes (sub_823B69A8 ticks the records
// once per frame) - where the console's script would have issued it.

#include "generated/doa4/doa4retail_init.h"

#include <atomic>
#include <cstring>

#include <rex/hook.h>
#include <rex/memory/utils.h>

REX_EXTERN(__imp__sub_82381D10);
REX_EXTERN(__imp__sub_823B69A8);

namespace {

constexpr uint32_t kRecord0State = 0x83047978u;
constexpr uint32_t kRecord0Hold = 0x8304798Eu;
constexpr uint32_t kFadeFlags = 0x8304790Cu;

// The deferred command's first four script halfwords (header, mode, ?, frames).
std::atomic<bool> g_deferred{false};
uint8_t g_command[8];

uint32_t Record0State(uint8_t* base) {
  return rex::memory::load_and_swap<uint32_t>(base + kRecord0State);
}

bool TransitionRunning(uint8_t* base) {
  const uint32_t state = Record0State(base);
  return state != 4 && state != 2 && (base[kFadeFlags] & 3);
}

}  // namespace

REX_HOOK_RAW(sub_82381D10) {
  if (TransitionRunning(base) && (base[kFadeFlags] & 2) && base[kRecord0Hold]) {
    const uint32_t script = rex::memory::load_and_swap<uint32_t>(base + ctx.r3.u32 + 28);
    std::memcpy(g_command, base + script, sizeof(g_command));
    g_deferred.store(true, std::memory_order_release);
  }
  __imp__sub_82381D10(ctx, base);
}

REX_HOOK_RAW(sub_823B69A8) {
  __imp__sub_823B69A8(ctx, base);
  if (!g_deferred.load(std::memory_order_acquire) || TransitionRunning(base)) return;
  g_deferred.store(false, std::memory_order_relaxed);
  // Re-run the command on a stand-in script context below this frame's stack:
  // sub_82381D10 reads its words through ctx+28 and then advances ctx+28/+20.
  const uint32_t sp = ctx.r1.u32;
  const uint64_t ret = ctx.r3.u64;
  const uint32_t frame = sp - 0x80, script_ctx = frame + 0x10, words = frame + 0x40;
  std::memcpy(base + words, g_command, sizeof(g_command));
  rex::memory::store_and_swap<uint32_t>(base + script_ctx + 28, words);
  ctx.r1.u64 = frame;
  ctx.r3.u64 = script_ctx;
  __imp__sub_82381D10(ctx, base);
  ctx.r1.u64 = sp;
  ctx.r3.u64 = ret;
}

