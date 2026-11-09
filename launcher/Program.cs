using System.Diagnostics;

namespace Doa4Launcher;

static class Program
{
    [STAThread]
    static int Main(string[] args)
    {
        if (args.Contains("--selftest")) return SelfTest();
        ApplicationConfiguration.Initialize();
        Application.Run(new LauncherForm());
        return 0;
    }

    // Exit code 0 = TOML merge keeps foreign lines, replaces managed keys, appends new ones.
    static int SelfTest()
    {
        var input = "# Auto-generated cvar configuration\r\nlog_level = \"debug\"\r\nfullscreen = false\r\n";
        var merged = Toml.Merge(input, new Dictionary<string, string> { ["fullscreen"] = "true", ["resolution_scale"] = "2" });
        bool ok = merged.Contains("# Auto-generated cvar configuration\n") && merged.Contains("log_level = \"debug\"\n")
                  && merged.Contains("fullscreen = true\n") && !merged.Contains("fullscreen = false")
                  && merged.Contains("resolution_scale = 2\n");
        return ok ? 0 : 1;
    }
}

sealed class LauncherForm : Form
{
    readonly LauncherConfig _cfg = LauncherConfig.Load();
    readonly Dictionary<string, Control> _controls = new();
    readonly TextBox _leanExe = new() { Dock = DockStyle.Fill };
    readonly TextBox _devExe = new() { Dock = DockStyle.Fill };
    readonly TextBox _dataRoot = new() { Dock = DockStyle.Fill };
    readonly RadioButton _lean = new() { Text = "Play build (lean, recommended)", AutoSize = true };
    readonly RadioButton _dev = new() { Text = "Dev build (diagnostics on)", AutoSize = true };
    readonly Label _status = new() { AutoSize = true, Anchor = AnchorStyles.Left };
    readonly ToolTip _tips = new();

    public LauncherForm()
    {
        Text = "DOA4 Launcher";
        Size = new Size(620, 500);
        MinimumSize = new Size(560, 440);
        StartPosition = FormStartPosition.CenterScreen;

        AutoDetectPaths();
        if (_cfg.Game.Count == 0) ImportFromGame();

        var tabs = new TabControl { Dock = DockStyle.Fill };
        tabs.TabPages.Add(GameTab());
        tabs.TabPages.Add(SettingsTab("Display", Settings.Display,
            "The game runs at 60 Hz with v-sync: its speed depends on it. Frame interpolation adds "
            + "in-between frames without changing that."));
        tabs.TabPages.Add(SettingsTab("Graphics", Settings.Graphics, FfxNote()));
        tabs.TabPages.Add(SettingsTab("Input && Audio", Settings.Input, null));

        var save = new Button { Text = "Save", AutoSize = true };
        var launch = new Button { Text = "Launch", AutoSize = true, Font = new Font(Font, FontStyle.Bold) };
        save.Click += (_, _) => { Collect(); _cfg.Save(); _status.Text = "Settings saved."; };
        launch.Click += (_, _) => Launch();
        var bottom = new FlowLayoutPanel { Dock = DockStyle.Bottom, AutoSize = true, FlowDirection = FlowDirection.RightToLeft, Padding = new Padding(6) };
        bottom.Controls.AddRange(new Control[] { launch, save, _status });

        Controls.Add(tabs);
        Controls.Add(bottom);
        AcceptButton = launch;
        RefreshUpscalers();
    }

    TabPage GameTab()
    {
        var page = new TabPage("Game") { Padding = new Padding(10) };
        var grid = Grid();
        _leanExe.Text = _cfg.LeanExe;
        _devExe.Text = _cfg.DevExe;
        _dataRoot.Text = _cfg.DataRoot;
        (_cfg.UseDev ? _dev : _lean).Checked = true;
        _lean.CheckedChanged += (_, _) => RefreshUpscalers();
        _leanExe.TextChanged += (_, _) => RefreshUpscalers();
        _devExe.TextChanged += (_, _) => RefreshUpscalers();

        var builds = new FlowLayoutPanel { AutoSize = true, FlowDirection = FlowDirection.TopDown };
        builds.Controls.AddRange(new Control[] { _lean, _dev });
        AddRow(grid, "Build", builds);
        AddRow(grid, "Play build exe", PathRow(_leanExe, pickFile: true));
        AddRow(grid, "Dev build exe", PathRow(_devExe, pickFile: true));
        AddRow(grid, "Game data folder", PathRow(_dataRoot, pickFile: false));
        page.Controls.Add(grid);
        return page;
    }

