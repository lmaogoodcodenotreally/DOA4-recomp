// DOA4Launcher (GUI): pick settings, launch. Design notes: docs/architecture.md
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use doa4_launcher::*;
use std::path::{Path, PathBuf};

#[derive(PartialEq, Clone, Copy)]
enum Tab { Display, Graphics, Input, Debug, Paths }

struct App {
    dir: PathBuf,
    cfg: Config,
    status: String,
    tab: Tab,
    /// Physical size of the monitor, once egui has told us. Used for the
    /// "match my display" aspect-ratio shortcut.
    monitor: Option<(f32, f32)>,
    /// The bind waiting for a key press ("..." on its button).
    capturing: Option<&'static str>,
}

/// The game's name for a key egui reports, if the game can bind it.
fn game_key_name(k: egui::Key) -> Option<String> {
    use egui::Key::*;
    let fixed = match k {
        ArrowLeft => "Left", ArrowRight => "Right", ArrowUp => "Up", ArrowDown => "Down",
        Enter => "Return", Space => "Space", Tab => "Tab", Backspace => "Backspace",
        Delete => "Delete", Insert => "Insert", Home => "Home", End => "End",
        PageUp => "PageUp", PageDown => "PageDown", Minus => "Minus", Plus | Equals => "Plus",
        Comma => "Comma", Period => "Period", Semicolon => "Semicolon", Slash => "Slash",
        Backslash => "Backslash", OpenBracket => "LBracket", CloseBracket => "RBracket",
        Quote => "Quote", Backtick => "Backtick",
        Num0 => "0", Num1 => "1", Num2 => "2", Num3 => "3", Num4 => "4",
        Num5 => "5", Num6 => "6", Num7 => "7", Num8 => "8", Num9 => "9",
        // Letters and F1-F24 are spelled the same by egui and the game.
        _ => return Some(k.name().to_string()).filter(|n| KEY_NAMES.contains(&n.as_str())),
    };
    Some(fixed.to_string())
}

impl App {
    fn new() -> Self {
        let dir = std::env::current_exe().ok().and_then(|p| p.parent().map(Path::to_path_buf)).unwrap_or_default();
        let mut cfg = load_cfg(&dir);
        let (lean, dev, data) = auto_detect(&dir);
        if cfg.lean_exe.is_empty() { cfg.lean_exe = lean; }
        if cfg.dev_exe.is_empty() { cfg.dev_exe = dev; }
        if cfg.data_root.is_empty() { cfg.data_root = data; }
        for s in all() { cfg.game.entry(s.key.to_string()).or_insert(s.default.to_string()); }
        Self { dir, cfg, status: String::new(), tab: Tab::Display, monitor: None, capturing: None }
    }

    /// While a bind is capturing, the next key press becomes its key (Escape
    /// cancels). The press is consumed so no widget also reacts to it.
    fn capture_key(&mut self, ctx: &egui::Context) {
        let Some(key) = self.capturing else { return };
        let pressed = ctx.input(|i| i.events.iter().find_map(|e| match e {
            egui::Event::Key { key, pressed: true, repeat: false, .. } => Some(*key),
            _ => None,
        }));
        let Some(k) = pressed else { return };
        ctx.input_mut(|i| { i.consume_key(egui::Modifiers::NONE, k); i.events.clear(); });
        if k == egui::Key::Escape {
            self.capturing = None;
            return;
        }
        match game_key_name(k) {
            Some(name) => {
                self.cfg.game.insert(key.to_string(), name);
                self.capturing = None;
            }
            None => self.status = format!("{} cannot be bound; press another key.", k.name()),
        }
    }

    /// A key bind: click it, then press the new key. The arrow resets it.
    fn bind_row(&mut self, ui: &mut egui::Ui, s: &Setting) {
        let v = self.cfg.game.get(s.key).cloned().unwrap_or_else(|| s.default.to_string());
        ui.horizontal(|ui| {
            let waiting = self.capturing == Some(s.key);
            let text = if waiting { "...".to_string() } else { v.clone() };
            let r = ui.add(egui::Button::new(text).min_size(egui::vec2(90.0, 0.0)).selected(waiting))
                .on_hover_text(format!("{}\n\nClick, then press the new key (Escape cancels).", s.help));
            if r.clicked() {
                self.capturing = if waiting { None } else { Some(s.key) };
            }
            let reset = ui.add_enabled(v != s.default, egui::Button::new("\u{21BA}"))
                .on_hover_text(format!("Reset to {}", s.default));
            if reset.clicked() {
                self.cfg.game.insert(s.key.to_string(), s.default.to_string());
                if waiting { self.capturing = None; }
            }
            if let Some(other) = bind_conflict(&self.cfg, s.key, &v) {
                ui.colored_label(egui::Color32::from_rgb(220, 150, 60), format!("also used by {other}"));
            }
        });
    }

