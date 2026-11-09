// DOA4 Compiler (GUI) - extract + recompile. Design: docs/architecture.md.

struct App { src: String, status: String, missing: Vec<String> }

impl App { fn new() -> Self { Self { src: String::new(), status: "Pick USA Rev 1 ISO/XISO".into(), missing: vec![] } } }

impl eframe::App for App {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        // Drag & drop: files dropped on the window fill the ISO path.
        ctx.input(|i| {
            for f in &i.raw.dropped_files {
                if let Some(p) = &f.path { self.src = p.display().to_string(); self.status = format!("Dropped: {}", p.display()); }
            }
        });
        let app_dir = std::env::current_exe().ok().and_then(|p| p.parent().map(|p| p.to_path_buf())).unwrap_or_default();
        let missing = doa4_compiler::check_requirements(&app_dir);
        if !missing.is_empty() && self.missing.is_empty() {
            self.missing = missing.iter().map(|r| r.name.to_string()).collect();
            // keep Vec<Req> for install
        }
        egui::CentralPanel::default().show(ctx, |ui| {
            ui.heading("DOA4 Compiler - extract + recompile");
            ui.label("USA Rev 1 only. Checks/installs 7-Zip, CMake, Ninja, LLVM.");
            if !self.missing.is_empty() {
                ui.label(format!("Missing: {}", self.missing.join(", ")));
            }
            ui.horizontal(|ui| {
                ui.label("ISO / XISO");
                ui.add(egui::TextEdit::singleline(&mut self.src).desired_width(420.0));
            });
            ui.label("Tip: drag & drop the ISO onto this window, or paste the ISO path, then Extract + Recompile.");
            ui.label(&self.status);
            ui.horizontal(|ui| {
                if ui.button("Yes, install missing").clicked() {
                    self.status = "Installing with winget...".into();
                    for r in doa4_compiler::check_requirements(&app_dir) { let _ = doa4_compiler::install_requirement(&r); }
                    doa4_compiler::refresh_path();
                    self.status = "Install step done. Restart compiler.exe so PATH updates.".into();
                }
                if ui.button("No, skip install").clicked() {
                    self.status = "Skipping install.".into();
                }
            });
            if ui.button("Extract + Recompile").clicked() {
                self.status = "Extracting...".into();
                match doa4_compiler::extract(std::path::Path::new(&self.src), &app_dir) {
                    Ok(_) => {
                        self.status = "Extracted. Recompiling...".into();
                        match doa4_compiler::recompile(&app_dir) {
                            Ok(_) => self.status = "Done. Run DOA4Launcher.exe.".into(),
                            Err(e) => self.status = format!("Recompile failed: {e}"),
                        }
                    }
                    Err(e) => self.status = format!("Extract failed: {e}"),
                }
            }
        });
    }
}
fn main() -> eframe::Result<()> {
    let opts = eframe::NativeOptions { viewport: egui::ViewportBuilder::default().with_inner_size([640.0, 320.0]), ..Default::default() };
    eframe::run_native("DOA4 Compiler", opts, Box::new(|_| Ok(Box::new(App::new()))))
}
