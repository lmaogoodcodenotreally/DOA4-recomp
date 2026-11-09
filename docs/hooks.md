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