    TabPage SettingsTab(string title, Setting[] settings, string? note)
    {
        var page = new TabPage(title) { Padding = new Padding(10) };
        var grid = Grid();
        foreach (var s in settings)
        {
            var value = _cfg.Game.TryGetValue(s.Key, out var v) ? v : s.Default;
            Control c;
            if (s.Kind == Kind.Bool && s.Choices == null)
                c = new CheckBox { Text = s.Label, AutoSize = true, Checked = value == "true" };
            else if (s.Choices != null)
            {
                var combo = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 240 };
                combo.Items.AddRange(s.Choices.Select(ch => ch.Text).ToArray<object>());
                combo.SelectedIndex = Math.Max(0, Array.FindIndex(s.Choices, ch => ch.Value == value));
                c = combo;
            }
            else
                c = new NumericUpDown { Minimum = 0, Maximum = 8192, Width = 100,
                                        Value = int.TryParse(value, out var n) ? n : 0 };
            _tips.SetToolTip(c, s.Tip);
            _controls[s.Key] = c;
            AddRow(grid, c is CheckBox ? "" : s.Label, c);
        }
        if (note != null)
            AddRow(grid, "", new Label { Text = note, AutoSize = true, MaximumSize = new Size(520, 0), ForeColor = SystemColors.GrayText });
        page.Controls.Add(grid);
        return page;
    }

    // FSR/CAS exist only when the build ships the FidelityFX runtime.
    bool BuildHasFfx()
    {
        var exe = _dev.Checked ? _devExe.Text : _leanExe.Text;
        var dir = Path.GetDirectoryName(exe);
        return dir != null && Directory.Exists(dir) && Directory.EnumerateFiles(dir, "amd_fidelityfx*.dll").Any();
    }

    string FfxNote() => "AMD FSR / CAS appear under Output scaling only for builds made with FidelityFX.";

    void RefreshUpscalers()
    {
        if (!_controls.TryGetValue("present_effect", out var c) || c is not ComboBox combo) return;
        var setting = Settings.Graphics.First(s => s.Key == "present_effect");
        var current = combo.SelectedIndex >= 0 && combo.SelectedIndex < combo.Items.Count
            ? setting.Choices!.First(ch => ch.Text == (string)combo.SelectedItem!).Value : setting.Default;
        var allowed = BuildHasFfx() ? setting.Choices! : setting.Choices!.Where(ch => ch.Value == "bilinear").ToArray();
        combo.Items.Clear();
        combo.Items.AddRange(allowed.Select(ch => ch.Text).ToArray<object>());
        combo.SelectedIndex = Math.Max(0, Array.FindIndex(allowed, ch => ch.Value == current));
    }

    void Collect()
    {
        _cfg.LeanExe = _leanExe.Text.Trim();
        _cfg.DevExe = _devExe.Text.Trim();
        _cfg.DataRoot = _dataRoot.Text.Trim();
        _cfg.UseDev = _dev.Checked;
        foreach (var s in Settings.All)
        {
            _cfg.Game[s.Key] = _controls[s.Key] switch
            {
                CheckBox cb => cb.Checked ? "true" : "false",
                ComboBox combo => s.Choices!.First(ch => ch.Text == (string)combo.SelectedItem!).Value,
                NumericUpDown num => ((int)num.Value).ToString(),
                _ => s.Default,
            };
        }
        // A non-16:9 camera only looks right when the picture fills the screen.
        if (_cfg.Game["aspect_ratio"] != "0") _cfg.Game["present_letterbox"] = "false";
    }

    void Launch()
    {
        Collect();
        _cfg.Save();
        var exe = _cfg.UseDev ? _cfg.DevExe : _cfg.LeanExe;
        if (!File.Exists(exe)) { Fail($"Game exe not found:\n{exe}"); return; }
        if (!Directory.Exists(_cfg.DataRoot)) { Fail($"Game data folder not found:\n{_cfg.DataRoot}"); return; }
        if (Process.GetProcessesByName(Path.GetFileNameWithoutExtension(exe)).Length > 0)
        {
            Fail("The game is already running. Close it first.");
            return;
        }

        var exeDir = Path.GetDirectoryName(exe)!;
        var tomlPath = Path.Combine(exeDir, Path.GetFileNameWithoutExtension(exe) + ".toml");
        var managed = Settings.All.ToDictionary(s => s.Key, s => Settings.ToToml(s, _cfg.Game[s.Key]));
        try
        {
            var existing = File.Exists(tomlPath) ? File.ReadAllText(tomlPath) : "# Auto-generated cvar configuration\n";
            File.WriteAllText(tomlPath, Toml.Merge(existing, managed));
        }
        catch (Exception e) { Fail($"Could not write game settings:\n{tomlPath}\n{e.Message}"); return; }

        var logDir = Path.Combine(Path.GetTempPath(), "DOA4Launcher");
        Directory.CreateDirectory(logDir);
        var psi = new ProcessStartInfo(exe) { WorkingDirectory = exeDir, UseShellExecute = false };
        foreach (var a in new[] { "--game_data_root", _cfg.DataRoot, "--gpu_plugin", "xenos",
                                  "--log_file", Path.Combine(logDir, "doa4.log"), "--log_level", "info" })
            psi.ArgumentList.Add(a);
        // The play build keeps every SDK diagnostic probe inert.
        if (!_cfg.UseDev) psi.Environment["REX_DIAG"] = "0";
        try
        {
            var p = Process.Start(psi);
            _status.Text = $"Launched {(_cfg.UseDev ? "dev" : "play")} build (pid {p?.Id}).";
        }
        catch (Exception e) { Fail($"Could not start the game:\n{e.Message}"); }
    }

    void Fail(string message) => MessageBox.Show(this, message, Text, MessageBoxButtons.OK, MessageBoxIcon.Warning);

    // First run: find the builds and the data folder - the titleupdate package
    // (exe beside the launcher, data in game\) or the usual repo layout.
    void AutoDetectPaths()
    {
        var here = AppContext.BaseDirectory;
        if (_cfg.LeanExe == "" && File.Exists(Path.Combine(here, "doa4retail.exe")))
            _cfg.LeanExe = Path.Combine(here, "doa4retail.exe");
        if (_cfg.DataRoot == "" && Directory.Exists(Path.Combine(here, "game")))
            _cfg.DataRoot = Path.Combine(here, "game");
        for (var dir = new DirectoryInfo(AppContext.BaseDirectory); dir != null; dir = dir.Parent)
        {
            var build = Path.Combine(dir.FullName, "out", "build");
            if (_cfg.LeanExe == "" && File.Exists(Path.Combine(build, "win-amd64-relwithdebinfo-lean", "doa4retail.exe")))
                _cfg.LeanExe = Path.Combine(build, "win-amd64-relwithdebinfo-lean", "doa4retail.exe");
            if (_cfg.DevExe == "" && File.Exists(Path.Combine(build, "win-amd64-relwithdebinfo", "doa4retail.exe")))
                _cfg.DevExe = Path.Combine(build, "win-amd64-relwithdebinfo", "doa4retail.exe");
            if (_cfg.DataRoot == "" && Directory.Exists(Path.Combine(dir.FullName, "DOA4_Retail_Rev1")))
                _cfg.DataRoot = Path.Combine(dir.FullName, "DOA4_Retail_Rev1");
        }
    }

    // First run: start from what the game is already configured with.
    void ImportFromGame()
    {
        var exe = _cfg.LeanExe != "" ? _cfg.LeanExe : _cfg.DevExe;
        if (exe == "") return;
        var toml = Toml.Read(Path.Combine(Path.GetDirectoryName(exe)!, Path.GetFileNameWithoutExtension(exe) + ".toml"));
        foreach (var s in Settings.All)
            if (toml.TryGetValue(s.Key, out var v)) _cfg.Game[s.Key] = v;
        if (_cfg.DataRoot == "" && toml.TryGetValue("game_data_root", out var root)) _cfg.DataRoot = root;
    }

    static TableLayoutPanel Grid()
    {
        var grid = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, AutoScroll = true };
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        return grid;
    }

    static void AddRow(TableLayoutPanel grid, string label, Control control)
    {
        grid.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        grid.Controls.Add(new Label { Text = label, AutoSize = true, Anchor = AnchorStyles.Left, Padding = new Padding(0, 6, 8, 0) });
        grid.Controls.Add(control);
    }

    Control PathRow(TextBox box, bool pickFile)
    {
        var browse = new Button { Text = "Browse...", AutoSize = true, Dock = DockStyle.Right };
        browse.Click += (_, _) =>
        {
            if (pickFile)
            {
                using var dlg = new OpenFileDialog { Filter = "Game executable|*.exe", FileName = box.Text };
                if (dlg.ShowDialog(this) == DialogResult.OK) box.Text = dlg.FileName;
            }
            else
            {
                using var dlg = new FolderBrowserDialog { SelectedPath = box.Text };
                if (dlg.ShowDialog(this) == DialogResult.OK) box.Text = dlg.SelectedPath;
            }
        };
        var row = new Panel { Dock = DockStyle.Fill, Height = 28 };
        row.Controls.Add(box);
        row.Controls.Add(browse);
        return row;
    }
}
