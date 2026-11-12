// Debug free camera (dev build only). Design: docs/hooks.md.

#include "generated/doa4/doa4retail_init.h"

#ifndef DOA4_LEAN
#define DOA4_LEAN 0
#endif

#if !DOA4_LEAN

#include <atomic>
#include <cstdint>

#include <rex/hook.h>
#include <rex/logging/macros.h>
#include <rex/memory/utils.h>

REX_EXTERN(__imp__sub_82312B50);
REX_EXTERN(__imp__sub_824060A8);

namespace {

constexpr uint32_t kCameras = 0x8320D110u;      // 5 cameras, 0x10F0 bytes each
constexpr uint32_t kCameraCount = 5;
constexpr uint32_t kCameraStride = 0x10F0u;
constexpr uint32_t kFreecamGate = 0xF0Bu;       // 0 = off, 1..8 = warm-up, 8 = live
constexpr uint32_t kMainCamera = 0x8321AF6Du;   // u8 camera index, 0xFF = none
constexpr uint32_t kPlayerInput = 0x830366E0u;  // 4 per-player copies of a pad record
constexpr uint32_t kRecordSize = 44;            // 11 big-endian words
constexpr uint32_t kSlotActive = 0x830B8C84u;   // u8[4]
constexpr uint32_t kSlotPad = 0x830B8C88u;      // u8[4], pad record each slot copies
constexpr uint32_t kKeepButtons = 0x300u;       // START | BACK, so pause still works

std::atomic<bool> g_on{false};

// The camera drives off pad 0, so the fighter(s) fed from pad 0 must not see it.
void ScrubRecord(uint8_t* base, uint32_t slot) {
  uint8_t* rec = base + kPlayerInput + slot * kRecordSize;
  for (uint32_t w = 0; w < 11; ++w) {
    uint8_t* p = rec + w * 4;
    const uint32_t keep =
        w < 5 ? rex::memory::load_and_swap<uint32_t>(p) & kKeepButtons : 0;
    rex::memory::store_and_swap<uint32_t>(p, keep);
  }
}

}  // namespace

bool Doa4FreecamOn() { return g_on.load(std::memory_order_relaxed); }

void Doa4ShowStageOverlay();

void Doa4ToggleFreecam() {
  const bool on = !g_on.load(std::memory_order_relaxed);
  g_on.store(on, std::memory_order_relaxed);
  if (on) Doa4ShowStageOverlay();  // the overlay lists the free camera state
  REXLOG_INFO("DOA4 free camera {}", on ? "on" : "off");
}

// Camera task, once per frame before any camera updates: (re)arm the gate on
// the main camera. The game re-inits a camera with the gate cleared, so this
// re-asserts it every frame instead of writing it once.
REX_HOOK_RAW(sub_82312B50) {
  const bool on = g_on.load(std::memory_order_relaxed);
  const uint8_t main = base[kMainCamera];
  for (uint32_t i = 0; i < kCameraCount; ++i) {
    uint8_t& gate = base[kCameras + i * kCameraStride + kFreecamGate];
    if (on && i == main) {
      if (gate == 0) gate = 1;
    } else {
      gate = 0;
    }
  }
  __imp__sub_82312B50(ctx, base);
}

// Copies pad records into the per-player input (and into an inactive tag
// partner). While the free camera is on, blank whatever came from pad 0.
REX_HOOK_RAW(sub_824060A8) {
  __imp__sub_824060A8(ctx, base);
  if (!g_on.load(std::memory_order_relaxed)) return;
  for (uint32_t slot = 0; slot < 4; ++slot) {
    if (!base[kSlotActive + slot] || base[kSlotPad + slot] != 0) continue;
    ScrubRecord(base, slot);
    if (!base[kSlotActive + (slot ^ 2)]) ScrubRecord(base, slot ^ 2);
  }
}

#endif  // !DOA4_LEAN
