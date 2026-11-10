//! DOA4 compiler: extract the user's ISO/XISO, then run SDK codegen + cmake.
//!
//! Every child process is streamed through `run_logged` into `compiler.log`
//! beside the exe, so a failed build is diagnosable (ninja writes compile
//! errors to stdout, which the old `.output()` capture discarded).

use std::io::{BufRead, BufReader, Write};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};

/// `(message, 0.0..=1.0)`. Progress is real: ninja's `[n/m]` for the build,
/// extracted bytes vs ISO size for the extract.
pub type Progress<'a> = &'a mut dyn FnMut(&str, f32);

pub const PRESET: &str = "win-amd64-relwithdebinfo-lean";
/// USA Rev 1 `default.xex`. The manifest's function addresses only match this.
pub const XEX_SIZE: u64 = 16_465_920;

pub struct Req {
    pub name: &'static str,
    pub exe: &'static str,
    /// Empty = we cannot install it for the user; show `hint` instead.
    pub winget: &'static str,
    pub hint: &'static str,
}

pub fn requirements() -> Vec<Req> {
    vec![
        Req { name: "7-Zip", exe: "7z.exe", winget: "7zip.7zip", hint: "" },
        Req { name: "CMake", exe: "cmake.exe", winget: "Kitware.CMake", hint: "" },
        Req { name: "Ninja", exe: "ninja.exe", winget: "Ninja-build.Ninja", hint: "" },
        Req { name: "LLVM/clang", exe: "clang++.exe", winget: "LLVM.LLVM", hint: "" },
    ]
}

pub fn log_path(app_dir: &Path) -> PathBuf { app_dir.join("compiler.log") }

