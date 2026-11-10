// DOA4 compiler (GUI): turn the player's own ISO into a playable build.
// One window, three steps, a real progress bar and a live log tail.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use doa4_compiler as dc;
use std::path::{Path, PathBuf};
use std::sync::mpsc::{channel, Receiver, Sender};

enum Msg { Step(String, f32), Done(PathBuf), Failed(String) }

struct App {
    dir: PathBuf,
    iso: String,
    status: String,
    progress: f32,
    tail: Vec<String>,
    missing: Vec<dc::Req>,
    hint: String,
    rx: Option<Receiver<Msg>>,
    done: bool,
    error: String,
}

impl App {
    fn new() -> Self {
        let dir = std::env::current_exe().ok().and_then(|p| p.parent().map(Path::to_path_buf)).unwrap_or_default();
        let done = dir.join("doa4retail.exe").exists();
        let mut s = Self {
            missing: dc::check_requirements(&dir),
            status: if done {
                "A build already exists here. Run the launcher, or build again to update it.".into()
            } else {
                "Pick your own Dead or Alive 4 (USA, Rev 1) ISO to begin.".into()
            },
            dir, iso: String::new(), progress: 0.0, tail: vec![], hint: String::new(),
            rx: None, done, error: String::new(),
        };
        s.hint = s.missing.iter().filter(|r| r.winget.is_empty()).map(|r| r.hint).collect::<Vec<_>>().join("\n");
        s
    }

    fn busy(&self) -> bool { self.rx.is_some() }

    fn start(&mut self) {
        self.error.clear();
        self.tail.clear();
        self.progress = 0.0;
        self.done = false;
        let (tx, rx): (Sender<Msg>, Receiver<Msg>) = channel();
        self.rx = Some(rx);
        let (iso, dir) = (self.iso.trim().trim_matches('"').to_string(), self.dir.clone());
        std::thread::spawn(move || {
            let t2 = tx.clone();
            let mut on = |m: &str, f: f32| { let _ = t2.send(Msg::Step(m.to_string(), f)); };
            let r = dc::build_all(Path::new(&iso), &dir, &mut on);
            let _ = tx.send(match r { Ok(p) => Msg::Done(p), Err(e) => Msg::Failed(e) });
        });
    }
}

impl eframe::App for App {
    fn update(&mut self, ctx: &egui::Context, _f: &mut eframe::Frame) {
        ctx.input(|i| {
            if let Some(p) = i.raw.dropped_files.iter().find_map(|f| f.path.clone()) {
                self.iso = p.display().to_string();
            }
        });
        if let Some(rx) = &self.rx {
            let mut end = false;
            while let Ok(m) = rx.try_recv() {
                match m {
                    Msg::Step(s, f) => {
                        self.progress = f.clamp(0.0, 1.0);
                        if self.tail.last() != Some(&s) {
                            self.tail.push(s.clone());
                            if self.tail.len() > 9 { self.tail.remove(0); }
                        }
                        self.status = s;
                    }
                    Msg::Done(_) => { self.done = true; self.progress = 1.0; self.status = "Done. Open the launcher to play.".into(); end = true; }
                    Msg::Failed(e) => { self.error = e; self.status = "Build failed.".into(); end = true; }
                }
            }
            if end { self.rx = None; }
            ctx.request_repaint_after(std::time::Duration::from_millis(100));
        }

        egui::CentralPanel::default().show(ctx, |ui| {
            ui.heading("Dead or Alive 4 - build");
            ui.label("Needs your own Xbox 360 disc dump: USA / Europe multi-language, Rev 1.");
            ui.add_space(6.0);

            ui.group(|ui| {
                ui.label(egui::RichText::new("1. Tools").strong());
                if self.missing.is_empty() {
                    ui.label("All build tools found.");
                } else {
                    for r in &self.missing { ui.label(format!("missing: {}", r.name)); }
                    if !self.hint.is_empty() { ui.label(egui::RichText::new(&self.hint).italics()); }
                    let can = self.missing.iter().any(|r| !r.winget.is_empty());
                    if ui.add_enabled(can && !self.busy(), egui::Button::new("Install the missing tools (winget)")).clicked() {
                        self.status = "Installing... accept any Windows prompts.".into();
                        let mut errs = vec![];
                        for r in &self.missing {
                            if let Err(e) = dc::install_requirement(r) { if !r.winget.is_empty() { errs.push(e) } }
                        }
                        dc::refresh_path(&self.dir);
                        self.missing = dc::check_requirements(&self.dir);
                        self.hint = self.missing.iter().filter(|r| r.winget.is_empty()).map(|r| r.hint).collect::<Vec<_>>().join("\n");
                        self.status = if errs.is_empty() { "Tools installed.".into() } else { errs.join("\n") };
                    }
                }
            });

            ui.add_space(6.0);
            ui.group(|ui| {
                ui.label(egui::RichText::new("2. Your ISO").strong());
                ui.horizontal(|ui| {
                    ui.add(egui::TextEdit::singleline(&mut self.iso).desired_width(460.0).hint_text("drag the .iso here, or paste its path"));
                });
                if dc::already_extracted(&self.dir) {
                    ui.label("game\\ is already extracted here - the ISO step will be skipped.");
                }
            });

            ui.add_space(6.0);
            // Only the winget-installable tools block the build: those are found by
            // exact path, whereas MSVC is detected by probing the Visual Studio
            // layout, which can miss a non-default install. A wrong guess there
            // must not stop someone whose toolchain is fine.
            let ready = !self.missing.iter().any(|r| !r.winget.is_empty());
            let have_input = !self.iso.trim().is_empty() || dc::already_extracted(&self.dir);
            ui.horizontal(|ui| {
                if ui.add_enabled(!self.busy() && ready && have_input, egui::Button::new("3. Build")).clicked() { self.start(); }
                if self.done && ui.button("Open launcher").clicked() {
                    let _ = std::process::Command::new(self.dir.join("DOA4Launcher.exe")).current_dir(&self.dir).spawn();
                }
                if ui.button("Open log").clicked() {
                    let _ = std::process::Command::new("cmd").args(["/C", "start", "", &dc::log_path(&self.dir).display().to_string()]).spawn();
                }
            });
            if !ready {
                ui.label("Install the missing tools above first.");
            }

            ui.add_space(6.0);
            ui.add(egui::ProgressBar::new(self.progress).show_percentage());
            ui.label(&self.status);
            if !self.error.is_empty() {
                ui.add_space(4.0);
                ui.label(egui::RichText::new("Error").strong());
                egui::ScrollArea::vertical().max_height(140.0).id_source("err").show(ui, |ui| {
                    ui.add(egui::Label::new(egui::RichText::new(&self.error).monospace()).wrap());
                });
            } else if !self.tail.is_empty() {
                egui::ScrollArea::vertical().max_height(120.0).stick_to_bottom(true).id_source("tail").show(ui, |ui| {
                    for l in &self.tail { ui.label(egui::RichText::new(l).monospace().weak()); }
                });
            }
        });
    }
}

fn main() -> eframe::Result<()> {
    let opts = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default().with_inner_size([700.0, 640.0]),
        ..Default::default()
    };
    eframe::run_native("DOA4 compiler", opts, Box::new(|_| Ok(Box::new(App::new()))))
}
