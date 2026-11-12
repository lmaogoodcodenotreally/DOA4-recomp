// Glow tuner (dev build only): TCR's P1 R3 glow tuner values applied to the
// shared glow post-process on every stage that uses it, edited with the mouse
// in the F5 tab. Design: docs/hooks.md.

#include "generated/doa4/doa4retail_init.h"

#ifndef DOA4_LEAN
#define DOA4_LEAN 0
#endif

#if !DOA4_LEAN

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>

#include <imgui.h>

#include <rex/hook.h>
#include <rex/logging/macros.h>
#include <rex/memory/utils.h>

REX_EXTERN(__imp__sub_824595F8);

namespace {

// Glow parameter block read by the pass: vec4 threshold, vec4 strength,
// u32 blur kernel (0-4) at +0x20, f32 blur spread (gaussian variance) at +0x24.
constexpr uint32_t kGlowParams = 0x830C5BC0u;     // main view
constexpr uint32_t kGlowParamsAlt = 0x830C5B90u;  // when 0x831A6354 is set
constexpr uint32_t kTcrThreshold = 0x83533400u;   // TCR tuner vec4, set at boot
constexpr uint32_t kTcrStrength = 0x83533950u;

std::atomic<bool> g_override{false};
std::atomic<int> g_kernel{1};       // TCR: 1
std::atomic<float> g_spread{16.0f}; // TCR: 16
std::atomic<int> g_passes{8};       // TCR: 8
std::atomic<uint8_t*> g_base{nullptr};

// What the stage itself asked for, captured before the override.
struct Seen {
  float threshold[3], strength[3], spread;
  int kernel, passes;
};
Seen g_seen{};                       // game thread writes, UI reads (display only)
std::atomic<int64_t> g_seen_ms{0};   // last stage-queued glow

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

float F32(const uint8_t* base, uint32_t a) {
  const uint32_t bits = rex::memory::load_and_swap<uint32_t>(base + a);
  float v;
  std::memcpy(&v, &bits, sizeof(v));
  return v;
}

void SetF32(uint8_t* base, uint32_t a, float v) {
  uint32_t bits;
  std::memcpy(&bits, &v, sizeof(bits));
  rex::memory::store_and_swap<uint32_t>(base + a, bits);
}

}  // namespace

// sub_824595F8: the glow pass (r3 target, r4 extra blur passes, r5 = alt
// view). Stages fill the parameter block and queue it via sub_824597D0; the
// block is read here at render time, so this is where the override goes.
REX_HOOK_RAW(sub_824595F8) {
  g_base.store(base, std::memory_order_relaxed);
  const uint32_t p = ctx.r5.u32 ? kGlowParamsAlt : kGlowParams;
  for (int i = 0; i < 3; ++i) {
    g_seen.threshold[i] = F32(base, p + i * 4);
    g_seen.strength[i] = F32(base, p + 0x10 + i * 4);
  }
  g_seen.kernel = static_cast<int>(rex::memory::load_and_swap<uint32_t>(base + p + 0x20));
  g_seen.spread = F32(base, p + 0x24);
  g_seen.passes = ctx.r4.s32;
  g_seen_ms.store(NowMs(), std::memory_order_relaxed);
  if (g_override.load(std::memory_order_relaxed)) {
    std::memcpy(base + p, base + kTcrThreshold, 16);
    std::memcpy(base + p + 0x10, base + kTcrStrength, 16);
    rex::memory::store_and_swap<uint32_t>(base + p + 0x20,
                                          g_kernel.load(std::memory_order_relaxed));
    SetF32(base, p + 0x24, g_spread.load(std::memory_order_relaxed));
    ctx.r4.u64 = static_cast<uint32_t>(g_passes.load(std::memory_order_relaxed));
  }
  __imp__sub_824595F8(ctx, base);
}

