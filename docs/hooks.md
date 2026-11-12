# Hooks — design notes

One file per hook under `src/`; this doc holds the design rationale that
used to live in long source comments. Source files keep a one-line summary
plus a pointer here.

## Widescreen (`src/doa4_widescreen.cpp`)

Every 3D camera builds its projection in `sub_8231A3D8(fov f1, aspect f2,
near f3, far f4)`: `m00 = 1 / (tan(fov/2) * aspect)`, `m11 = 1 / tan(fov/2)`,
so the field of view is vertical and a wider aspect shows more at the sides
(Hor+). Its view-frustum twin `sub_8231A658(fov f1, ?, aspect f3, near f4,
far f5)` feeds culling (`sub_8276C160`). The HD cameras pass 16:9 (constant
`0x822BCAAC`, or `1 / 0.5625` in `sub_8231A4D8`); other callers pass their own
shape (4:3 constant `0x822BCA40`, render-to-texture cameras) and are left
alone. The guest still renders 1280×720: the presenter stretches it to the
window (`present_letterbox=false`), and the wider projection makes that
stretch come out geometrically correct.

Cvar: `aspect_ratio` (`-1` = auto match the game window, `0` = game default
16:9). Only advances the aspect when the caller passed ~16:9; non-HD callers
keep their own shape.

## Skip intro (`src/doa4_skip_intro.cpp`)

The boot movie part lives in the SDK (`xfile.cpp`, ends the first `.sfd`
opened); this hook covers the disclaimer. Boot runs `sub_82403518`:
`sub_82403610` (the warning/disclaimer screen) and then movie 18
(`ninja_vi_hd.sfd`). `sub_82403610` starts the screen and loops a frame at
a time until its ~54.6 s timer ends (`sub_823A7370`) or `sub_82404AD0(pad)`
sees A/Start on pad 0–3. With `skip_intro` on, that press check answers
"pressed" while the disclaimer runs, so the game takes its own button exit
before the first frame. `sub_82404AD0`'s only other caller is the
low-refresh-rate warning (`sub_82406458`), which is left alone.

## Attract fade (`src/doa4_attract_fade.cpp`)

`sub_82381D10` is the scene-script FADE command: it starts a screen
transition on record 0 (`byte_83047974`, state `dword_83047978`) only when
none is running (state 2 = held, 4 = done); a command that arrives while one
runs is dropped and the script moves on. The attract intro ends with a
90-frame fade to black that holds (record hold byte `0x8304798E`,
fade-to-colour flag bit 1 of `byte_8304790C`), and the next scene's script
clears it. On the console that scene's load always outlasts the fade; here
it can be ready first, its clear is dropped, the fade then holds black, and
the whole demo plays out unseen until the next title. A command dropped
during a holding fade-to-colour is therefore deferred and re-run, unchanged,
on the first transition tick after that fade completes (`sub_823B69A8` ticks
the records once per frame) — where the console's script would have issued
it. The deferred command's first four script halfwords are stashed in
`g_command[8]`; re-run happens on a stand-in script context below the
frame's stack (`sub_82381D10` reads its words through `ctx+28` and advances
`ctx+28/+20`).

## Free camera (`src/doa4_freecam.cpp`, dev build only)

Restores the developer free camera that both the E3 demo (`0x822B18D0`) and
retail (`sub_8231C468`) still contain but never switch on. The camera task
`sub_82312980` runs it instead of a camera's normal update whenever that
camera's byte `+0xF0B` is non-zero, and the game only ever stores 0 there
(init `sub_8231BC00`). The byte also counts the warm-up: the free camera bumps
it each frame and reads input only once it reaches 8. Cameras sit at
`0x8320D110`, `0x10F0` bytes each (5 of them); the main camera's index is the
byte at `0x8321AF6D` (`0xFF` = none; the update order at `0x8321AF58` is not
it).

`sub_82312B50` runs once per camera-task tick before any camera, so the hook
there sets the gate on the main camera while the bind is on and clears it on
every other camera (and on all of them when off). It re-arms every tick
because a camera re-init clears the gate. Pose continuity needs nothing extra:
every camera update ends in `sub_823186A8`, which keeps the orbit fields
(`+0x20` pitch, `+0x24` yaw, `+0x2C` distance) and the eye/target in step, so
the free camera starts from the current view.

