// Stage debug tools (dev build only): DS event replay, ST flipbook tuner, the
// live retail stage toggles (KYN/KYO/NY2/TCR), the PRO light-beam toggle,
// their log lines, the on-screen stage overlay and the optional stage info
// for every stage. Design: docs/hooks.md.

#include "generated/doa4/doa4retail_init.h"

#ifndef DOA4_LEAN
#define DOA4_LEAN 0
#endif

#if !DOA4_LEAN

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging/macros.h>
#include <rex/memory/utils.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/windowed_app_context.h>

REX_EXTERN(__imp__sub_825DA540);
REX_EXTERN(__imp__sub_826D0D38);
REX_EXTERN(__imp__sub_8261F7C8);
REX_EXTERN(__imp__sub_82631780);
REX_EXTERN(__imp__sub_82674838);
REX_EXTERN(__imp__sub_826EA888);
REX_EXTERN(__imp__sub_82683888);
REX_EXTERN(__imp__sub_82567658);

REXCVAR_DEFINE_BOOL(doa4_stage_info, false, "DOA4",
                    "Developer option: show the running stage's internal identifiers (slot, "
                    "code name, file ids, stage object) in the stage overlay on every stage.");

bool Doa4FreecamOn();
const char* Doa4StageSelectLabel();

namespace {

constexpr uint32_t kDsEventFrame = 0x82BDBD54u;  // s32, -1 idle, 0..99 playing
constexpr uint32_t kStFlipbooks = 0x82F19830u;   // 0x80 bytes each
constexpr uint32_t kStFlipbookSize = 0x80u;
constexpr int kStFlipbookCount = 6;              // 4 in scripted scenes, else 6
constexpr uint32_t kStTunerField = 0x78u;        // s32 selected field 0..3
constexpr uint32_t kKynLightLock = 0x831A6BD8u;  // u32 flags, toggled by the game
constexpr uint32_t kKyoLightLock = 0x831A6BF0u;
constexpr uint32_t kNy2ShowRt = 0x82F6B224u;
constexpr uint32_t kTcrTunerOn = 0x831A769Cu;
constexpr uint32_t kTcrTunerIndex = 0x8357D498u;  // 0..5
constexpr uint32_t kTcrVecA = 0x83533400u;  // vec3, indices 0..2: glow threshold RGB (0..1)
constexpr uint32_t kTcrVecB = 0x83533950u;  // vec3, indices 3..5: glow strength RGB (>= 0)

enum Stage { kDS, kST, kKYN, kKYO, kNY2, kTCR, kPRO, kStageCount };
constexpr const char* kStageNames[kStageCount] = {"DS", "ST", "KYN", "KYO", "NY2", "TCR", "PRO"};
constexpr int64_t kLiveMs = 500;  // a stage counts as current if its update ran this recently

// Running stage (retail, from the per-frame stage update sub_82566818).
constexpr uint32_t kRunningStage = 0x82E76830u;      // s32 slot 0..50, +4 s32 side (0/1)
constexpr uint32_t kStageActive = 0x83033A84u;       // u8, gates the stage's vtable +0x14 update
constexpr uint32_t kStageObjectTable = 0x82C2A6F8u;  // 51 slots x {side 0, side 1} object ptrs
constexpr uint32_t kStageFileTable = 0x82C5D6A8u;    // 51 slots x {u16 .tpr id, u16 _dat.bin id}
constexpr uint32_t kStageSlots = 51;
// Slot -> stage code, matched through the stage file ids (research doc §9d);
// nullptr = slot without stage files.
constexpr const char* kSlotNames[kStageSlots] = {
    nullptr, "MDL", "GL1", "WAL", "WL2", nullptr, nullptr, nullptr, "LOB", nullptr,  //  0..9
    "DS",    "ST",  "BO",  "TC",  "MK",  "NY",    "TS",    "CP",    "MT",  "RN",     // 10..19
    "PRO",   "KYO", "MK2", "AQU", "SAF", "SNW",   "BRI",   "SBE",   "POL", "NY2",    // 20..29
    "TS2",   "KYN", nullptr, nullptr, nullptr, "DAN", "RAY", "HEL", "BAB", nullptr,  // 30..39
    "TCR",   "KYR", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr};
std::atomic<int64_t> g_info_tick{0};  // last frame a stage update ran (doa4_stage_info on)

std::atomic<uint8_t*> g_base{nullptr};
std::atomic<int64_t> g_tick[kStageCount] = {};
std::atomic<bool> g_ds_replay{false};
std::atomic<int> g_st_obj{-1};  // -1 = tuner off
std::atomic<bool> g_overlay{true};
std::atomic<bool> g_pro_beams{true};  // E3 default: g_dbg_pro_toggle_P2_RT_R3 starts at 1

void ShowOverlay();  // registers the overlay dialog (any thread)

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

bool Live(Stage s) { return NowMs() - g_tick[s].load(std::memory_order_relaxed) <= kLiveMs; }

void Tick(Stage s, uint8_t* base) {
  g_base.store(base, std::memory_order_relaxed);
  g_tick[s].store(NowMs(), std::memory_order_relaxed);
  if (g_overlay.load(std::memory_order_relaxed)) ShowOverlay();
}

uint32_t U32(const uint8_t* base, uint32_t a) { return rex::memory::load_and_swap<uint32_t>(base + a); }

float F32(const uint8_t* base, uint32_t a) {
  const uint32_t bits = U32(base, a);
  float v;
  std::memcpy(&v, &bits, sizeof(v));
  return v;
}

constexpr const char* kStFieldNames[4] = {"scale X", "scale Y", "scale Z", "speed (frame interval)"};

// Runs a game toggle function and logs the flag if the call flipped it.
template <typename Fn>
void LoggedToggle(uint8_t* base, uint32_t flag, const char* what, Fn&& call) {
  const uint32_t before = U32(base, flag);
  call();
  const uint32_t after = U32(base, flag);
  if (after != before) REXLOG_INFO("DOA4 {}: {}", what, after ? "on" : "off");
}

}  // namespace