    /// One settings row: label on the left, control on the right, the same
    /// explanation on the hover of both.
    fn row(&mut self, ui: &mut egui::Ui, s: &Setting) {
        ui.label(s.label).on_hover_text(s.help);
        if s.kind == Kind::Key {
            self.bind_row(ui, s);
            ui.end_row();
            return;
        }
        let v = self.cfg.game.entry(s.key.to_string()).or_insert(s.default.to_string());
        let r = if s.kind == Kind::Bool && s.choices.is_empty() {
            let mut b = v == "true";
            let r = ui.checkbox(&mut b, "");
            if r.changed() { *v = b.to_string(); }
            r
        } else if !s.choices.is_empty() {
            let mut idx = s.choices.iter().position(|(val, _)| *val == v.as_str()).unwrap_or(0);
            let r = egui::ComboBox::from_id_salt(s.key)
                .selected_text(s.choices[idx].1)
                .width(210.0)
                .show_ui(ui, |ui| {
                    for (i, (_, txt)) in s.choices.iter().enumerate() {
                        ui.selectable_value(&mut idx, i, *txt);
                    }
                })
                .response;
            *v = s.choices[idx].0.to_string();
            r
        } else {
            ui.add(egui::TextEdit::singleline(v).desired_width(90.0))
        };
        r.on_hover_text(s.help);
        ui.end_row();
    }

    fn grid(&mut self, ui: &mut egui::Ui, id: &str, settings: &[Setting]) {
        egui::Grid::new(id).num_columns(2).spacing([18.0, 9.0]).show(ui, |ui| {
            for s in settings { self.row(ui, s); }
        });
    }

    /// Plain rows, then anything marked advanced behind a collapsing header so
    /// a first-time player is not asked to choose a per-axis render scale.
    fn tab_body(&mut self, ui: &mut egui::Ui, id: &str, settings: Vec<Setting>) {
        let (plain, adv): (Vec<Setting>, Vec<Setting>) =
            settings.into_iter().partition(|s| !s.advanced);
        self.grid(ui, id, &plain);
        if !adv.is_empty() {
            ui.add_space(10.0);
            egui::CollapsingHeader::new("Advanced").id_salt(format!("{id}_adv")).show(ui, |ui| {
                self.grid(ui, &format!("{id}_adv_grid"), &adv);
            });
        }
    }

    fn preset_bar(&mut self, ui: &mut egui::Ui) {
        let active = active_preset(&self.cfg);
        ui.horizontal_wrapped(|ui| {
            ui.label("Preset");
            for p in presets() {
                let picked = active == Some(p.name);
                if ui.selectable_label(picked, p.name).on_hover_text(p.help).clicked() && !picked {
                    apply_preset(&mut self.cfg, p.name);
                    self.status = format!("{} preset applied.", p.name);
                }
            }
            // Not a button: Custom is what you get by changing anything below.
            ui.add_enabled(
                false,
                egui::SelectableLabel::new(active.is_none(), "Custom"),
            )
            .on_hover_text("Shown automatically once your graphics settings do not match a preset.");
        });
    }

    fn path_row(&mut self, ui: &mut egui::Ui, label: &str, pick_dir: bool, which: usize) {
        ui.label(label);
        ui.horizontal(|ui| {
            let field = match which {
                0 => &mut self.cfg.lean_exe,
                1 => &mut self.cfg.data_root,
                _ => &mut self.cfg.dev_exe,
            };
            ui.add(egui::TextEdit::singleline(field).desired_width(360.0));
            if ui.button("Browse...").clicked() {
                let start = if field.is_empty() { self.dir.clone() } else { PathBuf::from(&*field) };
                let dlg = rfd::FileDialog::new().set_directory(start.parent().unwrap_or(&self.dir));
                let picked = if pick_dir {
                    dlg.pick_folder()
                } else {
                    dlg.add_filter("Game executable", &["exe"]).pick_file()
                };
                if let Some(p) = picked { *field = p.display().to_string(); }
            }
        });
    }
}