Input: the free camera reads pad 0's held bits (`0x831011E0`) directly. The
fight reads per-player copies made by `sub_824060A8` (slot → pad index
`0x830B8C88`, active `0x830B8C84`, records at `0x830366E0`, 44 bytes; an
inactive tag partner gets the same copy), so the hook there blanks every
record copied from pad 0 while the camera is on, keeping only START/BACK so
pausing still works. Menus read the pad records directly and still react.

Controls (pad 0, held): D-pad orbits; Y/A distance; LB or X/B + D-pad moves the
look-at point, Y/A forward/back; RB + D-pad up/down is the field of view
(`+0x30`, radians) and left/right `+0x34`; RT is fast (0.8 vs 0.2).

Bind: `bind_doa4_freecam` (default F8), registered in `OnCreateDialogs`.

## Stage debug tools (`src/doa4_stage_debug.cpp`, dev build only)

**DS event replay.** The E3 demo restarts DS's 100-frame stage event animation
on P1 LB (`ds_dbg_event_anim_trigger_P1_LB`). Retail dropped the trigger but
kept the animation: frame counter `0x82BDBD54` (s32, -1 idle), advanced by
`ds_stage_update` `sub_825DA540` and reset to -1 at 100, started only by
`ds_stage_event_dispatch` case 4 writing 0. The hook on `sub_825DA540` writes
the same 0 when the bind was pressed and logs the start and the end (frame
back to -1). Since that update runs only while DS is the stage, the hook also
stamps the time; a press more than 500 ms after the last DS update is ignored
so it cannot fire on a later DS load.

**ST flipbook tuner.** Retail has an uncalled tuner, `sub_826CBB90` (slot 4 of
the ST flipbook vtable, missing from `.pdata`; codegen still emits it). It
edits one flipbook object (`0x82F19830`, `0x80` bytes each): P1 D-pad
up/down auto-repeat (`0x831011F0`) picks field `+0x78` (0-3), left/right is
±0.01 (×10 per held LB/RB) on scale X/Y/Z `+0x40/+0x44/+0x48` (0-10) or ±1 on
the frame interval `+0x50` (1-10). The hook runs it on the selected object
right after `st_update_flipbooks` `sub_826D0D38` and logs the fields when they
change. That update ticks objects 0-3, plus 4-5 when `0x83036A30 != 1` (not
in a scripted scene), and only while `sub_826CB9C8` holds, which is limited to
some ST scripted scenes and mode-3 areas, so most fights show no flipbook.

**Live retail toggles.** Four stage functions still read the pad and flip a
debug word every frame; no patch is needed, the hooks only call the original,
mark the stage as current and log the word when the call flipped it:
`sub_8261F7C8` KYN and `sub_82631780` KYO (P1 R3, freeze the light preset,
`0x831A6BD8` / `0x831A6BF0`), `sub_82674838` NY2 (P2 R3, extra render pass,
`0x82F6B224`) and `sub_826EA888` TCR (P1 R3, glow tuner `0x831A769C`, index
`0x8357D498` 0-5 over two vec3s `0x83533400` / `0x83533950`; tuner edits are
logged too, nothing else writes those vectors). The TCR vectors are copied to
`0x830C5BC0` and fed as pixel-shader constants to the glow post-process
(`sub_824595F8` / `sub_82459A88`): the first is the per-channel glow
threshold (0-1, default 0.40, lower = more glow), the second the per-channel
glow strength (>= 0, default 1.00). They apply while `dword_831A5FB8 == 0`
and last until the stage reloads.

**PRO light beams.** E3 drew PRO's light beams (`sub_8250F640`) only while
`g_dbg_pro_toggle_P2_RT_R3` was set (default 1, P2 RT+R3 flips it); retail
dropped the toggle. The retail beam pass is `sub_82683888`, called from the
PRO draw `sub_8267DCE8`; it already skips itself while `dword_831A6D34` (a
stage event state, set by `sub_826940E0` / `sub_8267CEA0`) is set or in scene
36 frames 1051-1400. The hook skips the whole call while the host toggle is
off; it is draw-only.

