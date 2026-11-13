use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
use std::path::Path;

#[derive(Debug, Clone)]
pub struct Setting {
    pub key: &'static str,
    pub kind: Kind,
    pub default: &'static str,
    pub label: &'static str,
    /// One or two plain sentences for a player who has never seen this cvar.
    /// Shown as a tooltip in the GUI and by `--list` in the console launcher.
    pub help: &'static str,
    pub choices: Vec<(&'static str, &'static str)>,
    /// Hidden behind "Advanced" in the GUI: useful, but not a first-run choice.
    pub advanced: bool,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Kind { Bool, Int, Double, Str, Key }

fn setting(
    key: &'static str, kind: Kind, default: &'static str, label: &'static str,
    help: &'static str, choices: &[(&'static str, &'static str)],
) -> Setting {
    Setting { key, kind, default, label, help, choices: choices.to_vec(), advanced: false }
}

fn advanced(
    key: &'static str, kind: Kind, default: &'static str, label: &'static str,
    help: &'static str, choices: &[(&'static str, &'static str)],
) -> Setting {
    Setting { advanced: true, ..setting(key, kind, default, label, help, choices) }
}

pub fn display() -> Vec<Setting> {
    vec![
        setting("fullscreen", Kind::Bool, "true", "Fullscreen",
            "Fill the selected monitor. Turn this off to play in a resizable window.", &[]),
        setting("monitor", Kind::Int, "0", "Monitor",
            "Which monitor to open on. Default follows the primary monitor Windows is set to.",
            &[("0", "Default"), ("1", "Monitor 1"), ("2", "Monitor 2"), ("3", "Monitor 3")]),
        setting("aspect_ratio", Kind::Double, "-1", "Aspect ratio",
            "Shape of the 3D camera. Auto matches the game window, so an ultrawide display fills \
             edge to edge instead of showing a 16:9 image with black bars at the sides. Only takes \
             effect with \"Keep 16:9\" turned off.",
            &[("-1", "Auto (match the window)"), ("0", "16:9 (game default)"), ("1.333333", "4:3"),
              ("1.6", "16:10"), ("2.370370", "21:9 (2560x1080)"), ("2.388889", "21:9 (3440x1440)"),
              ("2.4", "21:9 (3840x1600)"), ("3.555556", "32:9")]),
        setting("present_letterbox", Kind::Bool, "false", "Keep 16:9 (letterbox)",
            "Pad the picture out to 16:9 with black bars instead of filling the screen. Leave this \
             off for widescreen; turn it on if you want the original framing exactly.", &[]),
        setting("frame_interp", Kind::Bool, "false", "Frame interpolation",
            "Experimental. Inserts generated in-between frames: double the visual smoothness, \
             and up to your monitor's refresh rate on faster screens when the PC has time to \
             spare. The game's own logic still runs at 60 Hz, and fast motion can show \
             artefacts, so turn it off again if anything looks wrong.", &[]),
        setting("d3d12_allow_variable_refresh_rate_and_tearing", Kind::Bool, "false", "Screen sync",
            "V-Sync locks to your monitor and avoids tearing. Off lets the image tear, which is \
             what you want on a G-Sync or FreeSync display.",
            &[("false", "V-Sync"), ("true", "Off (tearing / variable refresh)")]),
        advanced("window_width", Kind::Int, "0", "Window width",
            "Window width in pixels when not fullscreen. 0 picks a size from the current display mode.", &[]),
        advanced("window_height", Kind::Int, "0", "Window height",
            "Window height in pixels when not fullscreen. 0 picks a size from the current display mode.", &[]),
    ]
}

pub fn graphics() -> Vec<Setting> {
    vec![
        setting("resolution_scale", Kind::Int, "2", "Internal resolution",
            "Renders the 3D image at this multiple of the Xbox 360's own resolution and scales it \
             down, which is the most effective way to sharpen the picture - and the most expensive. \
             Takes effect after a restart.",
            &[("1", "1x (as the console)"), ("2", "2x"), ("3", "3x"), ("4", "4x"),
              ("5", "5x"), ("6", "6x"), ("7", "7x"), ("8", "8x")]),
        setting("present_effect", Kind::Str, "cas", "Output scaling",
            "How the finished frame is fitted to your window. CAS sharpens at native size; the FSR \
             options upscale from a lower internal resolution. Everything except Bilinear needs a \
             FidelityFX build, which the compiler cannot produce when the install path contains \
             non-ASCII characters.",
            &[("bilinear", "Bilinear (plain)"), ("cas", "CAS (sharpen)"), ("fsr", "FSR 1"),
              ("fsr2", "FSR 2"), ("fsr3", "FSR 3")]),
        setting("native_2x_msaa", Kind::Bool, "true", "Native 2x MSAA",
            "Hardware anti-aliasing. Smooths the edges of 3D geometry without blurring textures, \
             and is usually cheaper than the post-process option below.", &[]),
        setting("swap_post_effect", Kind::Str, "none", "Anti-aliasing (post)",
            "Smooths jagged edges after the frame is drawn. Cheap, and catches edges MSAA misses, \
             but softens the whole image slightly.",
            &[("none", "Off"), ("fxaa", "FXAA"), ("fxaa_extreme", "FXAA extreme")]),
        setting("anisotropic_override", Kind::Int, "3", "Texture filtering",
            "Keeps textures sharp when seen at a steep angle, such as the arena floor. Costs very \
             little on any modern GPU.",
            &[("-1", "Game default"), ("0", "Off"), ("1", "1x"), ("2", "2x"), ("3", "4x"),
              ("4", "8x"), ("5", "16x")]),
        setting("scaled_point_filter_linear", Kind::Bool, "true", "Smooth 2D art",
            "Smooths menus and HUD artwork when the internal resolution is above 1x. Turn it off \
             for a sharper, more pixelated look.", &[]),
        setting("ssao_strength", Kind::Double, "0", "Ambient occlusion",
            "Soft contact shadows in creases and where objects meet, such as cloth folds and the \
             base of railings. Needs an internal resolution above 1x. Costs a little GPU time.",
            &[("0", "Off"), ("0.6", "Subtle"), ("1", "Strong")]),
        setting("shadow_quality", Kind::Int, "0", "Character shadows",
            "Draws the fighters' shadows on the floor from a much sharper silhouette, with soft \
             edges, instead of the console's small blurred one. Costs very little GPU time.",
            &[("0", "Original"), ("1", "Sharp"), ("2", "Sharpest")]),
        advanced("shadow_softness", Kind::Double, "0.5", "Character shadow softness",
            "How soft the edges of the sharper character shadows are. Only applies when Character \
             shadows is not Original.",
            &[("0.25", "Crisp"), ("0.5", "Default"), ("1", "Very soft")]),
        advanced("draw_resolution_scale_x", Kind::Int, "1", "Render scale X",
            "Per-axis render scale. Anything above 1 overrides Internal resolution for this axis, \
             so 3 x 2 renders wider than it does tall. Takes effect after a restart.",
            &[("1", "1x"), ("2", "2x"), ("3", "3x"), ("4", "4x"), ("5", "5x"), ("6", "6x"), ("7", "7x"), ("8", "8x")]),
        advanced("draw_resolution_scale_y", Kind::Int, "1", "Render scale Y",
            "Per-axis render scale. Anything above 1 overrides Internal resolution for this axis. \
             Takes effect after a restart.",
            &[("1", "1x"), ("2", "2x"), ("3", "3x"), ("4", "4x"), ("5", "5x"), ("6", "6x"), ("7", "7x"), ("8", "8x")]),
        advanced("gpu_upload_prefetch", Kind::Bool, "true", "Batched geometry uploads",
            "Sends the game's moving geometry to the GPU in one batch at the end of each frame \
             instead of piece by piece while drawing. Saves roughly a fifth of the GPU's time and \
             leaves more room for frame interpolation. Turn it off only if something looks wrong.",
            &[]),
    ]
}

pub fn input() -> Vec<Setting> {
    vec![
        setting("input_backend", Kind::Str, "sdl", "Controller backend",
            "SDL recognises the widest range of controllers. XInput only handles Xbox-style pads \
             but can be more predictable with them.",
            &[("sdl", "SDL"), ("xinput", "XInput")]),
        setting("mnk_mode", Kind::Bool, "true", "Keyboard as controller",
            "Lets the keyboard stand in for a controller, so you can play without a pad.", &[]),
        setting("skip_intro", Kind::Bool, "true", "Skip intro",
            "Goes straight to the menu instead of playing the logos and opening movie.", &[]),
        setting("audio_mute", Kind::Bool, "false", "Mute audio", "Silences all game sound.", &[]),
        advanced("hid_sdl_directinput", Kind::Bool, "false", "Legacy DirectInput pads",
            "Only needed for controllers older than XInput. Leave it off: enabling it adds a \
             polling delay that shows up as input lag.", &[]),
    ]
}

/// Key names the game's bind parser accepts (thirdparty/rexglue-sdk/src/ui/keybinds.cpp),
/// minus the modifiers and mouse buttons, which make poor single-key binds.
pub const KEY_NAMES: &[&str] = &[
    "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
    "F13", "F14", "F15", "F16", "F17", "F18", "F19", "F20", "F21", "F22", "F23", "F24",
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R",
    "S", "T", "U", "V", "W", "X", "Y", "Z", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    "Backtick", "Minus", "Plus", "Comma", "Period", "Semicolon", "Slash", "Backslash",
    "LBracket", "RBracket", "Quote", "Escape", "Return", "Space", "Tab", "Backspace", "Delete",
    "Insert", "Home", "End", "PageUp", "PageDown", "Left", "Right", "Up", "Down",
    "Numpad0", "Numpad1", "Numpad2", "Numpad3", "Numpad4", "Numpad5", "Numpad6", "Numpad7",
    "Numpad8", "Numpad9", "NumpadPlus", "NumpadMinus", "NumpadStar", "NumpadSlash",
    "PrintScreen", "Pause", "CapsLock", "NumLock", "ScrollLock",
];

/// Binds the runtime itself registers (thirdparty/rexglue-sdk/src/ui/rex_app.cpp, defaults).
pub const RUNTIME_BINDS: &[(&str, &str)] = &[
    ("F3", "the runtime's debug overlay"), ("F4", "the runtime's settings overlay"),
    ("F7", "the runtime's achievements overlay"), ("Backtick", "the runtime's console"),
];

/// What else already uses `key`, if anything: another of our binds or a runtime one.
pub fn bind_conflict(cfg: &Config, own: &str, key: &str) -> Option<String> {
    if let Some((_, what)) = RUNTIME_BINDS.iter().find(|(k, _)| *k == key) {
        return Some(what.to_string());
    }
    all().into_iter()
        .filter(|s| s.kind == Kind::Key && s.key != own)
        .find(|s| cfg.game.get(s.key).map(String::as_str).unwrap_or(s.default) == key)
        .map(|s| s.label.to_string())
}

/// Developer-build switches. The play build compiles their hooks out, so
/// `managed` only writes them when the developer build is selected.
pub fn debug() -> Vec<Setting> {
    vec![
        setting("doa4_all_vs_stages", Kind::Bool, "false", "All stages in Versus",
            "Makes every stage on the Versus stage-select screen selectable, including the ones \
             your save has not unlocked yet (RN, AQU, BO) and those a Versus sub-mode normally \
             hides. It also adds the 8 stages that never had a Versus entry but are on the \
             disc (MK2, POL, NY2, TS2, DAN, BAB, TCR, KYR) at the end of the list, with the \
             Random picture as a placeholder and the real stage name shown on screen. Nothing is \
             written to your save. Offline only.", &[]),
        setting("doa4_stage_info", Kind::Bool, "false", "Stage info on every stage",
            "Adds the running stage's internal identifiers to the stage overlay on every stage, not \
             only the ones with developer controls: its code name (e.g. TCR), slot number, file ids \
             and stage object. Hidden with the stage overlay key like the rest of the panel.", &[]),
        setting("bind_doa4_debug", Kind::Key, "F5", "Debug tab key",
            "Shows or hides the developer debug window: live game state, the stage chain, guest \
             memory and the glow tuner (colour pickers and sliders for the stage glow).", &[]),
        setting("bind_doa4_mark", Kind::Key, "F6", "Log mark key",
            "Writes a marker line with the current game state into the log, to point at the moment \
             something looked or felt wrong.", &[]),
        setting("bind_doa4_freecam", Kind::Key, "F8", "Free camera key",
            "Turns the debug free camera on and off. While it is on, the first controller moves the \
             camera instead of your fighter: the D-pad orbits, Y/A zoom, LB or X/B with the D-pad \
             moves the point it looks at, RB with the D-pad changes the field of view, and holding RT \
             goes faster. Start still pauses.",
            &[]),
        setting("bind_doa4_ds_event", Kind::Key, "F9", "DS event replay key",
            "On the DS stage, replays the stage's event animation - the manual trigger the E3 demo \
             had on LB.",
            &[]),
        setting("bind_doa4_st_tuner", Kind::Key, "F11", "ST flipbook tuner key",
            "On the ST stage, turns on the developers' unused tuner for the animated flipbook \
             effects and steps through them one press at a time (off, 1, 2, ... 6, off). The first \
             controller's D-pad up/down picks scale X, Y, Z or speed, left/right changes it, and LB/RB \
             make each step bigger. The flipbooks only appear in some ST scenes.",
            &[]),
        setting("bind_doa4_overlay", Kind::Key, "Home", "Stage overlay key",
            "Shows or hides the corner panel that appears on stages with developer controls (DS, \
             ST, KYN, KYO, NY2, TCR, PRO), while the free camera is on, and on every stage when \
             \"Stage info on every stage\" is on. It lists each stage's keys and combos with their \
             current state; every toggle is also written to the log.",
            &[]),
        setting("bind_doa4_pro_beams", Kind::Key, "End", "PRO light beams key",
            "On the PRO stage, turns the stage's light beams off and on - the switch the E3 demo had \
             on the second controller (RT + R3). They start on.",
            &[]),
    ]
}

pub fn all() -> Vec<Setting> { [display(), graphics(), input(), debug()].concat() }

/// A one-click graphics profile. Only the graphics group is touched, so a
/// player's display and input choices survive switching between them.
pub struct Preset {
    pub name: &'static str,
    pub help: &'static str,
    pub values: &'static [(&'static str, &'static str)],
}

/// Ordered cheapest first. "3x plus CAS" is the practical ceiling on this
/// engine - 4x stutters - so Quality stops there rather than at 8x.
pub fn presets() -> &'static [Preset] {
    &[
        Preset {
            name: "Performance",
            help: "Native resolution, cheapest anti-aliasing. For laptops and older GPUs.",
            values: &[
                ("resolution_scale", "1"), ("draw_resolution_scale_x", "1"), ("draw_resolution_scale_y", "1"),
                ("native_2x_msaa", "false"), ("swap_post_effect", "fxaa"),
                ("anisotropic_override", "2"), ("present_effect", "cas"),
                ("scaled_point_filter_linear", "true"), ("ssao_strength", "0"),
                ("shadow_quality", "0"),
            ],
        },
        Preset {
            name: "Balanced",
            help: "Twice the internal resolution with hardware anti-aliasing. A good default.",
            values: &[
                ("resolution_scale", "2"), ("draw_resolution_scale_x", "1"), ("draw_resolution_scale_y", "1"),
                ("native_2x_msaa", "true"), ("swap_post_effect", "none"),
                ("anisotropic_override", "3"), ("present_effect", "cas"),
                ("scaled_point_filter_linear", "true"), ("ssao_strength", "0"),
                ("shadow_quality", "0"),
            ],
        },
        Preset {
            name: "Quality",
            help: "Three times the internal resolution, the practical ceiling before the \
                   frame rate suffers. Wants a desktop GPU.",
            values: &[
                ("resolution_scale", "3"), ("draw_resolution_scale_x", "1"), ("draw_resolution_scale_y", "1"),
                ("native_2x_msaa", "true"), ("swap_post_effect", "fxaa"),
                ("anisotropic_override", "5"), ("present_effect", "cas"),
                ("scaled_point_filter_linear", "true"), ("ssao_strength", "0.6"),
                ("shadow_quality", "2"),
            ],
        },
    ]
}