impl eframe::App for App {
    fn update(&mut self, ctx: &egui::Context, _f: &mut eframe::Frame) {
        if self.monitor.is_none() {
            self.monitor = ctx.input(|i| i.viewport().monitor_size).map(|s| (s.x, s.y));
        }
        self.capture_key(ctx);
        let built = game_built(&self.cfg);

        egui::TopBottomPanel::top("head").show(ctx, |ui| {
            ui.add_space(6.0);
            ui.heading("Dead or Alive 4");
            if built {
                // Just the file name: a full path wraps over several lines and
                // pushes the tabs around. The whole thing is on the tooltip.
                let name = Path::new(&self.cfg.lean_exe)
                    .file_name().map(|n| n.to_string_lossy().into_owned())
                    .unwrap_or_else(|| self.cfg.lean_exe.clone());
                ui.label(egui::RichText::new(format!("Ready - {name}")).weak())
                    .on_hover_text(&self.cfg.lean_exe);
            } else {
                ui.colored_label(
                    egui::Color32::from_rgb(220, 150, 60),
                    "No build here yet. Run compiler.exe with your own USA / Europe Rev 1 ISO first.",
                );
            }
            ui.add_space(8.0);
            ui.horizontal(|ui| {
                for (t, name) in [(Tab::Display, "Display"), (Tab::Graphics, "Graphics"),
                                  (Tab::Input, "Input"), (Tab::Debug, "Debug"), (Tab::Paths, "Paths")] {
                    // Debug switches only exist in a developer build.
                    if t == Tab::Debug && self.cfg.dev_exe.is_empty() { continue; }
                    ui.selectable_value(&mut self.tab, t, name);
                }
            });
            ui.add_space(6.0);
        });

        egui::TopBottomPanel::bottom("foot").show(ctx, |ui| {
            ui.add_space(8.0);
            ui.horizontal(|ui| {
                let btn = egui::Button::new(egui::RichText::new("Launch").strong());
                if ui.add_enabled(built, btn).clicked() {
                    self.status = match launch(&self.dir, &self.cfg) { Ok(m) => m, Err(e) => e };
                }
                if ui.button("Save").clicked() {
                    save_cfg(&self.dir, &self.cfg);
                    self.status = "Settings saved.".into();
                }
                if ui.button("Reset to defaults").clicked() {
                    for s in all() { self.cfg.game.insert(s.key.to_string(), s.default.to_string()); }
                    self.status = "Defaults restored (not saved yet).".into();
                }
            });
            if !self.status.is_empty() {
                ui.add_space(2.0);
                ui.label(&self.status);
            }
            ui.add_space(8.0);
        });

        egui::CentralPanel::default().show(ctx, |ui| {
            egui::ScrollArea::vertical().show(ui, |ui| {
                ui.add_space(4.0);
                match self.tab {
                    Tab::Display => {
                        self.tab_body(ui, "display", display());
                        if let Some((w, h)) = self.monitor {
                            if let Some(a) = aspect_for(w, h) {
                                ui.add_space(10.0);
                                ui.horizontal_wrapped(|ui| {
                                    if ui.button(format!("Match my display ({} x {})", w as u32, h as u32))
                                        .on_hover_text("Writes this display's exact aspect ratio, \
                                                        instead of relying on the window being measured \
                                                        correctly, and turns the 16:9 letterbox off.")
                                        .clicked()
                                    {
                                        self.cfg.game.insert("aspect_ratio".into(), a.clone());
                                        self.cfg.game.insert("present_letterbox".into(), "false".into());
                                        self.status = format!("Aspect ratio set to {a} and letterbox turned off.");
                                    }
                                });
                            }
                        }
                    }
                    Tab::Graphics => {
                        self.preset_bar(ui);
                        ui.add_space(10.0);
                        self.tab_body(ui, "graphics", graphics());
                        ui.add_space(8.0);
                        ui.label(egui::RichText::new(
                            "Internal resolution and the per-axis render scales only take effect \
                             after the game is restarted.").weak());
                    }
                    Tab::Input => self.tab_body(ui, "input", input()),
                    Tab::Debug => {
                        if !self.cfg.use_dev {
                            ui.label(egui::RichText::new(
                                "These only take effect with the Developer build selected on the Paths tab.")
                                .weak());
                            ui.add_space(8.0);
                        }
                        self.tab_body(ui, "debug", debug());
                    }
                    Tab::Paths => {
                        self.path_row(ui, "Game executable", false, 0);
                        ui.add_space(6.0);
                        self.path_row(ui, "Game data folder (holds default.xex and the .afs files)", true, 1);
                        if !self.cfg.dev_exe.is_empty() {
                            ui.add_space(12.0);
                            ui.separator();
                            ui.label("A developer build was also found:");
                            self.path_row(ui, "Developer executable", false, 2);
                            ui.radio_value(&mut self.cfg.use_dev, false, "Play build (recommended)");
                            ui.radio_value(&mut self.cfg.use_dev, true, "Developer build (diagnostics)");
                        }
                        ui.add_space(12.0);
                        if ui.button("Re-detect").clicked() {
                            self.status = if redetect(&mut self.cfg, auto_detect(&self.dir)) {
                                "Missing paths re-detected.".into()
                            } else {
                                "All paths are valid; nothing changed.".into()
                            };
                        }
                    }
                }
                ui.add_space(4.0);
            });
        });
    }

    fn on_exit(&mut self, _g: Option<&eframe::glow::Context>) { save_cfg(&self.dir, &self.cfg); }
}

fn main() -> eframe::Result<()> {
    let opts = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_inner_size([640.0, 600.0])
            .with_min_inner_size([520.0, 420.0]),
        ..Default::default()
    };
    eframe::run_native("DOA4 launcher", opts, Box::new(|_| Ok(Box::new(App::new()))))
}
