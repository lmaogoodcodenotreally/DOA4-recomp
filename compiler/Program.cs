using System.Diagnostics;

namespace Doa4Compiler;

// Quiver first-run helper: user picks their own USA Rev 1 ISO/XISO,
// compiler.exe extracts game\ into the app directory (beside doa4retail.exe).
// No game data ships in the repo or release zip.
sealed class CompilerForm : Form
{
    readonly TextBox _src = new() { Dock = DockStyle.Fill };
    readonly Label _status = new() { AutoSize = true };
    public CompilerForm()
    {
        Text = "DOA4 Compiler - pick your USA Rev 1 disc";
        Size = new Size(620, 280);
        StartPosition = FormStartPosition.CenterScreen;
        var go = new Button { Text = "Extract to game\\", AutoSize = true };
        go.Click += (_, _) => Extract();
        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 3, Padding = new Padding(10) };
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        layout.Controls.Add(new Label { Text = "ISO / XISO", AutoSize = true }, 0, 0);
        var row = new Panel { Dock = DockStyle.Fill };
        var browse = new Button { Text = "Browse...", AutoSize = true, Dock = DockStyle.Right };
        browse.Click += (_, _) =>
        {
            using var dlg = new OpenFileDialog { Filter = "Xbox 360 disc|*.iso;*.xiso;*.img|Game executable|*.xex|All files|*.*" };
            if (dlg.ShowDialog() == DialogResult.OK) _src.Text = dlg.FileName;
        };
        row.Controls.Add(_src); row.Controls.Add(browse);
        layout.Controls.Add(row, 1, 0);
        layout.Controls.Add(new Label { Text = "Extracts default.xex + *.afs into game\\ beside doa4retail.exe. USA Rev 1 only.", AutoSize = true }, 0, 1);
        layout.SetColumnSpan(layout.Controls[^1], 2);
        layout.Controls.Add(go, 0, 2); layout.Controls.Add(_status, 1, 2);
        Controls.Add(layout);
    }

    void Extract()
    {
        var src = _src.Text.Trim();
        if (!File.Exists(src)) { Fail("Pick an ISO / XISO file first."); return; }
        var appDir = AppContext.BaseDirectory;
        var gameDir = Path.Combine(appDir, "game");
        Directory.CreateDirectory(gameDir);
        try
        {
            if (src.EndsWith(".xex", StringComparison.OrdinalIgnoreCase))
            {
                // Single XEX: verify retail size, copy as default.xex.
                var fi = new FileInfo(src);
                if (fi.Length != 16465920) Fail($"Unexpected XEX size {fi.Length} (expected US Rev 1 default.xex 16465920). Wrong region/revision?");
                File.Copy(src, Path.Combine(gameDir, "default.xex"), overwrite: true);
            }
            else
            {
                // Prefer 7z.exe (bundled/PATH) which handles ISO9660/XISO roots;
                // fallback to extract-xiso on PATH. Neither ships game data.
                var sevenZip = FindOnPathOrBeside("7z.exe") ?? FindOnPathOrBeside("extract-xiso.exe");
                if (sevenZip == null) { Fail("7z.exe or extract-xiso.exe not found. Install 7-Zip or put extract-xiso.exe beside compiler.exe."); return; }
                _status.Text = "Extracting...";
                var psi = new ProcessStartInfo(sevenZip) { UseShellExecute = false, RedirectStandardOutput = true, RedirectStandardError = true };
                foreach (var a in new[] { "x", src, $"-o{gameDir}", "-y" }) psi.ArgumentList.Add(a);
                using var p = Process.Start(psi)!;
                var output = p.StandardOutput.ReadToEnd() + p.StandardError.ReadToEnd();
                p.WaitForExit();
                if (p.ExitCode != 0) { Fail($"Extractor exit {p.ExitCode}:\n{output}"); return; }
                // 7z keeps the source folder wrapper; flatten one level if present.
                var nested = Directory.GetFiles(gameDir, "default.xex", SearchOption.AllDirectories).FirstOrDefault();
                if (nested == null) { Fail($"Extracted but default.xex not found under {gameDir}:\n{output}"); return; }
                if (Path.GetDirectoryName(nested) != gameDir)
                {
                    var root = Path.GetDirectoryName(nested)!;
                    foreach (var f in Directory.GetFiles(root)) File.Move(f, Path.Combine(gameDir, Path.GetFileName(f)), overwrite: true);
                    foreach (var d in Directory.GetDirectories(root)) { var t = Path.Combine(gameDir, Path.GetFileName(d)); if (Directory.Exists(t)) Directory.Delete(t, true); Directory.Move(d, t); }
                }
            }
            var xex = Path.Combine(gameDir, "default.xex");
            if (!File.Exists(xex)) { Fail("default.xex missing after extract."); return; }
            _status.Text = "Done. You can now run DOA4Launcher.exe.";
            MessageBox.Show(this, "Game data ready in game\\\n\nNext: run DOA4Launcher.exe.", Text, MessageBoxButtons.OK, MessageBoxIcon.Information);
        }
        catch (Exception e) { Fail(e.Message); }
    }

    static string? FindOnPathOrBeside(string name)
    {
        var beside = Path.Combine(AppContext.BaseDirectory, name);
        if (File.Exists(beside)) return beside;
        var path = Environment.GetEnvironmentVariable("PATH") ?? "";
        foreach (var dir in path.Split(Path.PathSeparator)) { var p = Path.Combine(dir.Trim('"'), name); if (File.Exists(p)) return p; }
        return null;
    }
    void Fail(string m) => MessageBox.Show(this, m, Text, MessageBoxButtons.OK, MessageBoxIcon.Warning);
}

static class Program
{
    [STAThread]
    static void Main()
    {
        ApplicationConfiguration.Initialize();
        Application.Run(new CompilerForm());
    }
}