void Doa4ReplayDsEvent() {
  // Only arm while DS is running, so a stray press cannot fire on a later DS load.
  if (!Live(kDS)) {
    REXLOG_INFO("DOA4 DS event replay: not on stage DS, ignored");
    return;
  }
  g_ds_replay.store(true, std::memory_order_relaxed);
  REXLOG_INFO("DOA4 DS event replay requested");
}

void Doa4CycleStTuner() {
  int obj = g_st_obj.load(std::memory_order_relaxed) + 1;
  if (obj >= kStFlipbookCount) obj = -1;
  g_st_obj.store(obj, std::memory_order_relaxed);
  if (obj < 0) {
    REXLOG_INFO("DOA4 ST flipbook tuner off");
  } else {
    REXLOG_INFO("DOA4 ST flipbook tuner on flipbook {}{}", obj + 1,
                Live(kST) ? "" : " (takes effect on stage ST)");
  }
}

void Doa4ToggleStageOverlay() {
  const bool on = !g_overlay.load(std::memory_order_relaxed);
  g_overlay.store(on, std::memory_order_relaxed);
  if (on && Doa4FreecamOn()) ShowOverlay();
  REXLOG_INFO("DOA4 stage overlay {}", on ? "on" : "off");
}

void Doa4ToggleProBeams() {
  const bool on = !g_pro_beams.load(std::memory_order_relaxed);
  g_pro_beams.store(on, std::memory_order_relaxed);
  REXLOG_INFO("DOA4 PRO light beams {}{}", on ? "on" : "off",
              Live(kPRO) ? "" : " (takes effect on stage PRO)");
}

// sub_82683888: PRO light-beam draw pass (called from the PRO draw sub_8267DCE8).
// E3's counterpart sub_8250F640 only drew while g_dbg_pro_toggle_P2_RT_R3
// (P2 RT+R3, default on) was set; retail dropped the toggle. Skipping the call
// is draw-only: the game itself skips it during its own beam-off states.
REX_HOOK_RAW(sub_82683888) {
  Tick(kPRO, base);
  if (g_pro_beams.load(std::memory_order_relaxed)) __imp__sub_82683888(ctx, base);
}

// ds_stage_update 0x825DA540: stands in for the E3 P1 LB trigger
// (ds_dbg_event_anim_trigger_P1_LB), same write as ds_stage_event_dispatch case 4.
REX_HOOK_RAW(sub_825DA540) {
  Tick(kDS, base);
  if (g_ds_replay.exchange(false, std::memory_order_relaxed)) {
    rex::memory::store_and_swap<uint32_t>(base + kDsEventFrame, 0);
    REXLOG_INFO("DOA4 DS event animation started");
  }
  const int32_t before = static_cast<int32_t>(U32(base, kDsEventFrame));
  __imp__sub_825DA540(ctx, base);
  if (before >= 0 && static_cast<int32_t>(U32(base, kDsEventFrame)) < 0) {
    REXLOG_INFO("DOA4 DS event animation finished");
  }
}

