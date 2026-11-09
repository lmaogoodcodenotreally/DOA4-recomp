// DOA4Launcher (TUI) - usage: DOA4Launcher-tui [--launch] [--selftest]
use doa4_launcher::*;
use std::io::{self, Write};

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.iter().any(|a| a == "--selftest") {
        let merged = toml_merge("# Auto-generated cvar configuration\nlog_level = \"debug\"\nfullscreen = false\n",
            &std::collections::BTreeMap::from([("fullscreen".into(), "true".into()), ("resolution_scale".into(), "2".into())]));
        let ok = merged.contains("log_level = \"debug\"") && merged.contains("fullscreen = true") && !merged.contains("fullscreen = false");
        std::process::exit(if ok { 0 } else { 1 });
    }
    let dir = std::env::current_exe().ok().and_then(|p| p.parent().map(|p| p.to_path_buf())).unwrap_or_default();
    let mut cfg = load_cfg(&dir);
    let (lean, dev, data) = auto_detect(&dir);
    if cfg.lean_exe.is_empty() { cfg.lean_exe = lean; }
    if cfg.dev_exe.is_empty() { cfg.dev_exe = dev; }
    if cfg.data_root.is_empty() { cfg.data_root = data; }
    for s in all() { cfg.game.entry(s.key.to_string()).or_insert(s.default.to_string()); }
    println!("DOA4 Launcher (TUI)");
    if !args.iter().any(|a| a == "--launch") {
        print!("Launch now? [y/N] "); io::stdout().flush().ok();
        let mut a = String::new(); io::stdin().read_line(&mut a).ok();
        if !a.trim().eq_ignore_ascii_case("y") { println!("Type Launch/Reconfigure... (no-op demo: edit launcher.json directly, or re-run with --launch)"); save_cfg(&dir, &cfg); return; }
    }
    match launch(&dir, &cfg) { Ok(m) => println!("{m}"), Err(e) => { eprintln!("{e}"); std::process::exit(1); } }
}
