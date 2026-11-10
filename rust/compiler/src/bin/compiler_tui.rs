// DOA4 compiler (console). Usage: compiler-tui [--iso <path>] [--yes] [--check]
use doa4_compiler as dc;
use std::io::{self, Write};
use std::path::{Path, PathBuf};

fn app_dir() -> PathBuf {
    std::env::current_exe().ok().and_then(|p| p.parent().map(Path::to_path_buf)).unwrap_or_default()
}

fn ask(q: &str) -> bool {
    print!("{q} [y/N] ");
    io::stdout().flush().ok();
    let mut s = String::new();
    io::stdin().read_line(&mut s).ok();
    s.trim().eq_ignore_ascii_case("y")
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let has = |f: &str| args.iter().any(|a| a == f);
    let dir = app_dir();

    println!("DOA4 compiler - needs your own USA / Europe Rev 1 disc dump.");
    let missing = dc::check_requirements(&dir);
    if !missing.is_empty() {
        println!("\nMissing build tools:");
        for r in &missing {
            println!("  - {}", r.name);
            if !r.hint.is_empty() { println!("    {}", r.hint); }
        }
        let installable: Vec<&dc::Req> = missing.iter().filter(|r| !r.winget.is_empty()).collect();
        if !installable.is_empty() && !has("--check") && (has("--yes") || ask("\nInstall the ones winget can handle?")) {
            for r in &installable {
                println!("Installing {}...", r.name);
                if let Err(e) = dc::install_requirement(r) { eprintln!("  {e}"); }
            }
            dc::refresh_path(&dir);
        }
        // Missing winget-installable tools are located by exact path, so they are
        // a hard stop. MSVC is detected by probing the Visual Studio layout, which
        // can miss a non-default install, so that one only warns.
        let still = dc::check_requirements(&dir);
        let blocking: Vec<&str> = still.iter().filter(|r| !r.winget.is_empty()).map(|r| r.name).collect();
        if !blocking.is_empty() {
            eprintln!("\nStill missing: {}. Install them and run this again.", blocking.join(", "));
            std::process::exit(2);
        }
        if !still.is_empty() {
            eprintln!("\nContinuing anyway. If the build cannot find the MSVC headers, install the above.");
        }
    } else {
        println!("All build tools found.");
    }
    if has("--check") { return; }

    let iso = args.iter().position(|a| a == "--iso").and_then(|i| args.get(i + 1).cloned());
    let iso = match iso {
        Some(s) => s,
        None if dc::already_extracted(&dir) => {
            println!("game\\ is already extracted here; reusing it.");
            String::new()
        }
        None => {
            println!("\nDrag the ISO into this window (or paste its path) and press Enter.");
            print!("ISO: ");
            io::stdout().flush().ok();
            let mut s = String::new();
            io::stdin().read_line(&mut s).ok();
            s.trim().trim_matches('"').trim_matches('\'').to_string()
        }
    };

    // Rewrite one status line in place; full detail goes to compiler.log.
    let mut last = String::new();
    let mut on = |m: &str, f: f32| {
        if m == last { return; }
        last = m.to_string();
        let m = if m.len() > 100 { &m[..100] } else { m };
        print!("\r{:>3}%  {:<100}", (f * 100.0) as u32, m);
        io::stdout().flush().ok();
    };
    match dc::build_all(Path::new(&iso), &dir, &mut on) {
        Ok(p) => println!("\n\nBuilt {}\nNow run DOA4Launcher.exe (or DOA4Launcher-tui.exe).", p.display()),
        Err(e) => {
            eprintln!("\n\n{e}");
            std::process::exit(1);
        }
    }
}