/// Which preset the current graphics settings correspond to, or `None` for
/// "Custom". Derived rather than stored, so hand-editing launcher.json or
/// changing one dropdown lands on Custom without anything to keep in sync.
pub fn active_preset(cfg: &Config) -> Option<&'static str> {
    presets()
        .iter()
        .find(|p| {
            p.values.iter().all(|(k, v)| {
                cfg.game.get(*k).map(String::as_str)
                    == Some(*v)
            })
        })
        .map(|p| p.name)
}

pub fn apply_preset(cfg: &mut Config, name: &str) {
    if let Some(p) = presets().iter().find(|p| p.name == name) {
        for (k, v) in p.values {
            cfg.game.insert(k.to_string(), v.to_string());
        }
    }
}

/// The aspect ratio of a display, formatted the way the cvar wants it.
pub fn aspect_for(width: f32, height: f32) -> Option<String> {
    (width > 0.0 && height > 0.0).then(|| format!("{:.6}", width / height))
}

pub fn to_toml(s: &Setting, value: &str) -> String {
    match s.kind { Kind::Str | Kind::Key => format!("\"{}\"", value), Kind::Double => format!("{:.6}", value.parse::<f64>().unwrap_or(0.0)), _ => value.to_string() }
}

#[derive(Default, Serialize, Deserialize)]
pub struct Config { pub lean_exe: String, pub dev_exe: String, pub data_root: String, pub use_dev: bool, pub game: BTreeMap<String, String> }

