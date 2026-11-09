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

// Lean play profile (build preset win-amd64-relwithdebinfo-lean): compiles
// out ALL project-side instrumentation -- the F5/F6 debug tab, the state
// poller / [state] label, and the DOA4_DUMP_IMAGE hook -- so play sessions
// carry zero debug overhead. The SDK's TEMP probes stay in the DLLs but are
// made inert at runtime by REX_DIAG=0 (see start_doa4_lean.ps1). Default 0:
// the dev build keeps everything.
#ifndef DOA4_LEAN
#define DOA4_LEAN 0
#endif

// State poll address (state/stage work): guest u32 polled once per frame by
// rex::perf::PollState as the current state id. 0x82C31C24 = MSEL screen
// code global (RE: written by menu dispatcher sub_8252C550 with the matched
// screen code; reset to -1 by sub_8253D960 when the menu system tears down
// -> >= 0 = menu screen, -1 = outside the menus: gameplay / title / load).
// v2 (state/stage work): OnPostSetup registers SetStatePollFn instead, which
// packs several RE'd globals (game state, stage ids, chars, screen code,
// record state) into one int64 -- this address stays as the v1 fallback and
// is ignored while the poll fn is set. Declared here and set in OnPostSetup
// so the SDK stays game-agnostic.
REXCVAR_DECLARE(uint32_t, perf_state_addr);

#if !DOA4_LEAN
// ---------------------------------------------------------------------------
// DOA4 debug tab (retail), toggled with F5; F6 marks a felt hitch in the log.
//
// Content: the live game-state pack (see PollGuestState for the addresses and
// the user-decoded mode map: fs=1/bk=1 press-start; fs=2/bk=2 + scr=5 +
// stg0==0 main-menu character select; scr=5 + stg0!=0 main-menu mode select;
// scr=8 stage select; fs=3 story; fs=4 time attack; fs=5 versus; fs=6
// survival/victory; fs=8 team battle; fs=9 sparring; fs=11/bk=11/scr=8
// watch) plus the live stage-select
// chain (retail addresses resolved 2026-10-13 from the loaded image, see
// E3_to_retail.md section 8: applier 0x823B2578 <-> globals 0x82C79B18/14/1C,
// cfg 0x83040090, slot table 0x82FFC0A8, scene dispatcher 0x8252C550).
//
// Interactions: MarkState (also F6) drops a timestamped USERMARK line with
// the full state pack PLUS the stage chain into the game log at the instant
// of a felt hitch -- the primary manual-repro breadcrumb for the
// transition/preview-hitch hunt. The chain section also self-logs its own
// value changes (rate-limited "DOA4DBG chain" lines + an on-screen churn
// rate), so unexpected per-frame chain activity -- e.g. the applier being
// re-run while a menu is up, which shows as the stage obj flickering --
// lands in the log without a manual mark. "Set cursor[0]" pokes the picker
// cursor byte (the game's own resolver -> applier interaction path); the
// cfg dump is read-only.
class Doa4DebugDialog : public rex::ui::ImGuiDialog {
 public:
  using GuestBaseFn = std::function<uint8_t*()>;
  using MarkFn = std::function<void(const char* why)>;

  // RETAIL addresses RESOLVED 2026-10-13 by offline analysis of the loaded
  // (decompressed) retail image, tracing the documented E3 chain by
  // structural signatures (see E3_to_retail.md section 5). Verified against
  // the boot dump: the applier's invalidate path stores -1/-1/0x7FFFFFFF,
  // exactly the values found at these addresses at boot.
  //   0x82C79B18: active stage ID, u16 BE, -1 = none   (E3: word_82A777A0)
  //   0x82C79B14: sub-stage ID,    u16 BE, -1 = none   (E3: word_82A7779C)
  //   0x82C79B1C: stage object ptr, u32 BE, 0x7FFFFFFF = none
  //               ([0]=self, [4]=sub-id, [6]=stage ID) (E3: dword_82A777A4)
  //   0x83040090: active-stage config struct; word0 = 0x7FFFFFFF marker;
  //               u16 at +0x04/+0x06/+0x08, per-player rows +0x208/+0x308
  //               (E3: dword_82CEF158)
  //   0x82FFC0A8: per-slot stage byte table, 0xFF = invalid; resolved by
  //               slot resolver 0x82388AD8 -> stage entry =
  //               0x82FFB5B8 + slot_byte*20 (E3: byte_82CE8D48)
  // Chain: scene dispatcher 0x8252C550 (reads scr @0x82C31C24, -1 = outside)
  //        -> MSEL slot resolver 0x82388AD8 (sibling 0x82388B88)
  //        -> applier 0x823B2578 writes the three globals + cfg.
  static constexpr uint32_t kRetailActiveStageId = 0x82C79B18;   // E3: 0x82A777A0
  static constexpr uint32_t kRetailSubStageId = 0x82C79B14;      // E3: 0x82A7779C
  static constexpr uint32_t kRetailActiveStageObj = 0x82C79B1C;  // E3: 0x82A777A4
  static constexpr uint32_t kRetailStageCfg = 0x83040090;        // E3: 0x82CEF158
  static constexpr uint32_t kRetailSlotTable = 0x82FFC0A8;       // E3: 0x82CE8D48
  static constexpr uint32_t kRetailPickerCursor = 0x83035CC4;    // picker byte 0
  // Dump only the region the applier actually writes (through the +0x308
  // per-player row); the E3 0xFB0 size is not verified for retail.
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

