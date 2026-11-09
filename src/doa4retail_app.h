// doa4retail - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/logging/macros.h>
#include <rex/perf/counter.h>
#include <rex/rex_app.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/keybinds.h>

// Lean play profile (DOA4_LEAN). Design: docs/architecture.md.
#ifndef DOA4_LEAN
#define DOA4_LEAN 0
#endif

// State poll (v1 fallback address; v2 poll fn in OnPostSetup). Design:
// docs/architecture.md, docs/debug.md.
REXCVAR_DECLARE(uint32_t, perf_state_addr);

#if !DOA4_LEAN
// DOA4 debug tab (F5 toggle, F6 mark). Design: docs/debug.md.
class Doa4DebugDialog : public rex::ui::ImGuiDialog {
 public:
  using GuestBaseFn = std::function<uint8_t*()>;
  using MarkFn = std::function<void(const char* why)>;

  // Retail stage-chain addresses. Map: docs/debug.md.
  static constexpr uint32_t kRetailActiveStageId = 0x82C79B18;   // E3: 0x82A777A0
  static constexpr uint32_t kRetailSubStageId = 0x82C79B14;      // E3: 0x82A7779C
  static constexpr uint32_t kRetailActiveStageObj = 0x82C79B1C;  // E3: 0x82A777A4
  static constexpr uint32_t kRetailStageCfg = 0x83040090;        // E3: 0x82CEF158
  static constexpr uint32_t kRetailSlotTable = 0x82FFC0A8;       // E3: 0x82CE8D48
  static constexpr uint32_t kRetailPickerCursor = 0x83035CC4;    // picker byte 0
  // Dump size: applier-written region only (see docs/debug.md).
  static constexpr uint32_t kStageCfgSize = 0x310;

  Doa4DebugDialog(rex::ui::ImGuiDrawer* drawer, GuestBaseFn guest_base,
                  MarkFn mark_fn)
      : ImGuiDialog(drawer),
        guest_base_(std::move(guest_base)),
        mark_fn_(mark_fn) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    uint8_t* base = guest_base_ ? guest_base_() : nullptr;