// st_update_flipbooks 0x826D0D38 (vt_stage_ST slot 95): after the flipbooks
// tick, run the uncalled tuner st_flipbook_dbg_tuner_orphan 0x826CBB90 (slot 4
// of vt_st_flipbook) on the selected object. It reads P1 D-pad repeat + LB/RB.
REX_HOOK_RAW(sub_826D0D38) {
  Tick(kST, base);
  __imp__sub_826D0D38(ctx, base);
  const int obj = g_st_obj.load(std::memory_order_relaxed);
  if (obj < 0) return;
  const uint32_t addr = kStFlipbooks + obj * kStFlipbookSize;
  uint8_t before[0x3C];  // +0x40..+0x7B: values and the field index
  std::memcpy(before, base + addr + 0x40, sizeof(before));
  const uint64_t ret = ctx.r3.u64;
  ctx.r3.u64 = addr;
  sub_826CBB90(ctx, base);
  ctx.r3.u64 = ret;
  if (!std::memcmp(before, base + addr + 0x40, sizeof(before))) return;
  REXLOG_INFO("DOA4 ST flipbook {}: editing {}, scale {:.2f} {:.2f} {:.2f}, interval {:.0f}",
              obj + 1, kStFieldNames[U32(base, addr + kStTunerField) & 3], F32(base, addr + 0x40),
              F32(base, addr + 0x44), F32(base, addr + 0x48), F32(base, addr + 0x50));
}

// Live retail toggles (no patch needed, the game already reads the pad); the
// hooks only mark the stage as current and log the flags. Design: docs/hooks.md.
REX_HOOK_RAW(sub_8261F7C8) {  // kyn_dbg_light_lock_P1_R3 (vt_stage_KYN slot 5)
  Tick(kKYN, base);
  LoggedToggle(base, kKynLightLock, "KYN lighting freeze (P1 R3)",
               [&] { __imp__sub_8261F7C8(ctx, base); });
}

REX_HOOK_RAW(sub_82631780) {  // kyo_dbg_light_lock_P1_R3 (vt_stage_KYO slot 5)
  Tick(kKYO, base);
  LoggedToggle(base, kKyoLightLock, "KYO lighting freeze (P1 R3)",
               [&] { __imp__sub_82631780(ctx, base); });
}

REX_HOOK_RAW(sub_82674838) {  // ny2_draw_dbg_rt_P2_R3 (vt_stage_NY2 slot 7)
  Tick(kNY2, base);
  LoggedToggle(base, kNy2ShowRt, "NY2 debug render pass (P2 R3)",
               [&] { __imp__sub_82674838(ctx, base); });
}

REX_HOOK_RAW(sub_826EA888) {  // tcr_stage_update_dbg_tuner_P1_R3 (vt_stage_TCR slot 7)
  Tick(kTCR, base);
  uint8_t a[12], b[12];
  std::memcpy(a, base + kTcrVecA, 12);
  std::memcpy(b, base + kTcrVecB, 12);
  const uint32_t index = U32(base, kTcrTunerIndex);
  LoggedToggle(base, kTcrTunerOn, "TCR glow tuner (P1 R3)",
               [&] { __imp__sub_826EA888(ctx, base); });
  if (std::memcmp(a, base + kTcrVecA, 12) || std::memcmp(b, base + kTcrVecB, 12) ||
      index != U32(base, kTcrTunerIndex)) {
    REXLOG_INFO("DOA4 TCR glow tuner: editing {}, threshold {:.2f} {:.2f} {:.2f}, "
                "strength {:.2f} {:.2f} {:.2f}",
                U32(base, kTcrTunerIndex) + 1, F32(base, kTcrVecA), F32(base, kTcrVecA + 4),
                F32(base, kTcrVecA + 8), F32(base, kTcrVecB), F32(base, kTcrVecB + 4),
                F32(base, kTcrVecB + 8));
  }
}

