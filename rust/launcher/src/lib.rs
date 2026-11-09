use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

#[derive(Debug, Clone)]
pub struct Setting { pub key: &'static str, pub kind: Kind, pub default: &'static str, pub label: &'static str, pub choices: Vec<(&'static str, &'static str)> }
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Kind { Bool, Int, Double, Str }

pub fn display() -> Vec<Setting> {
    vec![
        Setting{key:"fullscreen",kind:Kind::Bool,default:"true",label:"Fullscreen",choices:vec![]},
        Setting{key:"monitor",kind:Kind::Int,default:"0",label:"Monitor",choices:vec![("0","Default"),("1","Monitor 1"),("2","Monitor 2"),("3","Monitor 3")]},
        Setting{key:"window_width",kind:Kind::Int,default:"0",label:"Window width",choices:vec![]},
        Setting{key:"window_height",kind:Kind::Int,default:"0",label:"Window height",choices:vec![]},
        Setting{key:"aspect_ratio",kind:Kind::Double,default:"-1",label:"Aspect ratio",choices:vec![("-1","Auto"),("0","16:9"),("1.333333","4:3"),("1.6","16:10"),("2.370370","21:9 2560x1080"),("2.388889","21:9 3440x1440"),("2.4","21:9 3840x1600"),("3.555556","32:9")]},
        Setting{key:"present_letterbox",kind:Kind::Bool,default:"true",label:"Keep 16:9 (letterbox)",choices:vec![]},
        Setting{key:"frame_interp",kind:Kind::Bool,default:"false",label:"Frame interpolation (experimental)",choices:vec![]},
        Setting{key:"d3d12_allow_variable_refresh_rate_and_tearing",kind:Kind::Bool,default:"false",label:"Screen sync",choices:vec![("false","V-Sync"),("true","Off (tearing)")]},
    ]
}
pub fn graphics() -> Vec<Setting> {
    vec![
        Setting{key:"resolution_scale",kind:Kind::Int,default:"1",label:"Internal resolution",choices:vec![("1","1x"),("2","2x"),("3","3x"),("4","4x")]},
        Setting{key:"scaled_point_filter_linear",kind:Kind::Bool,default:"true",label:"Smooth 2D art",choices:vec![]},
        Setting{key:"swap_post_effect",kind:Kind::Str,default:"none",label:"Anti-aliasing (post)",choices:vec![("none","Off"),("fxaa","FXAA"),("fxaa_extreme","FXAA extreme")]},
        Setting{key:"native_2x_msaa",kind:Kind::Bool,default:"true",label:"Native 2x MSAA",choices:vec![]},
        Setting{key:"anisotropic_override",kind:Kind::Int,default:"3",label:"Texture filtering",choices:vec![("-1","Game default"),("0","Off"),("1","1x"),("2","2x"),("3","4x"),("4","8x"),("5","16x")]},
        Setting{key:"present_effect",kind:Kind::Str,default:"cas",label:"Output scaling",choices:vec![("bilinear","Bilinear"),("cas","CAS"),("fsr","FSR 1"),("fsr2","FSR 2"),("fsr3","FSR 3")]},
    ]
}
pub fn input() -> Vec<Setting> {
    vec![
        Setting{key:"input_backend",kind:Kind::Str,default:"sdl",label:"Controller backend",choices:vec![("sdl","SDL"),("xinput","XInput")]},
        Setting{key:"mnk_mode",kind:Kind::Bool,default:"true",label:"Keyboard as controller",choices:vec![]},
        Setting{key:"hid_sdl_directinput",kind:Kind::Bool,default:"false",label:"Legacy DirectInput pads",choices:vec![]},
        Setting{key:"audio_mute",kind:Kind::Bool,default:"false",label:"Mute audio",choices:vec![]},
        Setting{key:"skip_intro",kind:Kind::Bool,default:"true",label:"Skip intro",choices:vec![]},
    ]
}
pub fn all() -> Vec<Setting> { [display(), graphics(), input()].concat() }

pub fn to_toml(s: &Setting, value: &str) -> String {
    match s.kind { Kind::Str => format!("\"{}\"", value), Kind::Double => format!("{:.6}", value.parse::<f64>().unwrap_or(0.0)), _ => value.to_string() }
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

pub fn toml_read(path: &Path) -> BTreeMap<String, String> {
    let mut m = BTreeMap::new();
    if !path.exists() { return m; }
    for line in std::fs::read_to_string(path).unwrap_or_default().lines() {
        let t = line.trim();
        if t.starts_with('#') { continue; }
        if let Some(eq) = t.find('=') { m.insert(t[..eq].trim().to_string(), t[eq+1..].trim().trim_matches('"').to_string()); }
    }
    m
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

pub fn launch(app_dir: &Path, cfg: &Config) -> Result<String, String> {
    save_cfg(app_dir, cfg);
    let exe = if cfg.use_dev { &cfg.dev_exe } else { &cfg.lean_exe };
    if !Path::new(exe).exists() { return Err(format!("Game exe not found: {}", exe)); }
    if !Path::new(&cfg.data_root).exists() { return Err(format!("Game data folder not found: {}", cfg.data_root)); }
    let exe_dir = Path::new(exe).parent().ok_or("bad exe path")?;
    let toml = exe_dir.join(format!("{}.toml", Path::new(exe).file_stem().unwrap().to_string_lossy()));
    let managed: BTreeMap<String, String> = all().iter().map(|s| (s.key.to_string(), to_toml(s, cfg.game.get(s.key).unwrap_or(&s.default.to_string())))).collect();
    let existing = if toml.exists() { std::fs::read_to_string(&toml).unwrap_or_default() } else { "# Auto-generated cvar configuration\n".to_string() };
    std::fs::write(&toml, toml_merge(&existing, &managed)).map_err(|e| e.to_string())?;
    let log = std::env::temp_dir().join("DOA4Launcher/doa4.log");
    let mut cmd = std::process::Command::new(exe);
    cmd.current_dir(exe_dir).args(["--game_data_root", &cfg.data_root, "--gpu_plugin", "xenos", "--log_file", &log.display().to_string(), "--log_level", "info"]);
    if !cfg.use_dev { cmd.env("REX_DIAG", "0"); }
    cmd.spawn().map_err(|e| e.to_string())?;
    Ok(format!("Launched {}", if cfg.use_dev { "dev" } else { "play" }))
}
