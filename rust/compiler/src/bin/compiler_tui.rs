// DOA4 Compiler (TUI) - extract + recompile. Usage: compiler-tui [--iso <path>] [--yes]
use std::io::{self, Write};
use std::path::PathBuf;

fn app_dir() -> PathBuf { std::env::current_exe().ok().and_then(|p| p.parent().map(|p| p.to_path_buf())).unwrap_or_default() }

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let dir = app_dir();
    // requirements check with yes/no
    let missing = doa4_compiler::check_requirements(&dir);
    if !missing.is_empty() {
        println!("Missing requirements:");
        for r in &missing { println!("- {}", r.name); }
        print!("Install them with winget? [y/N] "); io::stdout().flush().ok();
        let mut ans = String::new(); io::stdin().read_line(&mut ans).ok();
        if ans.trim().eq_ignore_ascii_case("y") {
            for r in missing { println!("Installing {}...", r.name); let _ = doa4_compiler::install_requirement(&r); }
            println!("Install step done. Restart compiler-tui if tools were just installed.");
        }
    }
    let iso = if let Some(i) = args.iter().position(|a| a == "--iso") { args.get(i+1).cloned() } else { None };
    let iso = match iso {
        Some(s) => s,
        None => { print!("ISO/XISO path: "); io::stdout().flush().ok(); let mut s = String::new(); io::stdin().read_line(&mut s).ok(); s.trim().to_string() }
    };
    match doa4_compiler::extract(std::path::Path::new(&iso), &dir) {
        Ok(_) => { println!("Extracted. Recompiling..."); match doa4_compiler::recompile(&dir) { Ok(_) => println!("Done. Run DOA4Launcher.exe."), Err(e) => { eprintln!("Recompile failed: {e}"); std::process::exit(1); } } }
        Err(e) => { eprintln!("Extract failed: {e}"); std::process::exit(1); }
    }
}
