// "All stages in Versus" (dev build only): every entry of the VS stage list is
// selectable, ignoring the save-data unlocks and the sub-mode filter, and the
// 17 stage slots that never had a VS entry are appended with placeholder art.
// Design: docs/hooks.md.

#include "generated/doa4/doa4retail_init.h"

#ifndef DOA4_LEAN
#define DOA4_LEAN 0
#endif

#if !DOA4_LEAN

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging/macros.h>
#include <rex/memory/utils.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

REXCVAR_DEFINE_BOOL(doa4_all_vs_stages, false, "DOA4",
                    "Developer option: make every stage selectable in the Versus stage list, "
                    "including locked stages and the stage slots that never had a Versus entry "
                    "(shown with placeholder art). Offline only.");

REX_EXTERN(__imp__sub_82537458);
REX_EXTERN(__imp__sub_82524490);
REX_EXTERN(__imp__sub_82538438);
REX_EXTERN(__imp__sub_825382A8);

namespace {

constexpr uint32_t kStageList = 0x82C32AF8u;  // 17 x 16 bytes: u8 id, u8 sub-mode ok, ..., u32 kind
constexpr uint32_t kStageCount = 17;
constexpr uint32_t kEntrySize = 16;
constexpr uint32_t kListPtr = 0x8310B728u;    // built list: ptr, count; kind +12, 0 = hidden
constexpr uint32_t kSelected = 0x8310B710u;   // u32 stage id under the cursor
constexpr uint32_t kOnline = 0x8310B784u;     // u32, non-zero in a Live session
constexpr uint8_t kRename = 0x80;             // ids 12/19/23 | 0x80 collide with nothing
constexpr uint32_t kPlaceholderThumb = 0x00080018u;  // the Random entry's sprites
constexpr uint32_t kPlaceholderName = 0x00080024u;
constexpr uint32_t kKindSelectable = 3;  // confirm re-randomizes any other kind

// Stage slots with no VS entry whose stage data is on the retail disc; codes
// are the file prefixes. Slots 1/2/3/4/8/11/18/25/27 (MDL GL1 WAL WL2 LOB ST
// MT SNW SBE) are left out: their stg.afs entries (file table 0x82C5D6A8) are
// 16-byte stubs, and highlighting one in stage select starts its background
// load, which hands the model parser a null buffer and crashes.
struct Extra {
  uint8_t id;
  const char* label;
};
constexpr Extra kExtras[] = {
    {22, "MK2 (slot 22)"}, {28, "POL (slot 28)"}, {29, "NY2 (slot 29)"}, {30, "TS2 (slot 30)"},
    {35, "DAN (slot 35)"}, {38, "BAB (slot 38)"}, {40, "TCR (slot 40)"}, {41, "KYR (slot 41)"},
};
constexpr uint32_t kExtCount = kStageCount + sizeof(kExtras) / sizeof(kExtras[0]);

uint32_t g_ext = 0;       // guest address of the extended list, allocated once
bool g_building = false;  // inside the list builder (game thread only)
std::atomic<int64_t> g_drawn_ms{0};
std::atomic<uint8_t*> g_base{nullptr};

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// The builder hides these three by id: 19 and 12 from save flags, 23 from the
// full-completion check.
bool Gated(uint8_t id) { return id == 12 || id == 19 || id == 23; }

}  // namespace

// Stage-select label for the dev overlay: the real stage behind a placeholder
// entry, or nullptr when the cursor is not on one.
const char* Doa4StageSelectLabel() {
  uint8_t* base = g_base.load(std::memory_order_relaxed);
  if (!base || NowMs() - g_drawn_ms.load(std::memory_order_relaxed) > 500) return nullptr;
  const uint32_t sel = rex::memory::load_and_swap<uint32_t>(base + kSelected);
  for (const Extra& e : kExtras) {
    if (e.id == sel) return e.label;
  }
  return nullptr;
}

