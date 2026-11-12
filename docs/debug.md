# Debug tooling (dev build only, `DOA4_LEAN=0`)

Everything here is compiled out by the lean play preset. See
`architecture.md` for build profiles.

## F5 debug tab (`Doa4DebugDialog`, `src/doa4retail_app.h`)

Toggled with F5; F6 marks a felt hitch in the log. Content:

- **Live game-state pack** (same pack as `PollGuestState` / `[state]` lines):
  - `fs=1/bk=1` → press-start; `fs=2/bk=2 + scr=5 + stg0==0` →
    main-menu character select; `scr=5 + stg0!=0` → main-menu mode select;
    `scr=8` → stage select; `fs=3` → story; `fs=4` → time attack;
    `fs=5` → versus; `fs=6` → survival/victory; `fs=8` → team battle;
    `fs=9` → sparring; `fs=11/bk=11/scr=8` → watch.
  - The mode-select discriminator is the **first** picker byte only
    (`stg0==0` = character select; `stg1` is not the discriminator —
    S1 evidence: `stg=(0,40)` char0=5 sat there 38% of the run).
- **Stage-select chain** (retail addresses resolved from offline analysis of
  the loaded retail image, tracing the E3 chain by structural signatures,
  verified against the boot dump — the applier's invalidate path stores
  `-1/-1/0x7FFFFFFF`, exactly the values found at these addresses at boot):
  - `0x82C79B18` active stage ID, u16 BE, -1 = none
  - `0x82C79B14` sub-stage ID, u16 BE, -1 = none
  - `0x82C79B1C` stage object ptr, u32 BE, `0x7FFFFFFF` = none
    (`[0]`=self, `[4]`=sub-id, `[6]`=stage ID)
  - `0x83040090` active-stage config struct (word0 = `0x7FFFFFFF` marker;
    u16 at `+0x04/+0x06/+0x08`, per-player rows `+0x208/+0x308`; only the
    `0x310` region the applier writes is dumped, the E3 `0xFB0` size is not
    verified for retail)
  - `0x82FFC0A8` per-slot stage byte table (`0xFF` = invalid; stage entry =
    `0x82FFB5B8 + slot_byte*20`)
  - Chain: scene dispatcher `0x8252C550` (reads `scr` @ `0x82C31C24`,
    -1 = outside) → MSEL slot resolver `0x82388AD8` (sibling `0x82388B88`)
    → applier `0x823B2578` writes the three globals + cfg.
- **Interactions:** `MarkState` (also F6) drops a timestamped USERMARK line
  with the full state pack **plus** the stage chain into the game log at the
  instant of a felt hitch — the primary manual-repro breadcrumb for the
  transition/preview-hitch hunt. The chain section also self-logs its own
  value changes (rate-limited `DOA4DBG chain` lines + on-screen churn rate),
  so unexpected per-frame chain activity — e.g. the applier being re-run
  while a menu is up, which shows as the stage obj flickering — lands in the
  log without a manual mark. "Set cursor[0]" pokes the picker cursor byte
  (the game's own resolver → applier interaction path); the cfg dump is
  read-only. Writing the stage-ID global directly would NOT load anything
  (the applier does the work).
- **Guest memory:** stands in for E3's P1 R3 memory statistics
  (`dbg_mem_stats_on_P1_R3` 0x823BF208 → `MmQueryStatistics`; retail has
  neither). Shows free/total physical and virtual memory computed the same way
  as the runtime's `MmQueryStatistics` (physical heaps 4K/64K/16M page
  counts, 512 MB physical, `0x2FFE0000` virtual); "Log memory stats" writes
  one `DOA4 memory:` line. No pad combo: P1 R3 is taken by the KYN/KYO/TCR
  stage toggles.
- **Stage info (launcher Debug tab, `doa4_stage_info`):** the stage overlay
  names the running stage on every stage (code, slot, file ids, object) and
  the log gets one `DOA4 stage info:` line per stage change. Design:
  `hooks.md`, "Stage info on every stage".
