// DOA4 Compiler (GUI) - extract + recompile (background thread + progress).

struct App {
    src: String, status: String, missing: Vec<String>,
    busy: bool, rx: Option<std::sync::mpsc::Receiver<String>>,
}
impl App { fn new() -> Self { Self { src: String::new(), status: "Pick USA Rev 1 ISO/XISO".into(), missing: vec![], busy: false, rx: None } } }
impl eframe::App for App {
    fn update(&mut self, ctx: &egui::Context, _f: &mut eframe::Frame) {
        ctx.input(|i| { for f in &i.raw.dropped_files { if let Some(p) = &f.path { self.src = p.display().to_string(); self.status = format!("Dropped: {}", p.display()); } } });
        if let Some(rx) = &self.rx { while let Ok(m) = rx.try_recv() { let done = m.starts_with("Done") || m.contains("failed"); self.status = m; if done { self.busy = false; } } ctx.request_repaint(); }
        let app_dir = std::env::current_exe().ok().and_then(|p| p.parent().map(|p| p.to_path_buf())).unwrap_or_default();
        if self.missing.is_empty() && self.rx.is_none() && !self.busy {
            let missing = doa4_compiler::check_requirements(&app_dir);
            if !missing.is_empty() { self.missing = missing.iter().map(|r| r.name.to_string()).collect(); }
        }
        egui::CentralPanel::default().show(ctx, |ui| {
            ui.heading("DOA4 Compiler - extract + recompile");
            ui.label("USA Rev 1 only. Checks/installs 7-Zip, CMake, Ninja, LLVM.");
            if !self.missing.is_empty() { ui.label(format!("Missing: {}", self.missing.join(", "))); }
            ui.horizontal(|ui| { ui.label("ISO / XISO"); ui.add(egui::TextEdit::singleline(&mut self.src).desired_width(420.0)); });
            ui.label("Tip: drag & drop the ISO onto this window, or paste the ISO path, then Extract + Recompile.");
            if self.busy { ui.add(egui::ProgressBar::new(f32::INFINITY).show_percentage()); }
            ui.label(&self.status);
            let en = !self.busy;
            ui.add_enabled_ui(en, |ui| {
                ui.horizontal(|ui| {
                    if ui.button("Yes, install missing").clicked() {
                        self.status = "Installing with winget...".into();
                        for r in doa4_compiler::check_requirements(&app_dir) { let _ = doa4_compiler::install_requirement(&r); }
                        doa4_compiler::refresh_path();
                        self.status = "Install step done. Restart compiler.exe so PATH updates.".into();
                    }
                    if ui.button("No, skip install").clicked() { self.status = "Skipping install.".into(); }
                });
                if ui.button("Extract + Recompile").clicked() {
                    let src = self.src.clone();
                    let (tx, rx) = std::sync::mpsc::channel();
                    self.rx = Some(rx); self.busy = true;
                    std::thread::spawn(move || {
                        let _ = tx.send("Extracting...".into());
                        match doa4_compiler::extract(std::path::Path::new(&src), &app_dir) {
                            Ok(_) => {
                                let _ = tx.send("Extracted. Recompiling (codegen + cmake)...".into());
                                let _ = tx.send("Building... this can take several minutes.".into());
                                match doa4_compiler::recompile(&app_dir) {
                                    Ok(_) => { let _ = tx.send("Done. Run DOA4Launcher.exe.".into()); }
                                    Err(e) => { let _ = tx.send(format!("Recompile failed: {e}")); }
                                }
                            }
                            Err(e) => { let _ = tx.send(format!("Extract failed: {e}")); }
                        }
                    });
                }
            });
        });
    }
}
fn main() -> eframe::Result<()> {
    let opts = eframe::NativeOptions { viewport: egui::ViewportBuilder::default().with_inner_size([640.0, 320.0]), ..Default::default() };
    eframe::run_native("DOA4 Compiler", opts, Box::new(|_| Ok(Box::new(App::new()))))
}