    ImGui::SetNextWindowPos(ImVec2(10, 320), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420, 360), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.8f);
    if (!ImGui::Begin("DOA4 Debug##doa4", nullptr, ImGuiWindowFlags_NoCollapse)) {
      ImGui::End();
      return;
    }

    if (!base) {
      ImGui::TextDisabled("guest memory not mapped yet");
      ImGui::End();
      return;
    }

    // --- Live game state (same pack as PollGuestState / the [state] lines) --
    const unsigned fs = base[0x83035CC0u];
    const unsigned bk = base[0x82FCC93Bu];
    const unsigned st0 = base[0x83035CC4u];
    const unsigned st1 = base[0x83035CC5u];
    const uint32_t scr_be =
        (static_cast<uint32_t>(base[0x82C31C24u]) << 24) |
        (static_cast<uint32_t>(base[0x82C31C24u + 1]) << 16) |
        (static_cast<uint32_t>(base[0x82C31C24u + 2]) << 8) |
        static_cast<uint32_t>(base[0x82C31C24u + 3]);
    const int scr = (scr_be == 0xFFFFFFFFu) ? -1 : static_cast<int>(scr_be & 0xFF);
    const unsigned c0 = base[0x83033B41u];
    const unsigned c1 = base[0x83033B41u + 156];

    ImGui::Text("Mode: %s", ModeName(fs, bk, scr, st0, st1));
    ImGui::Text("fs=%u bk=%u scr=%d  stg=(%u,%u)", fs, bk, scr, st0, st1);
    ImGui::Text("record chars: (%u,%u)", c0, c1);
    if (const char* anchor = PickerAnchor(st0, st1)) {
      ImGui::SameLine();
      ImGui::TextDisabled("= %s", anchor);
    }
    if (ImGui::SmallButton("Copy state line")) {
      char line[160];
      std::snprintf(line, sizeof(line),
                    "fs=%u bk=%u scr=%d stg=(%u,%u) char=(%u,%u)", fs, bk, scr,
                    st0, st1, c0, c1);
      ImGui::SetClipboardText(line);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Mark in log (F6)")) {
      if (mark_fn_) {
        mark_fn_("MARK");
      }
    }

    // --- Stage-select chain (retail addresses resolved 2026-10-13) --------
    ImGui::Separator();
    ImGui::TextUnformatted("Active stage chain");
    {
      const uint16_t stage_id = ReadBE16(base, kRetailActiveStageId);
      const uint16_t sub_id = ReadBE16(base, kRetailSubStageId);
      if (stage_id == 0xFFFF) {
        ImGui::TextUnformatted("stage: (none active)");
      } else {
        ImGui::Text("stage: id=%u %s  sub=%u", stage_id,
                    StageName(stage_id < 33 ? stage_id : 144), sub_id);
      }
      const uint32_t obj = ReadBE32(base, kRetailActiveStageObj);
      ImGui::Text("stage obj: 0x%08X%s", obj, obj == 0x7FFFFFFFu ? " (none)" : "");
      if (IsGuestPointer(obj)) {
        ImGui::SameLine();
        ImGui::TextDisabled("self=0x%08X sub=%u id=%u", ReadBE32(base, obj),
                            ReadBE16(base, obj + 4), ReadBE16(base, obj + 6));
      }

      // Chain churn instrument (see docs/debug.md).
      const uint64_t sig = (static_cast<uint64_t>(stage_id) << 48) |
                           (static_cast<uint64_t>(sub_id) << 32) | obj;
      const auto now = std::chrono::steady_clock::now();
      if (chain_sig_valid_) {
        if (sig != chain_sig_) {
          ++chain_changes_;
          ++chain_win_changes_;
        }
      } else {
        chain_sig_valid_ = true;
      }
      chain_sig_ = sig;
      const auto win_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              now - chain_win_start_)
                              .count();
      if (win_ms >= 1000) {
        chain_delta_ = static_cast<int>(chain_win_changes_ * 1000 / win_ms);
        chain_win_changes_ = 0;
        chain_win_start_ = now;
      }
      if (chain_changes_ != 0 &&
          std::chrono::duration_cast<std::chrono::milliseconds>(
              now - chain_last_log_)
                  .count() >= 250) {
        REXLOG_ERROR("DOA4DBG chain x{} stage=({},{}) obj=0x{:08X}",
                     chain_changes_, stage_id, sub_id, obj);
        chain_changes_ = 0;
        chain_last_log_ = now;
      }
      if (chain_delta_ != 0) {
        ImGui::TextDisabled("chain churn: %d/s", chain_delta_);
      }
      ImGui::Text("cfg: %s +0x04=%u +0x06=%u +0x08=%u",
                  ReadBE32(base, kRetailStageCfg) == 0x7FFFFFFFu ? "uninit" : "set",
                  ReadBE16(base, kRetailStageCfg + 4),
                  ReadBE16(base, kRetailStageCfg + 6),
                  ReadBE16(base, kRetailStageCfg + 8));
      ImGui::Text("cfg rows: +0x208=%u +0x308=%u",
                  ReadBE16(base, kRetailStageCfg + 0x208),
                  ReadBE16(base, kRetailStageCfg + 0x308));
      ImGui::Text("slot table [0..11]:");
      ImGui::SameLine();
      for (int i = 0; i < 12; i++) {
        const uint8_t v = base[kRetailSlotTable + i];
        if (v == 0xFF) {
          ImGui::TextDisabled("--");
        } else {
          ImGui::Text("%u", v);
        }
        if (i != 11) ImGui::SameLine();
      }

      // Interactive cursor poke: game's own interaction path (see docs/debug.md).
      ImGui::InputInt("picker slot", &force_stage_id_);
      ImGui::SameLine();
      if (ImGui::Button("Set cursor[0]")) {
        const uint8_t v = static_cast<uint8_t>(force_stage_id_ & 0xFF);
        base[kRetailPickerCursor] = v;
        REXLOG_ERROR("DOA4DBG FORCE picker cursor[0] -> {} (slot[{}]={})", v, v,
                     base[kRetailSlotTable + v]);
      }
      ImGui::TextDisabled("(cursor[0] = shared picker base: pokes move the\n"
                          "menu cursor in main menu, the stage grid in stage\n"
                          "select -- use there for preview repros)");
      if (ImGui::Button("Dump stage cfg (0x310) to log")) {
        DumpStageCfg(base);
      }
    }

    ImGui::End();
  }

 private:
  static uint16_t ReadBE16(const uint8_t* base, uint32_t addr) {
    return static_cast<uint16_t>((static_cast<uint16_t>(base[addr]) << 8) |
                                 base[addr + 1]);
  }
  static uint32_t ReadBE32(const uint8_t* base, uint32_t addr) {
    return (static_cast<uint32_t>(base[addr]) << 24) |
           (static_cast<uint32_t>(base[addr + 1]) << 16) |
           (static_cast<uint32_t>(base[addr + 2]) << 8) |
           static_cast<uint32_t>(base[addr + 3]);
  }
  static bool IsGuestPointer(uint32_t p) {
    // REX_IMAGE_BASE .. REX_IMAGE_BASE + REX_IMAGE_SIZE (guest image window).
    return p >= 0x82000000u && p < 0x83600000u;
  }

  // Retail mode map (see docs/debug.md).
  static const char* ModeName(unsigned fs, unsigned bk, int scr, unsigned st0,
                              unsigned st1) {
    if (fs == 1 && bk == 1) return "press-start";
    if (fs == 2 && bk == 2) {
      if (scr == 8) return "stage select";
      // Discriminator is stg0 only (see docs/debug.md).
      if (scr == 5) {
        return st0 == 0 ? "main-menu character select" : "main-menu mode select";
      }
      return "menu";
    }
    switch (fs) {
      case 3: return "story (fight/movie)";
      case 4: return "time attack";
      case 5: return "versus";
      case 6: return "survival/victory";
      case 8: return "team battle";
      case 9: return "sparring";
      case 11: return "watch";  // fs=11/bk=11/scr=8, user-confirmed 2026-10-14
      default: return "other";
    }
  }

  // Picker anchors (see docs/debug.md).
  static const char* PickerAnchor(unsigned st0, unsigned st1) {
    if (st0 == 0 && st1 == 12) return "Biolab Core";
    if (st0 == 26 && st1 == 12) return "Waterfall Valley";
    return nullptr;
  }

  // Stage FILE ids (see docs/debug.md).
  static const char* StageName(unsigned id) {
    static constexpr const char* kStages[33] = {
        "MK",  "MK2", "DS",  "BO",  "TC",  "NY",  "NY2", "TS",
        "TS2", "CP",  "RN",  "PRO", "KYO", "KYN", "AQU", "SAF",
        "BRI", "POL", "DAN", "RAY", "HEL", "BAB", "TCR", "KYR",
        "MDL", "GL1", "WAL", "WL2", "ST",  "LOB", "MT",  "SNW",
        "SBE"};
    if (id < 33) return kStages[id];
    if (id == 144) return "random";
    return "?";
  }

  void DumpStageCfg(const uint8_t* base) {
    REXLOG_ERROR("DOA4DBG stage cfg dump @0x{:08X} ({} bytes)", kRetailStageCfg,
                 kStageCfgSize);
    for (uint32_t off = 0; off < kStageCfgSize; off += 16) {
      REXLOG_ERROR("  +0x{:03X}: {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}"
                   " {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}",
                   off, base[kRetailStageCfg + off + 0], base[kRetailStageCfg + off + 1],
                   base[kRetailStageCfg + off + 2], base[kRetailStageCfg + off + 3],
                   base[kRetailStageCfg + off + 4], base[kRetailStageCfg + off + 5],
                   base[kRetailStageCfg + off + 6], base[kRetailStageCfg + off + 7],
                   base[kRetailStageCfg + off + 8], base[kRetailStageCfg + off + 9],
                   base[kRetailStageCfg + off + 10], base[kRetailStageCfg + off + 11],
                   base[kRetailStageCfg + off + 12], base[kRetailStageCfg + off + 13],
                   base[kRetailStageCfg + off + 14], base[kRetailStageCfg + off + 15]);
    }
  }

  GuestBaseFn guest_base_;
  MarkFn mark_fn_;
  int force_stage_id_ = 0;
  // Chain churn instrument state (see the chain section in OnDraw).
  uint64_t chain_sig_ = 0;
  bool chain_sig_valid_ = false;
  uint32_t chain_changes_ = 0;      // value changes since last log line
  uint32_t chain_win_changes_ = 0;  // value changes in the current 1s window
  int chain_delta_ = 0;             // displayed churn rate (changes/sec)
  std::chrono::steady_clock::time_point chain_win_start_{};
  std::chrono::steady_clock::time_point chain_last_log_{};
};
#endif  // !DOA4_LEAN