pub fn load_cfg(app_dir: &Path) -> Config {
    let f = app_dir.join("launcher.json");
    if !f.exists() { return Config::default(); }
    serde_json::from_str(&std::fs::read_to_string(f).unwrap_or_default()).unwrap_or_default()
}
pub fn save_cfg(app_dir: &Path, c: &Config) {
    let _ = std::fs::write(app_dir.join("launcher.json"), serde_json::to_string_pretty(c).unwrap_or_default());
}

pub fn toml_merge(existing: &str, managed: &BTreeMap<String, String>) -> String {
    let mut pending = managed.clone();
    let mut out = String::new();
    for line in existing.split('\n') {
        let t = line.trim_end_matches('\r');
        if let Some(eq) = t.find('=') {
            let key = t[..eq].trim();
            if !t.trim_start().starts_with('#') && pending.contains_key(key) { out.push_str(&format!("{} = {}\n", key, pending.remove(key).unwrap())); continue; }
        }
        if !t.is_empty() { out.push_str(&format!("{}\n", t)); }
    }
    for (k, v) in pending { out.push_str(&format!("{} = {}\n", k, v)); }
    out
}

pub fn auto_detect(app_dir: &Path) -> (String, String, String) {
    let mut lean = String::new(); let mut dev = String::new(); let mut data = String::new();
    if app_dir.join("doa4retail.exe").exists() { lean = app_dir.join("doa4retail.exe").display().to_string(); }
    if app_dir.join("dev/doa4retail.exe").exists() { dev = app_dir.join("dev/doa4retail.exe").display().to_string(); }
    if app_dir.join("game").is_dir() { data = app_dir.join("game").display().to_string(); }
    let mut d = app_dir;
    loop {
        let b = d.join("out/build");
        if lean.is_empty() && b.join("win-amd64-relwithdebinfo-lean/doa4retail.exe").exists() { lean = b.join("win-amd64-relwithdebinfo-lean/doa4retail.exe").display().to_string(); }
        if dev.is_empty() && b.join("win-amd64-relwithdebinfo/doa4retail.exe").exists() { dev = b.join("win-amd64-relwithdebinfo/doa4retail.exe").display().to_string(); }
        if d.join("DOA4_Retail_Rev1").is_dir() && data.is_empty() { data = d.join("DOA4_Retail_Rev1").display().to_string(); }
        if let Some(p) = d.parent() { d = p; } else { break; }
    }
    (lean, dev, data)
}