// sub_82567658: stage state refresh, called only by the per-frame stage update
// sub_82566818 (itself a frame_systems_tick child, wrapped by the frame probe,
// so it cannot be hooked here), once the loaded-stage gate passed. Marks a
// running stage of any kind for the doa4_stage_info overlay block.
REX_HOOK_RAW(sub_82567658) {
  if (REXCVAR_GET(doa4_stage_info) && base[kStageActive]) {
    g_base.store(base, std::memory_order_relaxed);
    g_info_tick.store(NowMs(), std::memory_order_relaxed);
    if (g_overlay.load(std::memory_order_relaxed)) ShowOverlay();
    static uint64_t last = ~0ull;  // guest thread only
    const uint32_t slot = U32(base, kRunningStage), side = U32(base, kRunningStage + 4) & 1;
    if ((uint64_t(slot) << 1 | side) != last) {
      last = uint64_t(slot) << 1 | side;
      const bool valid = slot < kStageSlots;
      REXLOG_INFO("DOA4 stage info: {} slot {} side {}, files {:08X}, object {:08X}",
                  valid && kSlotNames[slot] ? kSlotNames[slot] : "?", slot, side,
                  valid ? U32(base, kStageFileTable + slot * 4) : 0,
                  valid ? U32(base, kStageObjectTable + (2 * slot + side) * 4) : 0);
    }
  }
  __imp__sub_82567658(ctx, base);
}

namespace {

void HideOverlay();

// Corner panel naming the current stage's debug controls and their state.
// Registered only while it has something to show: any registered dialog makes
// the presenter paint from the UI thread, which costs frame time even when
// the dialog draws nothing. It also never asks for display-rate repaints.
class StageOverlay : public rex::ui::ImGuiDialog {
 public:
  explicit StageOverlay(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}
  bool WantsContinuousRepaint() const override { return false; }

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (!DrawContent(io)) HideOverlay();
  }

 private:
  // Returns whether anything was drawn.
  static bool DrawContent(ImGuiIO& io) {
    bool drew = false;
    // Placeholder VS entries have no name sprite, so name the stage here
    // (independent of the overlay toggle).
    if (const char* label = Doa4StageSelectLabel()) {
      ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.82f),
                              ImGuiCond_Always, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowBgAlpha(0.7f);
      if (ImGui::Begin("##doa4_stagesel_label", nullptr,
                       ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                           ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings |
                           ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetWindowFontScale(1.6f);
        ImGui::Text("Stage %s", label);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::TextDisabled("No Versus art for this stage (placeholder). Dev option.");
      }
      ImGui::End();
      drew = true;
    }
    uint8_t* base = g_base.load(std::memory_order_relaxed);
    if (!base || !g_overlay.load(std::memory_order_relaxed)) return drew;
    int stage = -1;
    for (int s = 0; s < kStageCount; ++s) {
      if (Live(static_cast<Stage>(s))) stage = s;
    }
    const bool freecam = Doa4FreecamOn();
    const bool info = REXCVAR_GET(doa4_stage_info) &&
                      NowMs() - g_info_tick.load(std::memory_order_relaxed) <= kLiveMs;
    if (stage < 0 && !freecam && !info) return drew;

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 10, 10), ImGuiCond_Always, ImVec2(1, 0));
    ImGui::SetNextWindowBgAlpha(0.6f);
    if (ImGui::Begin("##doa4_stage_overlay", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                         ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_AlwaysAutoResize)) {
      if (info) {
        DrawStageInfo(base);
        if (stage >= 0) ImGui::Separator();
      }
      if (stage >= 0) {
        ImGui::Text("DOA4 dev - stage %s", kStageNames[stage]);
        ImGui::Separator();
        DrawStage(base, static_cast<Stage>(stage));
      }
      if (freecam) ImGui::TextUnformatted("Free camera: ON (controller 1 drives the camera)");
    }
    ImGui::End();
    return true;
  }

  static const char* OnOff(uint32_t v) { return v ? "ON" : "off"; }

  // The running stage's identifiers, read the way sub_82566818 picks the object.
  static void DrawStageInfo(const uint8_t* base) {
    const uint32_t slot = U32(base, kRunningStage);
    const uint32_t side = U32(base, kRunningStage + 4) & 1;
    const bool valid = slot < kStageSlots;
    const char* name = valid && kSlotNames[slot] ? kSlotNames[slot] : "?";
    ImGui::Text("Stage info - %s (slot %u, side %u)", name, slot, side);
    if (!valid) return;
    const uint32_t files = U32(base, kStageFileTable + slot * 4);
    const uint32_t obj = U32(base, kStageObjectTable + (2 * slot + side) * 4);
    ImGui::Text("Files: .tpr 0x%04X  _dat.bin 0x%04X", files >> 16, files & 0xFFFF);
    ImGui::Text("Object 0x%08X  vtable 0x%08X", obj, obj ? U32(base, obj) : 0);
  }

  static void DrawStage(const uint8_t* base, Stage stage) {
    switch (stage) {
      case kDS: {
        const int32_t f = static_cast<int32_t>(U32(base, kDsEventFrame));
        if (f >= 0) {
          ImGui::Text("Hotkey: replay event animation - playing, frame %d/100", f);
        } else {
          ImGui::TextUnformatted("Hotkey: replay event animation - idle");
        }
        break;
      }
      case kST: {
        const int obj = g_st_obj.load(std::memory_order_relaxed);
        if (obj < 0) {
          ImGui::TextUnformatted("Hotkey: flipbook tuner - off");
          break;
        }
        const uint32_t a = kStFlipbooks + obj * kStFlipbookSize;
        ImGui::Text("Hotkey: flipbook tuner - flipbook %d of %d", obj + 1, kStFlipbookCount);
        ImGui::Text("Editing %s", kStFieldNames[U32(base, a + kStTunerField) & 3]);
        ImGui::Text("Scale %.2f %.2f %.2f   interval %.0f", F32(base, a + 0x40),
                    F32(base, a + 0x44), F32(base, a + 0x48), F32(base, a + 0x50));
        ImGui::TextDisabled("P1 D-pad up/down: field, left/right: value, LB/RB: x10");
        ImGui::TextDisabled("Flipbooks only show in some ST scenes");
        break;
      }
      case kKYN:
      case kKYO:
        ImGui::Text("P1 R3: freeze lighting - %s",
                    OnOff(U32(base, stage == kKYN ? kKynLightLock : kKyoLightLock)));
        break;
      case kNY2:
        ImGui::Text("P2 R3: debug render pass - %s", OnOff(U32(base, kNy2ShowRt)));
        break;
      case kTCR: {
        const uint32_t on = U32(base, kTcrTunerOn);
        ImGui::Text("P1 R3: glow tuner - %s", OnOff(on));
        if (!on) break;
        const uint32_t idx = U32(base, kTcrTunerIndex);
        constexpr const char* kRgb[3] = {"R", "G", "B"};
        ImGui::Text("Editing %s %s", idx < 3 ? "threshold" : "strength", kRgb[idx % 3]);
        ImGui::Text("Threshold RGB %.2f %.2f %.2f (lower = more glow)", F32(base, kTcrVecA),
                    F32(base, kTcrVecA + 4), F32(base, kTcrVecA + 8));
        ImGui::Text("Strength  RGB %.2f %.2f %.2f", F32(base, kTcrVecB), F32(base, kTcrVecB + 4),
                    F32(base, kTcrVecB + 8));
        ImGui::TextDisabled("P1 D-pad up/down: value, left/right: +-0.01, LB/RB: x10");
        ImGui::TextDisabled("Mouse + any stage: F5 tab > Glow tuner");
        break;
      }
      case kPRO:
        ImGui::Text("End: light beams - %s", OnOff(g_pro_beams.load(std::memory_order_relaxed)));
        break;
      default:
        break;
    }
  }
};

}  // namespace