// sub_82537458: builds the stage-select list. It points the list at the static
// table (17 entries), sets every kind to 3, calls sub_82524490, then hides ids
// 19/23/12 and, in one VS sub-mode, entries whose byte 1 is 0, and counts what
// is left. With the option on, sub_82524490 swaps the list for a copy with the
// extra slots appended, the gated ids are renamed in that copy so none of the
// checks match, and everything is restored afterwards; save data is never touched.
REX_HOOK_RAW(sub_82537458) {
  if (!REXCVAR_GET(doa4_all_vs_stages) || rex::memory::load_and_swap<uint32_t>(base + kOnline)) {
    __imp__sub_82537458(ctx, base);
    return;
  }
  if (!g_ext) g_ext = rex::system::kernel_memory()->SystemHeapAlloc(kExtCount * kEntrySize);
  if (!g_ext) {
    REXLOG_WARN("DOA4 all VS stages: guest allocation failed, using the stock list");
    __imp__sub_82537458(ctx, base);
    return;
  }
  uint8_t* table = base + kStageList;
  uint8_t saved_flag[kStageCount];
  for (uint32_t i = 0; i < kStageCount; ++i) {
    saved_flag[i] = table[i * kEntrySize + 1];
    table[i * kEntrySize + 1] = 1;
  }
  // r3 is the stage to start on (-1 = mode default); keep it findable.
  if (ctx.r3.s32 >= 0 && Gated(static_cast<uint8_t>(ctx.r3.u32))) ctx.r3.u64 |= kRename;

  g_building = true;
  __imp__sub_82537458(ctx, base);
  g_building = false;

  for (uint32_t i = 0; i < kStageCount; ++i) table[i * kEntrySize + 1] = saved_flag[i];
  uint8_t* ext = base + g_ext;
  for (uint32_t i = 0; i < kExtCount; ++i) {
    uint8_t& id = ext[i * kEntrySize];
    if (id != 0xFF && (id & kRename) && Gated(id & ~kRename)) id &= ~kRename;
  }
  const uint32_t sel = rex::memory::load_and_swap<uint32_t>(base + kSelected);
  if ((sel & kRename) && sel < 0x100 && Gated(static_cast<uint8_t>(sel & ~kRename))) {
    rex::memory::store_and_swap<uint32_t>(base + kSelected, sel & ~kRename);
  }

  const uint32_t list = rex::memory::load_and_swap<uint32_t>(base + kListPtr);
  const uint32_t count = rex::memory::load_and_swap<uint32_t>(base + kListPtr + 4);
  uint32_t shown = 0;
  for (uint32_t i = 0; i < count; ++i) {
    shown += rex::memory::load_and_swap<uint32_t>(base + list + i * kEntrySize + 12) != 0;
  }
  REXLOG_INFO("DOA4 all VS stages: {}/{} stage-select entries selectable ({} placeholder)",
              shown, count, count - kStageCount);
}

// sub_82524490: the id-19 unlock check, called once by the list builder right
// after it has pointed the list at the static table with every kind set to 3.
REX_HOOK_RAW(sub_82524490) {
  if (g_building) {
    uint8_t* ext = base + g_ext;
    std::memcpy(ext, base + kStageList, kStageCount * kEntrySize);
    for (uint32_t i = 0; i < kStageCount; ++i) {
      if (Gated(ext[i * kEntrySize])) ext[i * kEntrySize] |= kRename;
    }
    for (uint32_t i = kStageCount; i < kExtCount; ++i) {
      uint8_t* e = ext + i * kEntrySize;
      std::memset(e, 0, kEntrySize);
      e[0] = kExtras[i - kStageCount].id;
      e[1] = 1;
      rex::memory::store_and_swap<uint32_t>(e + 4, kPlaceholderThumb);
      rex::memory::store_and_swap<uint32_t>(e + 8, kPlaceholderName);
      rex::memory::store_and_swap<uint32_t>(e + 12, kKindSelectable);
    }
    rex::memory::store_and_swap<uint32_t>(base + kListPtr, g_ext);
    rex::memory::store_and_swap<uint32_t>(base + kListPtr + 4, kExtCount);
  }
  __imp__sub_82524490(ctx, base);
}

void Doa4ShowStageOverlay();  // src/doa4_stage_debug.cpp

// sub_825382A8: resolves the confirmed stage-select entry to the stage id the
// match will load (Random and non-selectable entries re-roll here).
REX_HOOK_RAW(sub_825382A8) {
  __imp__sub_825382A8(ctx, base);
  const uint32_t id = ctx.r3.u32;
  const char* label = "stock";
  for (const Extra& e : kExtras) {
    if (e.id == id) label = e.label;
  }
  REXLOG_INFO("DOA4 stage select: confirmed stage {} ({})", id, label);
}

// sub_82538438: stage-select screen draw (every frame on that screen); only
// marks the screen as current for Doa4StageSelectLabel.
REX_HOOK_RAW(sub_82538438) {
  __imp__sub_82538438(ctx, base);
  g_base.store(base, std::memory_order_relaxed);
  g_drawn_ms.store(NowMs(), std::memory_order_relaxed);
  if (Doa4StageSelectLabel()) Doa4ShowStageOverlay();
}

#endif  // !DOA4_LEAN