## Glow tuner (`src/doa4_glow_tuner.cpp`, dev build only)

The glow is one shared post-process. A stage fills the parameter block at
`0x830C5BC0` (`0x830C5B90` while `0x831A6354` is set): vec4 threshold, vec4
strength, u32 blur kernel `+0x20` (0-4, case index of the gaussian blur
`sub_82457630`, 0 = widest), f32 blur spread `+0x24` (the gaussian's
variance), then queues it with `sub_824597D0(target, priority, extra blur
passes, alt)`. About 15 stage draws do this (TCR, KYN `sub_8261FB48`, NY2, ST
and others); stages that never call it have no glow. The queued command
`sub_824598D0` sets bit 0 of `0x831A62A0` and `0x8357A148` while the scene
command list runs (`sub_8277A248`); right after it `frame_systems_tick`
resolves the scene and the post-effect dispatcher `sub_82472ED8` calls the
pass `sub_824595F8`, which reads the block at render time.

- **Override:** the `sub_824595F8` hook copies TCR's tuner vectors
  (`0x83533400` / `0x83533950`, set by static init, so they exist on every
  stage) into the block and replaces kernel, spread and passes with host
  values. The stage's own values are captured first for display and "Copy
  this stage's values". On TCR the P1 R3 pad tuner keeps editing the same
  vectors.