      // Chain churn instrument. The applier (0x823B2578) both applies AND
      // clears the chain (arg0 == 0x7FFFFFFF -> invalidate path), so any
      // caller re-running it per frame shows up here as value churn -- on
      // screen as the stage obj flickering, in the log as rate-limited
      // "DOA4DBG chain" lines with the change rate and exact values.
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

      // --- Interactive: move the picker cursor / dump config ---------------
      // Writing the CURSOR byte is the game's own interaction path (the same
      // thing moving the picker does): slot resolver 0x82388AD8 -> applier
      // 0x823B2578 then update the stage globals itself. Writing the stage-ID
      // global directly would NOT load anything (the applier does the work).
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

  // User-decoded retail mode map (ground truth from manual playtests; see
  // the class comment). Unrecognized combos print as "other (fs,bk)".
  static const char* ModeName(unsigned fs, unsigned bk, int scr, unsigned st0,
                              unsigned st1) {
    if (fs == 1 && bk == 1) return "press-start";
    if (fs == 2 && bk == 2) {
      if (scr == 8) return "stage select";
      // Discriminator is the FIRST picker byte only (user-confirmed):
      // stg0==0 = character select (S1 evidence: stg=(0,40) char0=5 sat
      // there 38% of the run -- st1 is not the discriminator), stg0!=0 =
      // the mode-select menu layer.
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

  // User-confirmed picker->stage anchors (2D picker: one byte per axis).
  static const char* PickerAnchor(unsigned st0, unsigned st1) {
    if (st0 == 0 && st1 == 12) return "Biolab Core";
    if (st0 == 26 && st1 == 12) return "Waterfall Valley";
    return nullptr;
  }

  // Stage FILE id -> code, RETAIL order (33 {tpr,dat} pairs at 0x82C5C558;
  // 144 = random). Kept for the pending picker->file map.
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

  // DOA4 debug tab (F5 toggle, F6 mark): see Doa4DebugDialog above.
#if !DOA4_LEAN
  std::unique_ptr<rex::ui::ImGuiDialog> debug_tab_;
#endif

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Doa4retailApp>(new Doa4retailApp(ctx, "doa4retail",
        PPCImageConfig));
  }

#if !DOA4_LEAN
  // TEMP DIAGNOSTIC (state/stage RE, armed 2026-10-08): when the
  // DOA4_DUMP_IMAGE env var holds a host path, dump the loaded guest image
  // there for offline string/xref analysis (locating state / stage / char
  // globals). No effect unless the env var is set; stays armed until the
  // state tracker ships.
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

  // TEMP DIAGNOSTIC (state tracker v2, armed 2026-10-08): point the SDK's
  // once-per-frame state poll at the game-side packer below (v2, IDA-RE'd
  // globals) and register the labeler used by the [state] log lines, the
  // debug overlay state row and the per-state fps summary at exit. Frame
  // attribution comes from the perf CSV state_id column (one byte per
  // field, low byte first -- see PollGuestState). The v1 address poll stays
  // set as the documented fallback: it is ignored while the poll fn is
  // registered (SDK precedence), so removing SetStatePollFn restores v1.
  // Menu-vs-fight discrimination in the label is finalized from the R30
  // evidence (value spaces of fs/bk/rec); stays armed until then.
  void OnPostSetup() override {
    rex::perf::SetStatePollFn(&Doa4retailApp::PollGuestState);
    rex::perf::SetStateLabelFn(&Doa4retailApp::StateLabel);
    REXCVAR_SET(perf_state_addr, 0x82C31C24u);  // v1 fallback (see above)
  }

  // DOA4 debug tab (transition/preview-hitch hunt tooling): F5 toggles the
  // in-game state view (Doa4DebugDialog), F6 drops a USERMARK breadcrumb
  // line with the full state pack into the game log at the moment of a felt
  // hitch. F5/F6 are free of SDK binds (F3 debug, F4 settings, F7
  // achievements, Backtick console); both show up rebindable in the settings
  // overlay. Kept app-side so the SDK stays game-agnostic.
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

  // Timestamped breadcrumb: pressed by the user the instant they feel a
  // hitch (F6 or the debug-tab button). The log line carries the same state
  // pack as the [state] attribution, so the run log and the perf CSV can be
  // aligned to the felt event instead of to a guessed timestamp.
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
    // Stage chain (retail addresses resolved 2026-10-13, E3_to_retail.md
    // section 8): active stage id / sub id (u16 BE, -1 = none) and the stage
    // object pointer (u32 BE, 0x7FFFFFFF = none). Appended so every mark
    // captures what the stage system was doing at the felt event.
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

  // TEMP DIAGNOSTIC (state tracker v2): packs the state globals found by
  // IDA RE into one int64, one byte per field (field i = bits 8i..8i+7):
  //   [0]  byte_83035CC0        game-state byte (fs)
  //   [1]  byte_82FCC93B        game-state mirror (bk)
  //   [2]  byte_83035CC4        stage id side 0 (0..32 retail path-table
  //                             order at 0x82C5C558, 144 = random)
  //   [3]  byte_83035CC5        stage id side 1
  //   [4]  u8 of 0x82C31C24 BE  MSEL screen code (-1 -> 0xFF)
  //   [5]  byte_83033B41        battle record 0 char id
  //   [6]  byte_83033B41 + 156  battle record 1 char id
  //   [7]  reserved 0
  // Each byte is event-driven (selection/commit/battle-entry), so transitions
  // stay sparse; fs/bk are the fight-vs-menu discriminator candidates this
  // run observes. R30 finding: byte_83033B77 (record state) was field [7]
  // and churned 1-3x/sec in EVERY mode (menus and fights) -> it fragmented
  // the per-state attribution rows and spammed transitions, so it was
  // dropped from the pack (keep it out); [7] stays 0, which also makes the
  // INT64_MIN "unset" sentinel impossible (top byte 0 -> bit 63 clear).
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

  // Stage FILE id -> code, RETAIL order: 33 {tpr,dat} pointer pairs at
  // 0x82C5C558 in the shipped image (byte-verified; differs from the E3
  // demo order -- do not reuse across targets). RE: stagesel_commit writes
  // to byte_83035CC4 (stage-select byte 0) / byte_83035CC5 (byte 1);
  // 144 = random (stage_random_flag byte_82FCC8E7).
  //
  // NOT used by StateLabel anymore: R31-R33 showed the two live bytes are
  // stage-select PICKER coordinates, not path-table file ids -- during the
  // R33 cursor walk each menu move changed exactly ONE byte (2D picker:
  // one byte per axis, incl. the 0->26 wrap) while hovering known stages,
  // values (36/37/40) exist beyond the 33-entry table, and the fight runs
  // keep whatever pair the picker confirmed ((0,12)=Biolab Core and
  // (26,12)=Waterfall Valley are the user-confirmed anchors). The grid
  // holds day/night variants as separate cells (user: KYO=Kyoto in Bloom,
  // playable day+night -> path table KYO/KYN pair), which is why picker
  // values run past 33. Keep this table for a future picker->file map.
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

  // Labeler for the packed value produced by PollGuestState (see
  // OnPostSetup). Raw decode of every field. The two stage bytes print as
  // a RAW pair "stg=(a,b)" -- R33 proved they are stage-select picker
  // coordinates (each cursor move flips exactly one byte), so the old
  // "stage:NAME" decoding (path-table file names) was a misnomer; names
  // stay in the ledger as anchors only: (0,12)=Biolab Core,
  // (26,12)=Waterfall Valley. fs/bk/scr carry menu-vs-fight context
  // (fs=5+scr=8 real fight, fs=1 demo/attract, fs=2 menu).
  //
  // Return-buffer contract: a transition line (counter.cpp) formats TWO
  // labels (old + new) in ONE snprintf, so a single shared static buffer
  // aliases (R30 showed "X -> X" for every transition: both pointers hit the
  // same thread_local slot and the second call won). Use a 2-slot rotation
  // so two consecutive StateLabel calls return distinct buffers.
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
