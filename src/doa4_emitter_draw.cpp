// Emitter draw race fix: a child that dies in the frame's update still shows in
// the frame that draws the list it was alive in. Design: docs/hooks.md,
// "Emitter draw race".

#include "generated/doa4/doa4retail_init.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>

#include <rex/hook.h>
#include <rex/logging/macros.h>
#include <rex/memory/utils.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#ifndef DOA4_LEAN
#define DOA4_LEAN 0
#endif

REX_EXTERN(__imp__sub_82545D70);

namespace {

// Emitter group (vtable 0x8229E29C): +104 the list the update fills next,
// +108 the list the draw shows next, +148/+152 the two lists, +156/+160 their
// last index (-1 = empty).
constexpr uint32_t kDrawIndex = 108, kLists = 148, kLast = 156;
// Child base accessors: vtable+48 returns the state word at +0x5C (0 = dead).
constexpr uint32_t kAliveSlot = 48, kAliveGetter = 0x826398E8u, kState = 0x5C;
// The child classes seen dying under a drawn list in attract (MK's cart and
// two more). 0x140 covers each: the stride of 0x8229E690's static instances,
// and past the base fields the others' methods use (+0x130 at most).
constexpr uint32_t kClasses[] = {0x8229E690u, 0x8229E480u, 0x8229E530u};
constexpr uint32_t kSlotSize = 0x140, kPerDraw = 8, kDepths = 4;

// The copy reads kSlotSize bytes from the child: only where all of it is mapped.
bool Readable(uint32_t a) {
  auto* heap = rex::system::kernel_memory()->LookupHeap(a + kSlotSize - 1);
  uint32_t protect = 0;
  return heap && heap->QueryProtect(a + kSlotSize - 1, &protect) &&
         (protect & rex::memory::kMemoryProtectRead);
}

uint32_t g_scratch = 0;  // kPerDraw guest copies per nesting depth
std::recursive_mutex g_mu;
thread_local uint32_t g_depth = 0;  // a child's draw may draw a group too

uint32_t Load(uint8_t* base, uint32_t a) { return rex::memory::load_and_swap<uint32_t>(base + a); }

bool Cas(uint8_t* base, uint32_t a, uint32_t from, uint32_t to) {
  // Big-endian words: compare and store byte-swapped.
  uint32_t expected = std::byteswap(from);
  return std::atomic_ref<uint32_t>(*reinterpret_cast<uint32_t*>(base + a))
      .compare_exchange_strong(expected, std::byteswap(to));
}

#if !DOA4_LEAN
// DOA4_EMITTER_LOG=1: each vtable found dead under a drawn list, once; an
// "unhandled" one is a class kClasses lacks. Called under g_mu.
std::map<uint32_t, uint64_t> g_seen;
void Note(uint32_t vt, bool known) {
  static const bool on = [] {
    const char* v = std::getenv("DOA4_EMITTER_LOG");
    return v && v[0] == '1';
  }();
  if (!on) return;
  if (g_seen[vt]++ == 0) REXLOG_INFO("DOA4 emitter: dead child under a drawn list, vtable {:08X} ({})", vt, known ? "handled" : "unhandled");
}
#else
void Note(uint32_t, bool) {}
#endif

}  // namespace

// The update (on a worker) fills the list the next draw does not show and can
// kill a child the draw is about to show, the last list's. If the draw runs
// first the child shows once more and its replacement (the market cart's
// pieces) follows; if the update wins, that frame shows neither. Each such
// child is drawn from a copy that is still alive, the list pointing at the copy
// for the draw only: the game's own state is never written.
REX_HOOK_RAW(sub_82545D70) {
  const uint32_t g = ctx.r3.u32;
  const uint32_t r = Load(base, g + kDrawIndex) & 1;
  const int32_t last = int32_t(Load(base, g + kLast + 4 * r));
  const uint32_t list = Load(base, g + kLists + 4 * r);
  struct Swap {
    uint32_t at, child, copy;
  } swaps[kPerDraw];
  uint32_t n = 0;
  std::unique_lock lk(g_mu, std::defer_lock);
  for (int32_t i = 0; list && i <= last && n < kPerDraw && g_depth < kDepths; ++i) {
    const uint32_t at = list + 4 * uint32_t(i), c = Load(base, at);
    if (!c) continue;
    const uint32_t vt = Load(base, c);
    if (Load(base, vt + kAliveSlot) != kAliveGetter || Load(base, c + kState)) continue;
    if (!lk.owns_lock()) lk.lock();
    const bool known = std::find(std::begin(kClasses), std::end(kClasses), vt) != std::end(kClasses);
    Note(vt, known);
    if (!known || !Readable(c)) continue;
    if (!g_scratch) {
      g_scratch = rex::system::kernel_memory()->SystemHeapAlloc(kDepths * kPerDraw * kSlotSize);
      if (!g_scratch) break;
    }
    const uint32_t copy = g_scratch + (g_depth * kPerDraw + n) * kSlotSize;
    std::memcpy(base + copy, base + c, kSlotSize);
    rex::memory::store_and_swap<uint32_t>(base + copy + kState, 1);
    if (Cas(base, at, c, copy)) swaps[n++] = {at, c, copy};
  }
  ++g_depth;
  __imp__sub_82545D70(ctx, base);
  --g_depth;
  for (uint32_t i = 0; i < n; ++i) {
    Cas(base, swaps[i].at, swaps[i].copy, swaps[i].child);  // unless the update rewrote the slot
  }
}