- **Stages without glow:** forcing the pass there (setting the queued
  command's flags after `sub_8277A248`) was tried and dropped: in-game the
  pass only brightens what the stage's own materials mark as glowing, so a
  stage with no glow sources shows no change.
- **UI:** F5 tab, "Glow tuner": colour pickers with per-channel fields for
  threshold (0-1) and strength (HDR, >= 0), an all-channel slider for each,
  kernel / spread / passes sliders (all applied only with "Override" on),
  "TCR defaults" (0.40, 1.00, kernel 1,
  spread 16, 8 passes) and the live stage values.

**Stage overlay.** Every stage hook stamps a per-stage time and the guest base.
`StageOverlay` is an `ImGuiDialog` registered only while it has something to
show: the stage hooks, the free-camera toggle and the stage-select label ask
the UI thread to create it, and it removes itself (deferred) the first frame
it draws nothing. It returns false from `WantsContinuousRepaint` (an SDK
addition), so a visible overlay is repainted with the guest output instead of
at the display rate. See "UI repaints and frame rate" below. It shows a corner panel while one of the
seven stage hooks ran in the last 500 ms, or while the free camera is on: the
stage name, its keys/combos and their live state (DS frame, ST tuner object
and fields, light-lock / render-pass / tuner flags and TCR values).

**Stage info on every stage (`doa4_stage_info`, launcher Debug tab).** Adds
the running stage's internal identifiers to the overlay on any stage, and
logs a `DOA4 stage info:` line each time the running stage changes. Retail
ground truth:

- The per-frame stage update `sub_82566818` (a `frame_systems_tick` child)
  calls the running stage object's vtable `+0x14`. It picks the object as
  `g_stage_object_table` `0x82C2A6F8` `[2*slot + side]`, with slot = s32
  `0x82E76830` and side = s32 `0x82E76834`. It only does so while u8
  `0x83033A84` is set. The draw `sub_82566920` does the same with `+0x1C`.
- The frame probe already wraps `sub_82566818`, so the hook sits on its
  only callee `sub_82567658` (state refresh, runs after the loaded-stage
  gate) and only reports while `0x83033A84` is set.
- Shown: code name (slot → name map from the stage file ids, research doc
  §9d), slot, side, the `.tpr`/`_dat.bin` file ids from `g_stage_file_table`
  `0x82C5D6A8`, and the object with its vtable.
- The stage loader `sub_82401390` also writes the loaded slot to
  `stagesel_cursor_a[side]` (`0x83035CC4`), so those "picker" bytes are
  stage slots (0–50).

Binds: `bind_doa4_ds_event` (default F9), `bind_doa4_st_tuner` (default F11,
cycles off → 1 … 6 → off), `bind_doa4_overlay` (default Home, show/hide) and
`bind_doa4_pro_beams` (default End, PRO light beams on/off).
F10 is avoided as a default because Windows treats it as a system key.

**UI repaints and frame rate (SDK presenter).** Any ImGui window (F3, F4,
console, F5 tab, the stage overlay) used to drop the game: attract demo with
no frame interpolation, 60.0 fps → 48.0 fps with F5 open, every frame over
17.5 ms. Ground truth from presenter counters: the UI repainted at the full
144 Hz (1800 presents per 600 guest frames) and each paint took 6.92 ms, one
vblank, because every extra present waits for a free back buffer on the D3D12
queue the guest also renders on. Two causes:

- The achievement toast dialog is always registered and kept the SDK default
  `WantsContinuousRepaint() == true`. `ImGuiDrawer` ORs that flag over all
  dialogs, so as soon as anything else was visible, even our non-continuous
  overlay, the toast switched the UI to display-rate repaints. It is now
  continuous only while a toast is on screen.
- Dialogs that are really continuous (F4 settings) still asked for
  display-rate repaints. Now `Presenter::WaitForUITickFromUIThread` makes a
  drawer-requested repaint wait for the next guest frame while the guest
  output is live (refreshed in the last 100 ms), and goes back to vblank
  pacing when the guest stalls (loading, a XAM dialog the guest blocks on).
  `ForceUIThreadPaintTick` now also wakes that wait. Before, a guest frame
  that arrived while the UI thread was waiting sat there until the next
  vblank.

After the fix, F5 open: 60.0 fps, 600 presents per 600 frames, 0.25 ms per
paint. With interpolation and F5 open: ~59 real fps instead of ~42.

**GPU thread budget with frame interpolation (SDK).** The guest never waits
on itself: its only long ring wait is the flip throttle (`WAIT_REG_MEM` until
the vblank ISR clears `0x1F56D004`), and its main thread spends ~85% spinning
in its D3D library waiting for our GPU thread. That thread does the real
frame, the in-between replay, the pairing and, before, the driver's recording
of every D3D12 command list, serially; when they don't fit in a tick the
in-between frame is skipped ("out of time"). `d3d12_async_submission` (on)
moves the recording and `ExecuteCommandLists` to a worker; swaps and direct
queue signals drain it first, so the presenter's fence still follows the
frame. The replay is handed over in 64 KB pieces to overlap too. Attract A/B
(150 s, RX 7900 XTX, 3x2 scale): busy 12.8 → 11.3 ms per tick, out-of-time
frames 176 → 84. The per-10-second dump frame that the interpolation
diagnostics used to log in every build now needs `frame_interp_diag`.
Ground truth from a packet probe: the swap ends its indirect buffer (NOP
padding) and two `EVENT_WRITE_SHD` fences follow it in the primary ring,
before the flip wait and the first draw. The pairing and replay used to run
at the swap, holding those fences ~7.5 ms; they now run at the next packet
that is neither a NOP nor that fence, or at the end of the primary buffer.
Attract: busy 11.3 → 10.9 ms, out-of-time 384 → 40 of 7800 frames, replay
data restores unchanged.

## All stages in Versus (`src/doa4_all_stages.cpp`, dev build only)

The VS stage select is built by `sub_82537458` from a static 17-entry table at
`0x82C32AF8` (16 bytes: u8 stage id, u8 sub-mode flag, sprite indices at
`+7`/`+11`, u32 kind at `+12`) into the list at `0x8310B728` (ptr, count).
The builder sets every kind to 3 (selectable), then hides by id: 19 (RN)
unless save `+0x3FEF` is set (`sub_82524490`), 23 (AQU) unless the
full-completion check `sub_82537350` passes, 12 (BO) unless any of save
`+0x3FF0..+0x4006` is set; and when `byte_8303683A` (a VS sub-mode chosen
in the versus menu) is set or menu mode is 3, every entry whose byte 1 is 0,
leaving only HEL, PRO and RAY. Ids are stage-table slots (10 DS, 12 BO, 13 TC, 14 MK, 15 NY, 16 TS,
17 CP, 19 RN, 20 PRO, 21 KYO, 23 AQU, 24 SAF, 26 BRI, 31 KYN, 36 RAY, 37 HEL);
`0xFF` is Random.

With `doa4_all_vs_stages` on (and not in a Live session, `0x8310B784`), the
hook sets byte 1 on every static entry for the duration of the call and
hooks `sub_82524490`, which the builder calls once right after pointing the
list at the static table with every kind set to 3. There it copies the 17
entries into a 25-entry list allocated once with `SystemHeapAlloc`, appends
the 8 slots that have stage data on the retail disc but no VS entry (22 MK2,
28 POL, 29 NY2, 30 TS2, 35 DAN, 38 BAB, 40 TCR, 41 KYR) with kind 3 and the
Random entry's sprites
(`0x00080018` thumbnail, `0x00080024` name), ORs `0x80` into ids 12/19/23 in
the copy (and into `r3`, the start stage) so none of the gate checks match,
and repoints `0x8310B728` at it. Everything after that call (counts, cursor,
the grid `sub_8252C8C0`) and every reader of the list uses the pointer and
count, not 17: the cell draw `sub_82538600` (thumbnail = entry `+4`), the
name sprite in `sub_82538438` (entry `+8` of the entry whose id is the
cursor stage `0x8310B710`), the input handler and the confirm resolve
`sub_825382A8`. Confirm only re-randomizes when the id is `0xFF` or the
kind is not 3, so the extra entries keep their id. The commit path
(`sub_8252C428` → `sub_8231F140`, the per-stage camera/BGM list, 51 rows)
is id-generic. Afterwards the ids, byte 1 and the selected id are restored.
Save data is never written. It logs how many entries ended up selectable.

Left out: 1 MDL, 2 GL1, 3 WAL, 4 WL2, 8 LOB, 11 ST, 18 MT, 25 SNW, 27 SBE.
Their entries in the per-stage file table (`0x82C5D6A8`, two u16 `stg.afs`
indices per stage, `stg.afs` starting at index `0x252`) point at 16-byte
stubs. Highlighting a stage in the select screen already starts its full
background load (`stagesel_commit` → staged flag, loader `sub_82401390`), so
the cursor reaching such a slot handed the model parser `sub_8277E158` a
null buffer and crashed the loader thread.

The extra entries have no art of their own, so the stage overlay shows a
centered label with the stage code and slot while the cursor is on one
(`sub_82538438` marks the stage-select screen as current). `sub_825382A8`
(confirm) is hooked only to log the confirmed stage id. Tested: MK2, POL,
TCR, BAB and KYR load and play in a Versus match; NY2, TS2 and DAN not yet.

## Preview cut (`src/doa4_preview_cut.cpp`)

`sub_823B7890` is a scene-script element handler (table `0x822C8F08`, tag
`0x40009F04`). On each background camera cut it resolves the current frame
into its texture (`obj[16]`, when `obj+72` is set) and then, every frame,
queues a full-screen sprite of that still (`byte (1 - f1) * 255`) over the
live scene for the transition's 20 frames (`f1 = k/19`, measured R38). It is
transition type 1 of `off_82C794C8`, driven by the transition controller
`sub_823B6C30`. The still sits over a camera that keeps moving (R33–R34
constants), which reads as the picture holding after every cut (PIXHOLD 10
frames per preview cut). Pinning `f1` to 0.0 or 1.0 (R39/R40) both held it
for all 20 frames, so the overlay is skipped outright: the handler only
captures and queues the still; the camera switch and the controller's
counters/states live elsewhere, so every cut becomes a clean hard cut. The
hook body is therefore empty (no-op).

## Preview wind (`src/doa4_preview_wind.cpp`)

Stage cloth (MK awnings and flags) is pushed by wind source 0 (`0x82E87860`),
which the stage activation `sub_82566E80` sets up through `sub_8257E840` from
the per-stage table `off_82C26ED0`, indexed by
`stagesel_cursor_a[stage_side_current]`. It does this for every mode except
3 (`byte_830318FB`, the VS stage-select preview), so a previewed stage keeps
the title stage's wind. KYO's entry sweeps the wind direction by half a turn
every 20 frames, which made the MK awnings rock hard in the preview. A
fight, as checked on a real 360, uses MK's own slow sway. The hook runs
`sub_8257E840` after the activation in mode 3, as a fight does, but only when
the cursor index matches the stage being activated (`dword_82E76830`).