namespace {

rex::ui::ImGuiDrawer* g_drawer = nullptr;              // UI thread only
std::unique_ptr<StageOverlay> g_dialog;                // UI thread only
std::atomic<rex::ui::WindowedAppContext*> g_ui{nullptr};
std::atomic<bool> g_dialog_up{false};    // registered or about to be
std::atomic<bool> g_dialog_down{false};  // removal queued

void ShowOverlay() {
  if (g_dialog_up.exchange(true, std::memory_order_relaxed)) return;
  if (auto* ui = g_ui.load(std::memory_order_acquire)) {
    ui->CallInUIThread([] {
      if (g_drawer && !g_dialog) g_dialog = std::make_unique<StageOverlay>(g_drawer);
    });
  }
}

// Called from the dialog's own draw, so the removal is deferred.
void HideOverlay() {
  if (g_dialog_down.exchange(true, std::memory_order_relaxed)) return;
  if (auto* ui = g_ui.load(std::memory_order_acquire)) {
    ui->CallInUIThreadDeferred([] {
      g_dialog.reset();
      g_dialog_up.store(false, std::memory_order_relaxed);
      g_dialog_down.store(false, std::memory_order_relaxed);
    });
  }
}

}  // namespace

void Doa4ShowStageOverlay() { ShowOverlay(); }

void Doa4InitStageOverlay(rex::ui::ImGuiDrawer* drawer, rex::ui::WindowedAppContext* ui) {
  g_drawer = drawer;
  g_ui.store(ui, std::memory_order_release);
}

void Doa4ShutdownStageOverlay() {
  g_ui.store(nullptr, std::memory_order_release);
  g_dialog.reset();
  g_drawer = nullptr;
}

#endif  // !DOA4_LEAN