/// CMake wraps every POST_BUILD step in `cmd /C "cd /D <dir> && ..."`, and `^`
/// is cmd's escape character, so a `^` anywhere in the path makes the link step
/// fail after ~20 minutes of compiling. Measured: spaces, apostrophes, accents,
/// parentheses, `%`, `!` and `&` all build fine - `^` is the only one that stops
/// the build outright. (Non-ASCII characters additionally break FidelityFX; see
/// `ffx_hostile_char`, which degrades instead of refusing.)
pub fn unusable_path_reason(app_dir: &Path) -> Option<String> {
    if !app_dir.display().to_string().contains('^') { return None; }
    Some([
        "This folder's path contains a '^' character:".to_string(),
        format!("  {}", app_dir.display()),
        String::new(),
        "The Windows build tools cannot handle it (cmd.exe treats '^' as an escape".to_string(),
        "character), and the build would fail at the final link step. Move or rename".to_string(),
        "the folder so no '^' appears in its path, then run this again.".to_string(),
    ].join("
"))
}

/// clang on Windows compiles against the MSVC headers/libs, so the Build Tools
/// are required even though clang itself comes from LLVM. Too large (and too
/// workload-specific) to install unattended, so this is report-only.
pub fn msvc_present() -> bool {
    find_in_vs(r"VC\Auxiliary\Build", "vcvarsall.bat").is_some()
}

/// FidelityFX's shader compiler (FidelityFX_SC.exe) mis-handles non-ASCII paths
/// and then fails with "missing entry point definition". Measured against the
/// real FFX build: spaces, apostrophes and parentheses all compile fine; an
/// accented character does not. Rather than refuse the whole build, the compiler
/// leaves FidelityFX out for such a path - the game still runs, only the CAS /
/// FSR output-scaling options become unavailable.
pub fn ffx_hostile_char(app_dir: &Path) -> Option<char> {
    app_dir.display().to_string().chars().find(|c| !c.is_ascii())
}

/// True when CMake will have to fetch the FidelityFX SDK with git, rather than
/// use the copy vendored in the release zip.
fn needs_ffx_fetch(app_dir: &Path) -> bool {
    ffx_hostile_char(app_dir).is_none()
        && !app_dir.join("thirdparty/fidelityfx/ffx-api/CMakeLists.txt").exists()
}

pub fn check_requirements(app_dir: &Path) -> Vec<Req> {
    let mut missing: Vec<Req> = requirements()
        .into_iter()
        .filter(|r| find_beside_or_path(app_dir, r.exe).is_none())
        .collect();
    // Only a hard requirement when FidelityFX is not vendored beside us.
    if needs_ffx_fetch(app_dir) && find_beside_or_path(app_dir, "git.exe").is_none() {
        missing.push(Req {
            name: "Git (needed to fetch the FidelityFX SDK)",
            exe: "git.exe",
            winget: "Git.Git",
            hint: "",
        });
    }
    if !msvc_present() {
        missing.push(Req {
            name: "Visual Studio Build Tools (MSVC headers/libs, used by clang)",
            exe: "",
            winget: "",
            hint: "Install \"Desktop development with C++\" from \
                   https://visualstudio.microsoft.com/downloads/ (Build Tools are enough).",
        });
    }
    missing
}

pub fn install_requirement(r: &Req) -> Result<bool, String> {
    if r.winget.is_empty() { return Err(r.hint.to_string()); }
    Command::new("winget")
        .args(["install", "--id", r.winget, "-e", "--accept-source-agreements", "--accept-package-agreements"])
        .status()
        .map(|s| s.success())
        .map_err(|e| format!("winget failed ({e}). Install {} manually.", r.name))
}

pub fn find_beside_or_path(app_dir: &Path, name: &str) -> Option<PathBuf> {
    let beside = app_dir.join(name);
    if beside.exists() { return Some(beside); }
    for cand in [
        r"C:\Program Files\7-Zip", r"C:\Program Files (x86)\7-Zip",
        r"C:\Program Files\CMake\bin", r"C:\Program Files (x86)\CMake\bin",
        r"C:\Program Files\LLVM\bin", r"C:\Program Files (x86)\LLVM\bin",
        r"C:\Program Files\Git\cmd", r"C:\Program Files (x86)\Git\cmd",
    ] {
        let p = Path::new(cand).join(name);
        if p.exists() { return Some(p); }
    }
    for dir in std::env::var("PATH").unwrap_or_default().split(';') {
        let d = dir.trim().trim_matches('"');
        if d.is_empty() { continue; }
        let p = Path::new(d).join(name);
        if p.exists() { return Some(p); }
    }
    // CMake and Ninja ship inside Visual Studio rather than on PATH. Walk the
    // known layout (<root>/<year>/<edition>/<tail>) so any VS version matches,
    // instead of recursing blindly through all of Program Files.
    for tail in [
        r"Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja",
        r"Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin",
        r"VC\Tools\Llvm\x64\bin",
    ] {
        if let Some(p) = find_in_vs(tail, name) { return Some(p); }
    }
    None
}

/// Looks for `<vs root>/<year>/<edition>/<tail>/<name>`.
fn find_in_vs(tail: &str, name: &str) -> Option<PathBuf> {
    for base in [
        r"C:\Program Files\Microsoft Visual Studio",
        r"C:\Program Files (x86)\Microsoft Visual Studio",
    ] {
        for year in std::fs::read_dir(base).into_iter().flatten().flatten() {
            for edition in std::fs::read_dir(year.path()).into_iter().flatten().flatten() {
                let p = edition.path().join(tail).join(name);
                if p.exists() { return Some(p); }
            }
        }
    }
    None
}

fn walk_find(d: &Path, name: &str, depth: usize) -> Option<PathBuf> {
    if depth == 0 || !d.is_dir() { return None; }
    let mut dirs = Vec::new();
    for e in std::fs::read_dir(d).into_iter().flatten().flatten() {
        let p = e.path();
        if p.is_dir() { dirs.push(p); } else if p.file_name().map(|n| n == name).unwrap_or(false) { return Some(p); }
    }
    dirs.into_iter().find_map(|p| walk_find(&p, name, depth - 1))
}

/// Put the dirs of the tools we found on PATH, so cmake's own probing (and
/// ninja's child processes) see them without a shell restart.
pub fn refresh_path(app_dir: &Path) {
    let mut dirs: Vec<String> = Vec::new();
    for r in requirements() {
        if let Some(p) = find_beside_or_path(app_dir, r.exe) {
            if let Some(d) = p.parent() { dirs.push(d.display().to_string()); }
        }
    }
    let cur = std::env::var("PATH").unwrap_or_default();
    dirs.retain(|d| !cur.split(';').any(|c| c.eq_ignore_ascii_case(d)));
    if !dirs.is_empty() { std::env::set_var("PATH", format!("{};{}", dirs.join(";"), cur)); }
}

/// Record a decision the compiler made in compiler.log, next to the child
/// process output, so a later diagnosis sees why the build looks the way it does.
pub fn log_note(app_dir: &Path, msg: &str) {
    if let Ok(mut f) = std::fs::OpenOptions::new().create(true).append(true).open(log_path(app_dir)) {
        let _ = writeln!(f, "
=== {msg}");
    }
}

fn pump<R: std::io::Read + Send + 'static>(r: Option<R>, tx: std::sync::mpsc::Sender<String>) {
    if let Some(r) = r {
        std::thread::spawn(move || {
            for l in BufReader::new(r).lines().map_while(Result::ok) {
                if tx.send(l).is_err() { break; }
            }
        });
    }
}

/// Run `cmd`, appending every stdout/stderr line to `compiler.log` and handing
/// it to `on_line`. Returns the tail of the output on failure.
pub fn run_logged(
    app_dir: &Path,
    cmd: &mut Command,
    mut on_line: impl FnMut(&str),
) -> Result<(), String> {
    let mut log = std::fs::OpenOptions::new()
        .create(true).append(true).open(log_path(app_dir))
        .map_err(|e| format!("cannot open compiler.log: {e}"))?;
    let _ = writeln!(log, "\n=== {cmd:?}");
    let mut child = cmd
        .stdout(Stdio::piped()).stderr(Stdio::piped()).stdin(Stdio::null())
        .spawn()
        .map_err(|e| format!("cannot start {:?}: {e}", cmd.get_program()))?;
    let (tx, rx) = std::sync::mpsc::channel();
    pump(child.stdout.take(), tx.clone());
    pump(child.stderr.take(), tx);
    // Keep a bounded tail so a failure message is useful without the whole log.
    let mut tail: std::collections::VecDeque<String> = std::collections::VecDeque::new();
    for line in rx {
        let _ = writeln!(log, "{line}");
        on_line(&line);
        if tail.len() == 40 { tail.pop_front(); }
        tail.push_back(line);
    }
    let st = child.wait().map_err(|e| e.to_string())?;
    if st.success() { return Ok(()); }
    // Prefer the real diagnostics over 40 lines of trailing warnings.
    let errs: Vec<&String> = tail.iter()
        .filter(|l| l.contains("error") || l.contains("Error") || l.starts_with("FAILED") || l.starts_with("Failed"))
        .collect();
    let shown: Vec<String> = if errs.is_empty() {
        tail.iter().rev().take(12).rev().cloned().collect()
    } else {
        errs.iter().rev().take(12).rev().map(|s| s.to_string()).collect()
    };
    Err(format!("{}\n(full log: {})", shown.join("\n"), log_path(app_dir).display()))
}

fn dir_bytes(d: &Path) -> u64 {
    let mut n = 0;
    for e in std::fs::read_dir(d).into_iter().flatten().flatten() {
        let p = e.path();
        n += if p.is_dir() { dir_bytes(&p) } else { e.metadata().map(|m| m.len()).unwrap_or(0) };
    }
    n
}

/// True when `game\default.xex` is the USA Rev 1 executable, which is all
/// codegen needs.
pub fn have_xex(app_dir: &Path) -> bool {
    std::fs::metadata(app_dir.join("game").join("default.xex"))
        .map(|m| m.len() == XEX_SIZE)
        .unwrap_or(false)
}

/// True when `game\` holds a full dump (executable plus the .afs archives), so
/// a re-run can skip the multi-GB extract instead of deleting and redoing it.
pub fn already_extracted(app_dir: &Path) -> bool {
    have_xex(app_dir)
        && std::fs::read_dir(app_dir.join("game"))
            .map(|d| d.flatten().any(|e| e.path().extension().map(|x| x.eq_ignore_ascii_case("afs")).unwrap_or(false)))
            .unwrap_or(false)
}

pub fn extract(iso: &Path, app_dir: &Path, p: Progress) -> Result<PathBuf, String> {
    let game = app_dir.join("game");
    if already_extracted(app_dir) {
        p("Using the game\\ folder already extracted here.", 0.35);
        return Ok(game.join("default.xex"));
    }
    if !iso.exists() { return Err(format!("No such file: {}", iso.display())); }
    if game.exists() { std::fs::remove_dir_all(&game).map_err(|e| format!("cannot clear game\\: {e}"))?; }
    std::fs::create_dir_all(&game).map_err(|e| e.to_string())?;

    if iso.extension().map(|e| e.eq_ignore_ascii_case("xex")).unwrap_or(false) {
        let len = std::fs::metadata(iso).map_err(|e| e.to_string())?.len();
        if len != XEX_SIZE {
            return Err(format!("This default.xex is {len} bytes; USA Rev 1 is {XEX_SIZE}. Other regions/revisions are not supported."));
        }
        std::fs::copy(iso, game.join("default.xex")).map_err(|e| e.to_string())?;
        p("Copied default.xex (no .afs data - movies/audio will be missing).", 0.35);
        return Ok(game.join("default.xex"));
    }

    // Xbox XISO: extract-xiso reads the Xbox filesystem; 7-Zip mis-reads it.
    let tool = find_beside_or_path(app_dir, "extract-xiso.exe")
        .ok_or("extract-xiso.exe is missing - it ships next to compiler.exe; re-extract the release zip")?;
    let total = std::fs::metadata(iso).map(|m| m.len()).unwrap_or(0);
    let watch = game.clone();
    let stop = std::sync::Arc::new(std::sync::atomic::AtomicBool::new(false));
    let (ptx, prx) = std::sync::mpsc::channel();
    {
        let stop = stop.clone();
        std::thread::spawn(move || {
            while !stop.load(std::sync::atomic::Ordering::Relaxed) {
                std::thread::sleep(std::time::Duration::from_millis(700));
                let _ = ptx.send(dir_bytes(&watch));
            }
        });
    }
    let mut cmd = Command::new(tool);
    cmd.current_dir(app_dir).args(["-x", "-d", "game"]).arg(iso);
    let mut frac = 0.0f32;
    let r = run_logged(app_dir, &mut cmd, |_line| {
        while let Ok(b) = prx.try_recv() {
            if total > 0 { frac = (b as f32 / total as f32).min(1.0); }
        }
        p(&format!("Extracting the ISO... {:.0}%", frac * 100.0), 0.35 * frac);
    });
    stop.store(true, std::sync::atomic::Ordering::Relaxed);
    r.map_err(|e| format!("extract-xiso failed:\n{e}"))?;

    // extract-xiso may nest everything under a folder named after the image.
    let xex = walk_find(&game, "default.xex", 4).ok_or(
        "default.xex was not found in the extracted image - is this a Dead or Alive 4 XISO?",
    )?;
    if xex.parent() != Some(game.as_path()) {
        let root = xex.parent().unwrap().to_path_buf();
        for e in std::fs::read_dir(&root).map_err(|e| e.to_string())?.flatten() {
            let to = game.join(e.file_name());
            if to != e.path() { std::fs::rename(e.path(), to).map_err(|e| e.to_string())?; }
        }
        let _ = std::fs::remove_dir_all(&root);
    }
    let len = std::fs::metadata(game.join("default.xex")).map(|m| m.len()).unwrap_or(0);
    if len != XEX_SIZE {
        return Err(format!("This image's default.xex is {len} bytes; USA Rev 1 is {XEX_SIZE}. Other regions/revisions are not supported."));
    }
    p("Extracted.", 0.35);
    Ok(game.join("default.xex"))
}

fn find_rexglue(app_dir: &Path) -> Option<PathBuf> {
    [
        app_dir.join("rexgluerd.exe"),
        app_dir.join("rexglue.exe"),
        app_dir.join("out/build").join(PRESET).join("sdk-bin/rexgluerd.exe"),
        app_dir.join("thirdparty/rexglue-sdk/build/rexglue.exe"),
    ]
    .into_iter()
    .find(|p| p.exists())
}

pub fn recompile(app_dir: &Path, p: Progress) -> Result<PathBuf, String> {
    if !have_xex(app_dir) { return Err("Extract an ISO first.".into()); }
    let xex = app_dir.join("game").join("default.xex");

    // Codegen reads the xex from the path the manifest names.
    let assets = app_dir.join("assets").join("game");
    std::fs::create_dir_all(&assets).map_err(|e| e.to_string())?;
    std::fs::copy(&xex, assets.join("doa4.xex")).map_err(|e| format!("cannot stage doa4.xex: {e}"))?;

    if !app_dir.join("generated/doa4/sources.cmake").exists() {
        let rex = find_rexglue(app_dir)
            .ok_or("rexgluerd.exe is missing - it ships next to compiler.exe; re-extract the release zip")?;
        p("Translating the game code (codegen)...", 0.38);
        // Relative manifest path on purpose: rexglue mis-decodes non-ASCII
        // absolute paths. See the note in CMakeLists.txt.
        let mut cmd = Command::new(&rex);
        cmd.current_dir(app_dir).args(["codegen", "./doa4retail_manifest.toml"]);
        run_logged(app_dir, &mut cmd, |l| {
            if !l.trim().is_empty() { p(&format!("Codegen: {}", l.trim()), 0.45); }
        })
        .map_err(|e| format!("Codegen failed:\n{e}"))?;
        if !app_dir.join("generated/doa4/sources.cmake").exists() {
            return Err("Codegen produced no sources - see compiler.log".into());
        }
    } else {
        p("Reusing existing generated code.", 0.5);
    }

    refresh_path(app_dir);
    let cmake = find_beside_or_path(app_dir, "cmake.exe")
        .ok_or("cmake.exe not found. Install CMake (the compiler offers to do this) and re-run.")?;

    p("Configuring the build...", 0.52);
    let mut cfg = Command::new(&cmake);
    cfg.current_dir(app_dir).args(["--preset", PRESET]);
    // A -D on the command line overrides the preset's cache variable.
    if let Some(c) = ffx_hostile_char(app_dir) {
        cfg.arg("-DREXGLUE_ENABLE_FIDELITYFX=OFF");
        let msg = format!(
            "Note: '{c}' in this folder's path breaks the FidelityFX shader compiler, so this build leaves FidelityFX out (CAS / FSR unavailable). Move the folder somewhere without that character to get them.");
        log_note(app_dir, &msg);
        p(&msg, 0.53);
    }
    run_logged(app_dir, &mut cfg, |l| {
        if !l.trim().is_empty() { p(&format!("CMake: {}", l.trim()), 0.55); }
    })
    .map_err(|e| format!("CMake configure failed:\n{e}"))?;

    p("Compiling (this takes a while; progress is real)...", 0.56);
    let mut bld = Command::new(&cmake);
    bld.current_dir(app_dir).args(["--build", "--preset", PRESET]);
    run_logged(app_dir, &mut bld, |l| {
        // ninja prefixes each step with [done/total].
        if let Some(rest) = l.strip_prefix('[') {
            if let Some((frac, what)) = rest.split_once(']') {
                if let Some((n, m)) = frac.split_once('/') {
                    if let (Ok(n), Ok(m)) = (n.trim().parse::<f32>(), m.trim().parse::<f32>()) {
                        if m > 0.0 {
                            let short = what.trim();
                            p(&format!("Compiling {}/{}  {}", n as u32, m as u32, short), 0.56 + 0.42 * (n / m));
                            return;
                        }
                    }
                }
            }
        }
    })
    .map_err(|e| format!("Build failed:\n{e}"))?;

    let out = app_dir.join("out/build").join(PRESET).join("doa4retail.exe");
    if !out.exists() { return Err("The build finished but doa4retail.exe is missing - see compiler.log".into()); }
    std::fs::copy(&out, app_dir.join("doa4retail.exe")).map_err(|e| e.to_string())?;
    copy_runtime_dlls(app_dir);
    p("Done. Run DOA4Launcher.exe.", 1.0);
    Ok(app_dir.join("doa4retail.exe"))
}

/// The runtime/GPU/FidelityFX DLLs land in build subdirs; the game needs them
/// beside the exe.
fn copy_runtime_dlls(app_dir: &Path) {
    let build = app_dir.join("out/build").join(PRESET);
    for sub in ["", "sdk-bin", "ffx-bin", "rexglue-sdk", "bin"] {
        let d = if sub.is_empty() { build.clone() } else { build.join(sub) };
        for e in std::fs::read_dir(&d).into_iter().flatten().flatten() {
            let p = e.path();
            if p.extension().map(|x| x.eq_ignore_ascii_case("dll")).unwrap_or(false) {
                let _ = std::fs::copy(&p, app_dir.join(p.file_name().unwrap()));
            }
        }
    }
}

/// What both front-ends run: extract, then codegen + build.
pub fn build_all(iso: &Path, app_dir: &Path, p: Progress) -> Result<PathBuf, String> {
    if let Some(why) = unusable_path_reason(app_dir) { return Err(why); }
    let _ = std::fs::write(log_path(app_dir), format!("DOA4 compiler log\niso: {}\n", iso.display()));
    extract(iso, app_dir, p)?;
    recompile(app_dir, p)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ninja_progress_lines_parse() {
        // Guards the [n/m] shape recompile() keys progress off.
        let mut seen = None;
        let line = "[783/1323] Building CXX object foo.cpp.obj";
        if let Some(rest) = line.strip_prefix('[') {
            if let Some((frac, _)) = rest.split_once(']') {
                let (n, m) = frac.split_once('/').unwrap();
                seen = Some((n.parse::<u32>().unwrap(), m.parse::<u32>().unwrap()));
            }
        }
        assert_eq!(seen, Some((783, 1323)));
    }

    #[test]
    fn xex_only_dump_is_enough_for_codegen_but_not_to_skip_extracting() {
        let dir = std::env::temp_dir().join("doa4_xexonly_test");
        let game = dir.join("game");
        std::fs::create_dir_all(&game).unwrap();
        std::fs::write(game.join("default.xex"), vec![0u8; XEX_SIZE as usize]).unwrap();
        assert!(have_xex(&dir), "codegen only needs the xex");
        assert!(!already_extracted(&dir), "no .afs yet, so do not skip the extract");
        std::fs::write(game.join("cmn.afs"), b"x").unwrap();
        assert!(already_extracted(&dir));
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn ffx_hostile_characters_are_spotted_but_spaces_are_not() {
        // Spaces were measured to be fine for the FidelityFX shader compiler.
        assert_eq!(ffx_hostile_char(Path::new(r"D:\Test Space folder\DOA4")), None);
        assert_eq!(ffx_hostile_char(Path::new(r"D:\Games\DOA4-recomp")), None);
        assert!(ffx_hostile_char(Path::new(r"C:\Users\José\DOA4")).is_some());
        // Measured as fine, so they must not switch FidelityFX off.
        assert_eq!(ffx_hostile_char(Path::new(r"D:'b(c)\DOA4")), None);
    }

    #[test]
    fn caret_in_the_path_is_refused_up_front() {
        assert!(unusable_path_reason(Path::new(r"D:^b\DOA4")).is_some());
        assert!(unusable_path_reason(Path::new(r"D:\Test Space'éç_(x)&y!z%w\DOA4")).is_none());
    }

    #[test]
    fn missing_iso_is_reported_not_panicked() {
        let dir = std::env::temp_dir().join("doa4_compiler_test");
        std::fs::create_dir_all(&dir).unwrap();
        let e = extract(Path::new("Z:\\nope.iso"), &dir, &mut |_, _| {}).unwrap_err();
        assert!(e.contains("No such file"), "{e}");
    }
}
