// DOA4Launcher (GUI): pick settings, launch. Design notes: docs/architecture.md
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use doa4_launcher::*;
use std::path::{Path, PathBuf};

#[derive(PartialEq, Clone, Copy)]
enum Tab { Display, Graphics, Input, Paths }

struct App { dir: PathBuf, cfg: Config, status: String, tab: Tab }

impl App {
    fn new() -> Self {
        let dir = std::env::current_exe().ok().and_then(|p| p.parent().map(Path::to_path_buf)).unwrap_or_default();
        let mut cfg = load_cfg(&dir);
        let (lean, dev, data) = auto_detect(&dir);
        if cfg.lean_exe.is_empty() { cfg.lean_exe = lean; }
        if cfg.dev_exe.is_empty() { cfg.dev_exe = dev; }
        if cfg.data_root.is_empty() { cfg.data_root = data; }
        for s in all() { cfg.game.entry(s.key.to_string()).or_insert(s.default.to_string()); }
        Self { dir, cfg, status: String::new(), tab: Tab::Display }
    }

    fn rows(&mut self, ui: &mut egui::Ui, settings: Vec<Setting>) {
        egui::Grid::new("settings").num_columns(2).spacing([16.0, 8.0]).show(ui, |ui| {
            for s in settings {
                ui.label(s.label);
                let v = self.cfg.game.entry(s.key.to_string()).or_insert(s.default.to_string());
                if s.kind == Kind::Bool && s.choices.is_empty() {
                    let mut b = v == "true";
                    if ui.checkbox(&mut b, "").changed() { *v = b.to_string(); }
                } else if !s.choices.is_empty() {
                    let mut idx = s.choices.iter().position(|(val, _)| *val == v.as_str()).unwrap_or(0);
                    egui::ComboBox::from_id_salt(s.key).selected_text(s.choices[idx].1).show_ui(ui, |ui| {
                        for (i, (_, txt)) in s.choices.iter().enumerate() { ui.selectable_value(&mut idx, i, *txt); }
                    });
                    *v = s.choices[idx].0.to_string();
                } else {
                    ui.add(egui::TextEdit::singleline(v).desired_width(120.0));
                }
                ui.end_row();
            }
        });
    }
}

impl eframe::App for App {
    fn update(&mut self, ctx: &egui::Context, _f: &mut eframe::Frame) {
        let built = game_built(&self.cfg);
        egui::TopBottomPanel::top("head").show(ctx, |ui| {
            ui.add_space(4.0);
            ui.heading("Dead or Alive 4");
            if !built {
                ui.colored_label(egui::Color32::from_rgb(220, 150, 60),
                    "No build found here yet - run compiler.exe with your own USA / Europe Rev 1 ISO first.");
            }
            ui.add_space(4.0);
            ui.horizontal(|ui| {
                for (t, name) in [(Tab::Display, "Display"), (Tab::Graphics, "Graphics"), (Tab::Input, "Input"), (Tab::Paths, "Paths")] {
                    ui.selectable_value(&mut self.tab, t, name);
                }
            });
            ui.add_space(4.0);
        });

        egui::TopBottomPanel::bottom("foot").show(ctx, |ui| {
            ui.add_space(6.0);
            ui.horizontal(|ui| {
                if ui.add_enabled(built, egui::Button::new("Launch")).clicked() {
                    self.status = match launch(&self.dir, &self.cfg) { Ok(m) => m, Err(e) => e };
                }
                if ui.button("Save settings").clicked() {
                    save_cfg(&self.dir, &self.cfg);
                    self.status = format!("Saved to {}", self.dir.join("launcher.json").display());
                }
                if ui.button("Reset to defaults").clicked() {
                    for s in all() { self.cfg.game.insert(s.key.to_string(), s.default.to_string()); }
                    self.status = "Defaults restored (not saved yet).".into();
                }
            });
            if !self.status.is_empty() { ui.label(&self.status); }
            ui.add_space(6.0);
        });

        egui::CentralPanel::default().show(ctx, |ui| {
            egui::ScrollArea::vertical().show(ui, |ui| match self.tab {
                Tab::Display => self.rows(ui, display()),
                Tab::Graphics => {
                    self.rows(ui, graphics());
                    ui.add_space(8.0);
                    ui.label(egui::RichText::new(
                        "CAS and FSR need a FidelityFX-enabled build. The compiler turns FidelityFX \
                         off when the install path contains characters its shader compiler cannot \
                         handle - check compiler.log if these do nothing.").weak());
                }
                Tab::Input => self.rows(ui, input()),
                Tab::Paths => {
                    ui.label("Game executable");
                    ui.add(egui::TextEdit::singleline(&mut self.cfg.lean_exe).desired_width(f32::INFINITY));
                    ui.label("Game data folder (holds default.xex and the .afs files)");
                    ui.add(egui::TextEdit::singleline(&mut self.cfg.data_root).desired_width(f32::INFINITY));
                    if !self.cfg.dev_exe.is_empty() {
                        ui.add_space(8.0);
                        ui.label("A developer build was also found:");
                        ui.add(egui::TextEdit::singleline(&mut self.cfg.dev_exe).desired_width(f32::INFINITY));
                        ui.radio_value(&mut self.cfg.use_dev, false, "Play build (recommended)");
                        ui.radio_value(&mut self.cfg.use_dev, true, "Developer build (diagnostics)");
                    }
                    ui.add_space(8.0);
                    if ui.button("Re-detect").clicked() {
                        let (lean, dev, data) = auto_detect(&self.dir);
                        self.cfg.lean_exe = lean; self.cfg.dev_exe = dev; self.cfg.data_root = data;
                        self.status = "Paths re-detected.".into();
                    }
                }
            });
        });
    }

    fn on_exit(&mut self, _g: Option<&eframe::glow::Context>) { save_cfg(&self.dir, &self.cfg); }
}

fn main() -> eframe::Result<()> {
    let opts = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default().with_inner_size([600.0, 560.0]),
        ..Default::default()
    };
    eframe::run_native("DOA4 launcher", opts, Box::new(|_| Ok(Box::new(App::new()))))
}
