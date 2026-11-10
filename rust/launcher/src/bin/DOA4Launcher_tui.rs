// DOA4Launcher (console).
//   DOA4Launcher-tui                 interactive: list settings, change, launch
//   DOA4Launcher-tui --launch        launch with the saved settings
//   DOA4Launcher-tui --set k=v ...   change settings and save (no launch)
//   DOA4Launcher-tui --list          print the settings and exit
use doa4_launcher::*;
use std::io::{self, Write};
use std::path::Path;

fn show(cfg: &Config) {
    for (group, items) in [("Display", display()), ("Graphics", graphics()), ("Input", input())] {
        println!("\n{group}");
        for s in items {
            let v = cfg.game.get(s.key).cloned().unwrap_or_else(|| s.default.to_string());
            let pretty = s.choices.iter().find(|(val, _)| *val == v).map(|(_, t)| *t).unwrap_or(&v);
            println!("  {:<48} {:<10} {}", s.key, v, pretty);
        }
    }
}

/// Returns false if the key is not one we manage.
fn set(cfg: &mut Config, kv: &str) -> bool {
    let Some((k, v)) = kv.split_once('=') else { return false };
    let (k, v) = (k.trim(), v.trim());
    let Some(s) = all().into_iter().find(|s| s.key == k) else { return false };
    if !s.choices.is_empty() && !s.choices.iter().any(|(val, _)| *val == v) {
        eprintln!("{k}: expected one of {}", s.choices.iter().map(|(val, _)| *val).collect::<Vec<_>>().join(", "));
        return false;
    }
    if s.kind == Kind::Bool && s.choices.is_empty() && v != "true" && v != "false" {
        eprintln!("{k}: expected true or false");
        return false;
    }
    cfg.game.insert(k.to_string(), v.to_string());
    true
}

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let has = |f: &str| args.iter().any(|a| a == f);
    let dir = std::env::current_exe().ok().and_then(|p| p.parent().map(Path::to_path_buf)).unwrap_or_default();

    let mut cfg = load_cfg(&dir);
    let (lean, dev, data) = auto_detect(&dir);
    if cfg.lean_exe.is_empty() { cfg.lean_exe = lean; }
    if cfg.dev_exe.is_empty() { cfg.dev_exe = dev; }
    if cfg.data_root.is_empty() { cfg.data_root = data; }
    for s in all() { cfg.game.entry(s.key.to_string()).or_insert(s.default.to_string()); }

    if has("--list") { show(&cfg); return; }

    // --set k=v [k=v ...]
    if let Some(i) = args.iter().position(|a| a == "--set") {
        let mut n = 0;
        for kv in &args[i + 1..] {
            if kv.starts_with("--") { break; }
            if set(&mut cfg, kv) { n += 1; } else { eprintln!("ignored: {kv}"); }
        }
        save_cfg(&dir, &cfg);
        println!("{n} setting(s) saved to {}", dir.join("launcher.json").display());
        if !has("--launch") { return; }
    }

    if !game_built(&cfg) {
        eprintln!("No build here yet. Run compiler.exe (or compiler-tui.exe) with your own\n\
                   Dead or Alive 4 (USA / Europe, Rev 1) ISO first.");
        eprintln!("  game exe : {}", if cfg.lean_exe.is_empty() { "not found" } else { &cfg.lean_exe });
        eprintln!("  game data: {}", if cfg.data_root.is_empty() { "not found" } else { &cfg.data_root });
        std::process::exit(1);
    }

    if !has("--launch") {
        println!("DOA4 launcher. Settings live in {}", dir.join("launcher.json").display());
        show(&cfg);
        println!("\nType  key=value  to change one, 'l' to launch, Enter to quit.");
        loop {
            print!("> ");
            io::stdout().flush().ok();
            let mut line = String::new();
            // EOF (piped/closed stdin) means "no input", not "launch".
            if io::stdin().read_line(&mut line).unwrap_or(0) == 0 {
                save_cfg(&dir, &cfg);
                return;
            }
            let line = line.trim();
            match line {
                "" => { save_cfg(&dir, &cfg); return; }
                "l" | "launch" => break,
                "list" => show(&cfg),
                _ => {
                    if set(&mut cfg, line) { save_cfg(&dir, &cfg); println!("ok"); }
                    else { println!("not a setting: {line}   (type 'list')"); }
                }
            }
        }
    }

    match launch(&dir, &cfg) {
        Ok(m) => println!("{m}"),
        Err(e) => { eprintln!("{e}"); std::process::exit(1); }
    }
}