/// The Paths tab's Re-detect: only repairs paths that are empty or gone from
/// disk, so a working hand-picked path is never replaced or blanked.
pub fn redetect(cfg: &mut Config, (lean, dev, data): (String, String, String)) -> bool {
    let mut changed = false;
    for (cur, found) in [(&mut cfg.lean_exe, lean), (&mut cfg.dev_exe, dev), (&mut cfg.data_root, data)] {
        if (cur.is_empty() || !Path::new(cur.as_str()).exists()) && *cur != found { *cur = found; changed = true; }
    }
    if cfg.dev_exe.is_empty() { cfg.use_dev = false; }
    changed
}

/// Nothing to launch until compiler.exe has produced the exe and the dump.
pub fn game_built(cfg: &Config) -> bool {
    !cfg.lean_exe.is_empty() && Path::new(&cfg.lean_exe).exists()
        && !cfg.data_root.is_empty() && Path::new(&cfg.data_root).join("default.xex").exists()
}

/// The cvars written to the game's TOML. Debug keys only go to the developer
/// build: the play build never registers them and would warn "unknown cvar".
pub fn managed(cfg: &Config) -> BTreeMap<String, String> {
    let dev_only: Vec<&str> = debug().iter().map(|s| s.key).collect();
    all().iter()
        .filter(|s| cfg.use_dev || !dev_only.contains(&s.key))
        .map(|s| (s.key.to_string(), to_toml(s, cfg.game.get(s.key).unwrap_or(&s.default.to_string()))))
        .collect()
}