class Doa4retailApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  // DOA4 debug tab (see docs/debug.md).
#if !DOA4_LEAN
  std::unique_ptr<rex::ui::ImGuiDialog> debug_tab_;
#endif

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Doa4retailApp>(new Doa4retailApp(ctx, "doa4retail",
        PPCImageConfig));
  }

#if !DOA4_LEAN
  // Guest-image dump (DOA4_DUMP_IMAGE). See docs/debug.md.
  void OnPostLoadXexImage() override {
    const char* path = std::getenv("DOA4_DUMP_IMAGE");
    if (!path || !*path) {
      return;
    }
    constexpr uint64_t kImageBase = 0x82000000ull;  // REX_IMAGE_BASE (pch)
    constexpr uint64_t kImageSize = 0x1600000ull;   // REX_IMAGE_SIZE (pch)
    uint8_t* base = runtime()->virtual_membase();
    if (!base) {
      REXLOG_ERROR("GUEST IMGDUMP: no memory base");
      return;
    }
    std::FILE* f = std::fopen(path, "wb");
    if (!f) {
      REXLOG_ERROR("GUEST IMGDUMP: cannot open {}", path);
      return;
    }
    const size_t written =
        std::fwrite(base + kImageBase, 1, static_cast<size_t>(kImageSize), f);
    std::fclose(f);
    REXLOG_ERROR("GUEST IMGDUMP {} bytes @0x{:X} -> {}", written, kImageBase,
                 path);
  }

  // State tracker v2 (see docs/debug.md).
  void OnPostSetup() override {
    rex::perf::SetStatePollFn(&Doa4retailApp::PollGuestState);
    rex::perf::SetStateLabelFn(&Doa4retailApp::StateLabel);
    REXCVAR_SET(perf_state_addr, 0x82C31C24u);  // v1 fallback (see above)
  }

  // F5/F6 binds (see docs/architecture.md, docs/debug.md).
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    rex::ui::RegisterBind("bind_doa4_debug", "F5", "Toggle DOA4 debug tab",
                          [this, drawer] {
                            if (debug_tab_) {
                              debug_tab_.reset();
                            } else {
                              debug_tab_ = std::make_unique<Doa4DebugDialog>(
                                  drawer,
                                  [this]() -> uint8_t* {
                                    return runtime() ? runtime()->virtual_membase()
                                                     : nullptr;
                                  },
                                  [this](const char* why) { MarkState(why); });
                            }
                          });
    rex::ui::RegisterBind("bind_doa4_mark", "F6",
                          "Mark felt hitch in DOA4 log (state snapshot)",
                          [this] { MarkState("F6MARK"); });
  }

  void OnShutdown() override {
    rex::ui::UnregisterBind("bind_doa4_debug");
    rex::ui::UnregisterBind("bind_doa4_mark");
    debug_tab_.reset();
  }

  // Felt-hitch breadcrumb (see docs/debug.md).
  void MarkState(const char* why) {
    const uint8_t* base = runtime() ? runtime()->virtual_membase() : nullptr;
    if (!base) {
      return;
    }
    const uint32_t scr_be = (static_cast<uint32_t>(base[0x82C31C24u]) << 24) |
                            (static_cast<uint32_t>(base[0x82C31C24u + 1]) << 16) |
                            (static_cast<uint32_t>(base[0x82C31C24u + 2]) << 8) |
                            static_cast<uint32_t>(base[0x82C31C24u + 3]);
    const int scr = (scr_be == 0xFFFFFFFFu) ? -1 : static_cast<int>(scr_be & 0xFF);
    // Stage chain (see docs/debug.md).
    const uint16_t stage_id = static_cast<uint16_t>(
        (static_cast<uint16_t>(base[0x82C79B18u]) << 8) | base[0x82C79B18u + 1]);
    const uint16_t sub_id = static_cast<uint16_t>(
        (static_cast<uint16_t>(base[0x82C79B14u]) << 8) | base[0x82C79B14u + 1]);
    const uint32_t obj = (static_cast<uint32_t>(base[0x82C79B1Cu]) << 24) |
                         (static_cast<uint32_t>(base[0x82C79B1Cu + 1]) << 16) |
                         (static_cast<uint32_t>(base[0x82C79B1Cu + 2]) << 8) |
                         static_cast<uint32_t>(base[0x82C79B1Cu + 3]);
    REXLOG_ERROR(
        "USERMARK {} fs={} bk={} stg=({},{}) scr={} char=({},{}) "
        "stage=({},{}) obj=0x{:08X}",
        why, base[0x83035CC0u], base[0x82FCC93Bu], base[0x83035CC4u],
        base[0x83035CC5u], scr, base[0x83033B41u], base[0x83033B41u + 156],
        stage_id, sub_id, obj);
  }

  // State pack (see docs/debug.md).
  static int64_t PollGuestState(const uint8_t* base) {
    if (!base)
      return INT64_MIN;
    const uint64_t scr =
        (static_cast<uint64_t>(base[0x82C31C24u]) << 24) |
        (static_cast<uint64_t>(base[0x82C31C24u + 1]) << 16) |
        (static_cast<uint64_t>(base[0x82C31C24u + 2]) << 8) |
        static_cast<uint64_t>(base[0x82C31C24u + 3]);
    uint64_t v = 0;
    v |= static_cast<uint64_t>(base[0x83035CC0u]);
    v |= static_cast<uint64_t>(base[0x82FCC93Bu]) << 8;
    v |= static_cast<uint64_t>(base[0x83035CC4u]) << 16;
    v |= static_cast<uint64_t>(base[0x83035CC5u]) << 24;
    v |= (scr & 0xFFu) << 32;
    v |= static_cast<uint64_t>(base[0x83033B41u]) << 40;
    v |= static_cast<uint64_t>(base[0x83033B41u + 156]) << 48;
    return static_cast<int64_t>(v);
  }

  // Stage FILE ids (see docs/debug.md).
  static const char* StageName(unsigned id) {
    static constexpr const char* kStages[33] = {
        "MK",  "MK2", "DS",  "BO",  "TC",  "NY",  "NY2", "TS",
        "TS2", "CP",  "RN",  "PRO", "KYO", "KYN", "AQU", "SAF",
        "BRI", "POL", "DAN", "RAY", "HEL", "BAB", "TCR", "KYR",
        "MDL", "GL1", "WAL", "WL2", "ST",  "LOB", "MT",  "SNW",
        "SBE"};
    if (id < 33)
      return kStages[id];
    if (id == 144)
      return "random";
    // Two rotating buffers so a label showing two different unknown ids
    // (stage side 0 != side 1) does not alias them onto one slot.
    static thread_local char unk[2][16];
    static thread_local unsigned slot = 0;
    slot ^= 1u;
    std::snprintf(unk[slot], sizeof(unk[slot]), "id%u", id);
    return unk[slot];
  }

  // State labeler (see docs/debug.md).
  static const char* StateLabel(int64_t value) {
    if (value == INT64_MIN)
      return "unset";
    const uint64_t v = static_cast<uint64_t>(value);
    const unsigned fs = static_cast<unsigned>(v & 0xFF);
    const unsigned bk = static_cast<unsigned>((v >> 8) & 0xFF);
    const unsigned st0 = static_cast<unsigned>((v >> 16) & 0xFF);
    const unsigned st1 = static_cast<unsigned>((v >> 24) & 0xFF);
    const unsigned scrb = static_cast<unsigned>((v >> 32) & 0xFF);
    const int scr = (scrb == 0xFF) ? -1 : static_cast<int>(scrb);
    const unsigned c0 = static_cast<unsigned>((v >> 40) & 0xFF);
    const unsigned c1 = static_cast<unsigned>((v >> 48) & 0xFF);
    static thread_local char buf[2][128];
    static thread_local unsigned slot = 0;
    slot ^= 1u;
    // Single branch: always print both raw picker bytes (even when equal)
    // so old->new lines diff positionally and unknown values stay truthful.
    std::snprintf(buf[slot], sizeof(buf[slot]),
                  "stg=(%u,%u) (char0=%u,char1=%u) fs=%u bk=%u scr=%d", st0,
                  st1, c0, c1, fs, bk, scr);
    return buf[slot];
  }
#endif  // !DOA4_LEAN

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
};