- **Glow tuner:** colour pickers and sliders for the shared glow post-process
  on any stage that uses it (TCR's P1 R3 tuner values). Design: `hooks.md`, "Glow tuner".
- **Chain-churn instrument:** the applier both applies AND clears the chain
  (`arg0 == 0x7FFFFFFF` → invalidate path), so any caller re-running it per
  frame shows up as value churn — on screen as the stage obj flickering, in
  the log as rate-limited `DOA4DBG chain` lines with the change rate and
  exact values.

### Stage FILE ids

33 `{tpr,dat}` pointer pairs at `0x82C5C558` in the shipped image
(byte-verified; differs from the E3 demo order — do not reuse across
targets). RE: `stagesel_commit` writes to `byte_83035CC4` / `byte_83035CC5`;
`144` = random (`stage_random_flag byte_82FCC8E7`). **Not used by
`StateLabel` anymore:** the two live bytes are stage-select **picker
coordinates**, not path-table file ids (R31–R33: each menu move changed
exactly ONE byte, 2D picker one byte per axis incl. the 0→26 wrap; values
36/37/40 exist beyond the 33-entry table; fights keep the confirmed pair —
`(0,12)` = Biolab Core and `(26,12)` = Waterfall Valley are the valid
anchors). The grid holds day/night variants as separate cells (KYO/KYN
pair), which is why picker values run past 33. The table is kept for a
future picker→file map.

### Guest-image dump

When env var `DOA4_DUMP_IMAGE` holds a host path, dump the loaded guest
image there for offline string/xref analysis. No effect unless set.

## State tracker v2

`PollGuestState` packs the state globals into one `int64`, one byte per
field (field *i* = bits 8*i*..8*i*+7):

| Field | Source | Meaning |
|---|---|---|
| [0] | `byte_83035CC0` | game-state byte (fs) |
| [1] | `byte_82FCC93B` | game-state mirror (bk) |
| [2] | `byte_83035CC4` | stage picker side 0 |
| [3] | `byte_83035CC5` | stage picker side 1 |
| [4] | u8 of `0x82C31C24` BE | MSEL screen code (-1 → 0xFF) |
| [5] | `byte_83033B41` | battle record 0 char id |
| [6] | `byte_83033B41 + 156` | battle record 1 char id |
| [7] | reserved 0 | — |

Each byte is event-driven (selection/commit/battle-entry), so transitions
stay sparse. **R30 finding:** `byte_83033B77` (record state) churned 1–3×/s
in every mode and fragmented the per-state attribution rows — dropped from
the pack. Keeping [7] = 0 also makes the `INT64_MIN` "unset" sentinel
impossible (top byte 0 → bit 63 clear).

`StateLabel` decodes the pack (raw `stg=(a,b)` picker pair; the old
"stage:NAME" decoding was a misnomer). It uses a 2-slot rotation buffer:
a transition line formats two labels (old + new) in one `snprintf`, so a
single shared buffer aliases (R30 showed `X -> X` for every transition).

## Draw probe (`src/doa4_frame_probe.cpp`)

TEMP probe (frame interpolation RE, 2026-10-16): which part of one main-loop
iteration issues the GPU work. Every direct child of `frame_systems_tick`
(`sub_824F4878`) and of the main loop (`sub_824F41E0`) is wrapped; the
outermost wrapped function on the stack is the "tag", and every D3D
draw/clear/resolve primitive (found by their PM4 `DRAW_INDX`/`DRAW_INDX_2`
headers) counts against the current tag. Static reach cannot see vtable
draws, this can. Every 600 presents it logs `DRAWPROBE` rows per
(tag, game mode, loading gate) with per-frame averages. Dev build only,
inert unless `DOA4_DRAWPROBE=1`. The loading gate is `r3 != 0` from
`sub_824F4E90` (transition/loading branch ran instead of the gameplay
branch). In `Count`, tag 1 = primitive on a non-main thread, 0 = main
thread outside any tag.