// F5 tab section. The values live in TCR's own tuner variables, so the
// controller tuner on TCR and these widgets edit the same numbers.
void Doa4DrawGlowTuner(uint8_t* base) {
  if (!base) return;
  ImGui::Separator();
  if (!ImGui::CollapsingHeader("Glow tuner (TCR P1 R3, any stage with glow)")) return;

  bool on = g_override.load(std::memory_order_relaxed);
  if (ImGui::Checkbox("Override the stage's glow", &on)) {
    g_override.store(on, std::memory_order_relaxed);
    REXLOG_INFO("DOA4 glow override {}", on ? "on" : "off");
  }
  float thr[3], str[3];
  for (int i = 0; i < 3; ++i) {
    thr[i] = F32(base, kTcrThreshold + i * 4);
    str[i] = F32(base, kTcrStrength + i * 4);
  }
  bool thr_changed = ImGui::ColorEdit3("Threshold", thr, ImGuiColorEditFlags_Float);
  float thr_all = (thr[0] + thr[1] + thr[2]) / 3.0f;
  if (ImGui::SliderFloat("Threshold (all)", &thr_all, 0.0f, 1.0f, "%.2f")) {
    thr[0] = thr[1] = thr[2] = thr_all;
    thr_changed = true;
  }
  ImGui::TextDisabled("Lower threshold = more of the picture glows");
  bool str_changed =
      ImGui::ColorEdit3("Strength", str, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
  float str_all = (str[0] + str[1] + str[2]) / 3.0f;
  if (ImGui::SliderFloat("Strength (all)", &str_all, 0.0f, 4.0f, "%.2f")) {
    str[0] = str[1] = str[2] = str_all;
    str_changed = true;
  }
  for (int i = 0; i < 3; ++i) {
    if (thr_changed) SetF32(base, kTcrThreshold + i * 4, std::clamp(thr[i], 0.0f, 1.0f));
    if (str_changed) SetF32(base, kTcrStrength + i * 4, std::max(str[i], 0.0f));
  }

  int kernel = g_kernel.load(std::memory_order_relaxed);
  if (ImGui::SliderInt("Blur kernel (0 = widest)", &kernel, 0, 4)) {
    g_kernel.store(kernel, std::memory_order_relaxed);
  }
  float spread = g_spread.load(std::memory_order_relaxed);
  if (ImGui::SliderFloat("Blur spread", &spread, 0.5f, 64.0f, "%.1f")) {
    g_spread.store(spread, std::memory_order_relaxed);
  }
  int passes = g_passes.load(std::memory_order_relaxed);
  if (ImGui::SliderInt("Extra blur passes", &passes, 0, 16)) {
    g_passes.store(passes, std::memory_order_relaxed);
  }

  const int64_t now = NowMs();
  const bool stage_glow = now - g_seen_ms.load(std::memory_order_relaxed) < 500;
  if (ImGui::Button("TCR defaults")) {
    for (int i = 0; i < 3; ++i) {
      SetF32(base, kTcrThreshold + i * 4, 0.40f);
      SetF32(base, kTcrStrength + i * 4, 1.00f);
    }
    g_kernel.store(1, std::memory_order_relaxed);
    g_spread.store(16.0f, std::memory_order_relaxed);
    g_passes.store(8, std::memory_order_relaxed);
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!stage_glow);
  if (ImGui::Button("Copy this stage's values")) {
    const Seen s = g_seen;
    for (int i = 0; i < 3; ++i) {
      SetF32(base, kTcrThreshold + i * 4, s.threshold[i]);
      SetF32(base, kTcrStrength + i * 4, s.strength[i]);
    }
    g_kernel.store(std::clamp(s.kernel, 0, 4), std::memory_order_relaxed);
    g_spread.store(s.spread, std::memory_order_relaxed);
    g_passes.store(std::max(s.passes, 0), std::memory_order_relaxed);
  }
  ImGui::EndDisabled();

  if (stage_glow) {
    const Seen s = g_seen;
    ImGui::TextDisabled("Stage glow: threshold %.2f %.2f %.2f, strength %.2f %.2f %.2f",
                        s.threshold[0], s.threshold[1], s.threshold[2], s.strength[0],
                        s.strength[1], s.strength[2]);
    ImGui::TextDisabled("            kernel %d, spread %.1f, passes %d", s.kernel, s.spread,
                        s.passes);
  } else {
    ImGui::TextDisabled("This stage has no glow of its own: nothing to tune");
  }
}

#endif  // !DOA4_LEAN