pub fn launch(app_dir: &Path, cfg: &Config) -> Result<String, String> {
    save_cfg(app_dir, cfg);
    let exe = if cfg.use_dev { &cfg.dev_exe } else { &cfg.lean_exe };
    if exe.is_empty() || !Path::new(exe).exists() {
        return Err(format!("No game executable yet{}. Run compiler.exe first.",
            if exe.is_empty() { String::new() } else { format!(" at {exe}") }));
    }
    if !Path::new(&cfg.data_root).join("default.xex").exists() {
        return Err(format!("No game data at {}. Run compiler.exe first.", cfg.data_root));
    }
    let exe_dir = Path::new(exe).parent().ok_or("bad exe path")?;
    let toml = exe_dir.join(format!("{}.toml", Path::new(exe).file_stem().unwrap().to_string_lossy()));
    let managed = managed(cfg);
    let existing = if toml.exists() { std::fs::read_to_string(&toml).unwrap_or_default() } else { "# Auto-generated cvar configuration\n".to_string() };
    std::fs::write(&toml, toml_merge(&existing, &managed)).map_err(|e| e.to_string())?;
    let log = std::env::temp_dir().join("DOA4Launcher/doa4.log");
    let mut cmd = std::process::Command::new(exe);
    cmd.current_dir(exe_dir).args(["--game_data_root", &cfg.data_root, "--gpu_plugin", "xenos", "--log_file", &log.display().to_string(), "--log_level", "info"]);
    if !cfg.use_dev { cmd.env("REX_DIAG", "0"); }
    cmd.spawn().map_err(|e| e.to_string())?;
    Ok(format!("Launched {}", if cfg.use_dev { "dev" } else { "play" }))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn merge_overwrites_managed_keys_and_keeps_the_rest() {
        let out = toml_merge(
            "# header
log_level = \"debug\"
fullscreen = false
",
            &BTreeMap::from([("fullscreen".into(), "true".into()), ("resolution_scale".into(), "2".into())]),
        );
        assert!(out.contains("log_level = \"debug\""), "{out}");
        assert!(out.contains("fullscreen = true") && !out.contains("fullscreen = false"), "{out}");
        assert!(out.contains("resolution_scale = 2"), "{out}");
        assert!(out.starts_with("# header"), "{out}");
    }

    #[test]
    fn commented_keys_are_not_treated_as_managed() {
        let out = toml_merge("#fullscreen = false
", &BTreeMap::from([("fullscreen".into(), "true".into())]));
        assert!(out.contains("#fullscreen = false"), "{out}");
        assert!(out.contains("
fullscreen = true"), "{out}");
    }

    #[test]
    fn factory_defaults_match_a_preset() {
        // Otherwise a fresh install opens on "Custom", which reads like the
        // player already changed something.
        let mut cfg = Config::default();
        for s in all() { cfg.game.insert(s.key.to_string(), s.default.to_string()); }
        assert_eq!(active_preset(&cfg), Some("Balanced"));
    }

    #[test]
    fn presets_round_trip_and_fall_back_to_custom() {
        let mut cfg = Config::default();
        for s in all() { cfg.game.insert(s.key.to_string(), s.default.to_string()); }

        for p in presets() {
            apply_preset(&mut cfg, p.name);
            assert_eq!(active_preset(&cfg), Some(p.name), "{} should report itself", p.name);
        }

        // Changing any single graphics value must drop to Custom.
        apply_preset(&mut cfg, "Balanced");
        cfg.game.insert("anisotropic_override".into(), "0".into());
        assert_eq!(active_preset(&cfg), None);

        // Display and input choices are not part of a preset, so they must not
        // affect which preset is reported.
        apply_preset(&mut cfg, "Quality");
        cfg.game.insert("fullscreen".into(), "false".into());
        cfg.game.insert("skip_intro".into(), "false".into());
        assert_eq!(active_preset(&cfg), Some("Quality"));
    }

    #[test]
    fn preset_keys_all_exist_as_settings() {
        let keys: Vec<&str> = all().iter().map(|s| s.key).collect();
        for p in presets() {
            for (k, v) in p.values {
                assert!(keys.contains(k), "preset {} sets unknown key {k}", p.name);
                let s = all().into_iter().find(|s| s.key == *k).unwrap();
                if !s.choices.is_empty() {
                    assert!(s.choices.iter().any(|(val, _)| val == v),
                        "preset {} sets {k}={v}, not one of its choices", p.name);
                }
            }
        }
    }

    #[test]
    fn aspect_for_a_3440x1440_display() {
        assert_eq!(aspect_for(3440.0, 1440.0).as_deref(), Some("2.388889"));
        assert_eq!(aspect_for(0.0, 1080.0), None);
    }

    #[test]
    fn debug_keys_only_reach_the_developer_build() {
        let mut cfg = Config::default();
        assert!(!managed(&cfg).contains_key("bind_doa4_freecam"));
        assert!(managed(&cfg).contains_key("fullscreen"));
        cfg.use_dev = true;
        assert_eq!(managed(&cfg).get("bind_doa4_freecam").map(String::as_str), Some("\"F8\""));
    }

    #[test]
    fn redetect_keeps_working_paths_and_fills_missing_ones() {
        let here = std::env::current_exe().unwrap().display().to_string();
        let mut cfg = Config { lean_exe: here.clone(), dev_exe: here.clone(), data_root: "Z:/gone".into(), use_dev: true, ..Default::default() };
        assert!(redetect(&mut cfg, (String::new(), String::new(), "found".into())));
        assert_eq!((cfg.lean_exe.as_str(), cfg.dev_exe.as_str(), cfg.data_root.as_str()), (here.as_str(), here.as_str(), "found"));
        assert!(cfg.use_dev);
        cfg.dev_exe = "Z:/gone.exe".into();
        redetect(&mut cfg, (String::new(), String::new(), String::new()));
        assert!(cfg.dev_exe.is_empty() && !cfg.use_dev);
    }

    #[test]
    fn bind_defaults_are_valid_and_free() {
        let mut cfg = Config::default();
        for s in all() { cfg.game.insert(s.key.to_string(), s.default.to_string()); }
        for s in all().into_iter().filter(|s| s.kind == Kind::Key) {
            assert!(KEY_NAMES.contains(&s.default), "{} default {} is not a key name", s.key, s.default);
            assert_eq!(bind_conflict(&cfg, s.key, s.default), None, "{} default clashes", s.key);
        }
        cfg.game.insert("bind_doa4_mark".into(), "F8".into());
        assert_eq!(bind_conflict(&cfg, "bind_doa4_mark", "F8").as_deref(), Some("Free camera key"));
        assert!(bind_conflict(&cfg, "bind_doa4_mark", "F3").is_some());
        let debug_key = debug().into_iter().find(|s| s.key == "bind_doa4_debug").unwrap();
        assert_eq!(to_toml(&debug_key, "F5"), "\"F5\"");
    }

    #[test]
    fn doubles_are_written_as_toml_floats() {
        let s = &display().into_iter().find(|s| s.key == "aspect_ratio").unwrap();
        assert_eq!(to_toml(s, "-1"), "-1.000000");
    }
}
