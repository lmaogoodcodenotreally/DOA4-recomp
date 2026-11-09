use std::path::{Path, PathBuf};
use std::process::Command;

pub struct Req { pub name: &'static str, pub exe: &'static str, pub winget: &'static str }

pub fn requirements() -> Vec<Req> {
    vec![
        Req { name: "7-Zip (extract ISO/XISO)", exe: "7z.exe", winget: "7zip.7zip" },
        Req { name: "CMake (build)", exe: "cmake.exe", winget: "Kitware.CMake" },
        Req { name: "Ninja (build)", exe: "ninja.exe", winget: "Ninja-build.Ninja" },
        Req { name: "LLVM/clang (build)", exe: "clang++.exe", winget: "LLVM.LLVM" },
    ]
}

pub fn find_beside_or_path(app_dir: &Path, name: &str) -> Option<PathBuf> {
    let beside = app_dir.join(name);
    if beside.exists() { return Some(beside); }
    for cand in [
        r"C:\Program Files\7-Zip", r"C:\Program Files (x86)\7-Zip",
        r"C:\Program Files\CMake\bin", r"C:\Program Files (x86)\CMake\bin",
        r"C:\Program Files\LLVM\bin", r"C:\Program Files (x86)\LLVM\bin",
        r"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja",
        r"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin",
    ] {
        let p = Path::new(cand).join(name);
        if p.exists() { return Some(p); }
    }
    // Bundled tools under VS BuildTools (CMake ships Ninja inside)
    for base in [r"C:\Program Files\Microsoft Visual Studio", r"C:\Program Files (x86)\Microsoft Visual Studio"] {
        if Path::new(base).exists() {
            for entry in walkdir_shallow(Path::new(base), 6) {
                if entry.file_name().map(|n| n == name).unwrap_or(false) { return Some(entry); }
            }
        }
    }
    for dir in std::env::var("PATH").unwrap_or_default().split(';') {
        let p = Path::new(dir.trim_matches('"')).join(name);
        if p.exists() { return Some(p); }
    }
    None
}

fn walkdir_shallow(d: &Path, depth: usize) -> Vec<PathBuf> {
    let mut out = Vec::new();
    if depth == 0 || !d.is_dir() { return out; }
    for e in std::fs::read_dir(d).into_iter().flatten().flatten() {
        let p = e.path();
        if p.is_dir() { out.extend(walkdir_shallow(&p, depth - 1)); } else { out.push(p); }
    }
    out
}

pub fn version_works(path: &Path, flag: &str) -> bool {
    Command::new(path).arg(flag).output().map(|o| o.status.success()).unwrap_or(false)
}

pub fn check_requirements(app_dir: &Path) -> Vec<Req> {
    requirements().into_iter().filter(|r| find_beside_or_path(app_dir, r.exe).is_none()).collect()
}

pub fn install_requirement(r: &Req) -> std::io::Result<std::process::ExitStatus> {
    Command::new("winget").args(["install", "--id", r.winget, "-e", "--accept-source-agreements", "--accept-package-agreements"]).status()
}
pub fn refresh_path() {
    let mut dirs = vec![r"C:\Program Files\7-Zip".to_string(), r"C:\Program Files\CMake\bin".to_string(), r"C:\Program Files\LLVM\bin".to_string()];
    dirs.push(r"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja".to_string());
    let cur = std::env::var("PATH").unwrap_or_default();
    std::env::set_var("PATH", format!("{};{}", dirs.join(";"), cur));
}

pub fn extract(iso: &Path, app_dir: &Path) -> Result<PathBuf, String> {
    let game = app_dir.join("game");
    std::fs::create_dir_all(&game).map_err(|e| e.to_string())?;
    if iso.extension().map(|e| e.eq_ignore_ascii_case("xex")).unwrap_or(false) {
        let meta = std::fs::metadata(iso).map_err(|e| e.to_string())?;
        if meta.len() != 16465920 { return Err(format!("Unexpected XEX size {} (expected US Rev 1 default.xex 16465920)", meta.len())); }
        std::fs::copy(iso, game.join("default.xex")).map_err(|e| e.to_string())?;
    } else {
        let z = find_beside_or_path(app_dir, "7z.exe").or_else(|| find_beside_or_path(app_dir, "extract-xiso.exe"))
            .ok_or("7z.exe or extract-xiso.exe not found")?;
        let st = Command::new(z).arg("x").arg(iso).arg(format!("-o{}", game.display())).arg("-y").output().map_err(|e| e.to_string())?;
        if !st.status.success() { return Err(format!("extractor failed: {}", String::from_utf8_lossy(&st.stderr))); }
        // flatten one wrapper level
        fn find_xex(d: &Path) -> Option<PathBuf> {
            for e in std::fs::read_dir(d).ok()?.flatten() {
                let p = e.path();
                if p.is_file() && p.file_name().map(|n| n=="default.xex").unwrap_or(false) { return Some(p); }
                if p.is_dir() { if let Some(f) = find_xex(&p) { return Some(f); } }
            }
            None
        }
        let nested = find_xex(&game).ok_or("default.xex not found after extract")?;
        if nested.parent() != Some(game.as_path()) {
            let root = nested.parent().unwrap().to_path_buf();
            for f in std::fs::read_dir(&root).map_err(|e| e.to_string())?.flatten() {
                let p = f.path();
                if p.is_file() { std::fs::rename(&p, game.join(p.file_name().unwrap())).map_err(|e| e.to_string())?; }
            }
        }
    }
    Ok(game.join("default.xex"))
}

pub fn recompile(app_dir: &Path) -> Result<PathBuf, String> {
    let game = app_dir.join("game");
    let xex = game.join("default.xex");
    if !xex.exists() { return Err("Run extract first".into()); }
    let assets = app_dir.join("assets").join("game");
    std::fs::create_dir_all(&assets).map_err(|e| e.to_string())?;
    std::fs::copy(&xex, assets.join("doa4.xex")).map_err(|e| e.to_string())?;
    let gen = app_dir.join("generated").join("doa4");
    if !gen.join("sources.cmake").exists() {
        let rex = [
            app_dir.join("out/build/rexglue-sdk/rexglue.exe"),
            app_dir.join("thirdparty/rexglue-sdk/build/rexglue.exe"),
        ].into_iter().find(|p| p.exists())
            .or_else(|| find_beside_or_path(app_dir, "rexglue.exe"))
            .ok_or("rexglue.exe not found. Build the SDK first")?;
        let st = Command::new(rex).current_dir(app_dir).args(["codegen", "--config"]).arg(app_dir.join("doa4retail_manifest.toml")).output().map_err(|e| e.to_string())?;
        if !gen.exists() { return Err(format!("codegen failed: {}", String::from_utf8_lossy(&st.stderr))); }
    }
    let cmake = find_beside_or_path(app_dir, "cmake.exe").ok_or("cmake not found on PATH")?;
    let cfg = Command::new(&cmake).current_dir(app_dir).args(["--preset", "win-amd64-relwithdebinfo-lean"]).output().map_err(|e| e.to_string())?;
    let _ = cfg;
    let st = Command::new(&cmake).current_dir(app_dir).args(["--build", "--preset", "win-amd64-relwithdebinfo-lean"]).output().map_err(|e| e.to_string())?;
    let _ = st;
    let out = app_dir.join("out/build/win-amd64-relwithdebinfo-lean/doa4retail.exe");
    if out.exists() { std::fs::copy(&out, app_dir.join("doa4retail.exe")).map_err(|e| e.to_string())?; }
    Ok(app_dir.join("doa4retail.exe"))
}
