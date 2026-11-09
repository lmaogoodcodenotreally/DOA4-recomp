// DOA4Launcher (GUI) - design: docs/architecture.md
use doa4_launcher::*;
use std::path::PathBuf;

struct App { cfg: Config, status: String }
impl App {
    fn new() -> Self {
        let dir = std::env::current_exe().ok().and_then(|p| p.parent().map(|p| p.to_path_buf())).unwrap_or_default();
        let mut cfg = load_cfg(&dir);
        let (lean, dev, data) = auto_detect(&dir);
        if cfg.lean_exe.is_empty() { cfg.lean_exe = lean; }
        if cfg.dev_exe.is_empty() { cfg.dev_exe = dev; }
        if cfg.data_root.is_empty() { cfg.data_root = data; }
        for s in all() { cfg.game.entry(s.key.to_string()).or_insert(s.default.to_string()); }
        Self { cfg, status: String::new() }
    }
}
impl eframe::App for App {
    fn update(&mut self, ctx: &egui::Context, _f: &mut eframe::Frame) {
        egui::CentralPanel::default().show(ctx, |ui| {
            ui.heading("DOA4 Launcher");
            ui.label("Builds");
            ui.radio_value(&mut self.cfg.use_dev, false, "Play build (lean, recommended)");
            ui.radio_value(&mut self.cfg.use_dev, true, "Dev build (diagnostics)");
            ui.label("Play exe"); ui.add(egui::TextEdit::singleline(&mut self.cfg.lean_exe));
            ui.label("Dev exe"); ui.add(egui::TextEdit::singleline(&mut self.cfg.dev_exe));
            ui.label("Game data folder"); ui.add(egui::TextEdit::singleline(&mut self.cfg.data_root));
            ui.separator();
            egui::ScrollArea::vertical().show(ui, |ui| {
                for tab in ["Display", "Graphics", "Input"] {
                    ui.collapsing(tab, |ui| {
                        let settings = match tab { "Display" => display(), "Graphics" => graphics(), _ => input() };
                        for s in settings {
                            let v = self.cfg.game.entry(s.key.to_string()).or_insert(s.default.to_string());
                            if s.kind == Kind::Bool && s.choices.is_empty() { let mut b = v == "true"; if ui.checkbox(&mut b, s.label).changed() { *v = b.to_string(); } }
                            else if !s.choices.is_empty() {
                                let idx = s.choices.iter().position(|(val, _)| *val == v.as_str()).unwrap_or(0);
                                let mut idx = idx;
                                egui::ComboBox::from_id_source(tab.to_string() + s.key).selected_text(s.choices[*&idx].1).show_ui(ui, |ui| {
                                    for (i, (_, txt)) in s.choices.iter().enumerate() { ui.selectable_value(&mut idx, i, *txt); }
                                });
                                *v = s.choices[idx].0.to_string();
                            } else { ui.label(s.label); ui.add(egui::TextEdit::singleline(v)); }
                        }
                    });
                }
            });
            ui.label(&self.status);
            ui.horizontal(|ui| {
                if ui.button("Save").clicked() { let d = PathBuf::from("."); save_cfg(&d, &self.cfg); self.status = "Saved.".into(); }
                if ui.button("Launch").clicked() {
                    let d = std::env::current_exe().ok().and_then(|p| p.parent().map(|p| p.to_path_buf())).unwrap_or_default();
                    self.status = match launch(&d, &self.cfg) { Ok(m) => m, Err(e) => e };
                }
            });
        });
    }
}
fn main() -> eframe::Result<()> {
    let opts = eframe::NativeOptions { viewport: egui::ViewportBuilder::default().with_inner_size([680.0, 620.0]), ..Default::default() };
    eframe::run_native("DOA4 Launcher", opts, Box::new(|_| Ok(Box::new(App::new()))))
}
