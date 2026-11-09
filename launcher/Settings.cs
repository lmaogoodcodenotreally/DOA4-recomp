using System.Globalization;
using System.Text;
using System.Text.Json;

namespace Doa4Launcher;

enum Kind { Bool, Int, Double, String }

// One game cvar the launcher manages. Value strings are stored in TOML form
// without quotes ("true", "2", "fxaa"); Kind decides how they are written.
sealed record Setting(string Key, Kind Kind, string Default, string Label, string Tip,
                      (string Value, string Text)[]? Choices = null);

static class Settings
{
    // vsync and video_mode_refresh_rate are deliberately absent: the game
    // simulates one fixed step per flip, so changing them changes game speed
    // (known_issues R51/R52).
    public static readonly Setting[] Display =
    {
        new("fullscreen", Kind.Bool, "true", "Fullscreen", "Start in fullscreen (borderless)."),
        new("monitor", Kind.Int, "0", "Monitor", "Which monitor to use.",
            new[] { ("0", "Default"), ("1", "Monitor 1"), ("2", "Monitor 2"), ("3", "Monitor 3") }),
        new("window_width", Kind.Int, "0", "Window width", "Windowed mode only; 0 = automatic."),
        new("window_height", Kind.Int, "0", "Window height", "Windowed mode only; 0 = automatic."),
        new("aspect_ratio", Kind.Double, "-1", "Aspect ratio",
            "Shape of your screen. Wider than 16:9 shows more of the stage at the sides; "
            + "anything but 16:9 turns off the 16:9 letterbox. Auto matches the game window.",
            new[] { ("-1", "Auto (match screen)"), ("0", "16:9 (original)"), ("1.333333", "4:3"), ("1.6", "16:10"),
                    ("2.370370", "21:9 (2560x1080)"), ("2.388889", "21:9 (3440x1440)"),
                    ("2.4", "21:9 (3840x1600)"), ("3.555556", "32:9") }),
        new("present_letterbox", Kind.Bool, "true", "Keep 16:9 (letterbox)",
            "Black bars on screens that are not 16:9 instead of stretching."),
        new("frame_interp", Kind.Bool, "false", "Frame interpolation (experimental)",
            "Shows an extra in-between frame per game frame (about 120 fps) for smoother motion. "
            + "The game itself still runs at 60 Hz, so speed and timing are unchanged. Best on a 120 Hz+ "
            + "screen; may show occasional artefacts and costs GPU time on heavy stages."),
        // Not the game's `vsync` (its clock): this only picks how finished frames reach the screen.
        new("d3d12_allow_variable_refresh_rate_and_tearing", Kind.Bool, "false", "Screen sync",
            "V-Sync shows the newest finished frame at each screen refresh: no tearing, and it never "
            + "waits, so game speed is unchanged. Off shows frames immediately (tearing); only worth it "
            + "on a FreeSync / G-Sync screen.",
            new[] { ("false", "V-Sync (no tearing)"), ("true", "Off (tearing allowed)") }),
    };

    public static readonly Setting[] Graphics =
    {
        new("resolution_scale", Kind.Int, "1", "Internal resolution",
            "Renders the game above its native 1280x720. Higher = sharper, heavier on the GPU.",
            new[] { ("1", "1x  (1280x720, native)"), ("2", "2x  (2560x1440)"), ("3", "3x  (3840x2160)"),
                    ("4", "4x  (5120x2880)") }),
        new("scaled_point_filter_linear", Kind.Bool, "true", "Smooth 2D art",
            "Above 1x, smooths menu/HUD art the game draws pixel-exact, so it is not blown up into blocks."),
        new("swap_post_effect", Kind.String, "none", "Anti-aliasing (post)",
            "FXAA smooths edges after rendering.",
            new[] { ("none", "Off"), ("fxaa", "FXAA"), ("fxaa_extreme", "FXAA (extreme)") }),
        new("native_2x_msaa", Kind.Bool, "true", "Native 2x MSAA",
            "Uses the GPU's own 2x MSAA when the game asks for it."),
        new("anisotropic_override", Kind.Int, "3", "Texture filtering",
            "Sharper textures at angles.",
            new[] { ("-1", "Game default"), ("0", "Off"), ("1", "1x"), ("2", "2x"), ("3", "4x"),
                    ("4", "8x"), ("5", "16x") }),
        new("present_effect", Kind.String, "cas", "Output scaling",
            "How the image is scaled to your screen. CAS gives the cleanest result when the internal "
            + "resolution is above your screen's. FSR/CAS need a FidelityFX build.",
            new[] { ("bilinear", "Bilinear"), ("cas", "AMD CAS (recommended)"), ("fsr", "AMD FSR 1"),
                    ("fsr2", "AMD FSR 2"), ("fsr3", "AMD FSR 3") }),
    };

    public static readonly Setting[] Input =
    {
        new("input_backend", Kind.String, "sdl", "Controller backend",
            "SDL supports most pads; XInput is Xbox pads only.",
            new[] { ("sdl", "SDL (recommended)"), ("xinput", "XInput") }),
        new("mnk_mode", Kind.Bool, "true", "Keyboard as controller",
            "Lets the keyboard drive the game alongside a pad."),
        new("hid_sdl_directinput", Kind.Bool, "false", "Legacy DirectInput pads",
            "Only for old DirectInput-only controllers. Causes a short hitch when pads connect."),
        new("audio_mute", Kind.Bool, "false", "Mute audio", "Silence all game audio."),
        new("skip_intro", Kind.Bool, "true", "Skip intro",
            "Skip the warning screen and the intro movie; boot straight to PRESS START."),
    };

    public static IEnumerable<Setting> All => Display.Concat(Graphics).Concat(Input);

    public static string ToToml(Setting s, string value) => s.Kind switch
    {
        Kind.String => $"\"{value}\"",
        Kind.Double => double.Parse(value, CultureInfo.InvariantCulture)
                           .ToString("F6", CultureInfo.InvariantCulture),
        _ => value,
    };
}

// The launcher's own state, saved as launcher.json beside the exe.
sealed class LauncherConfig
{
    public string LeanExe { get; set; } = "";
    public string DevExe { get; set; } = "";
    public string DataRoot { get; set; } = "";
    public bool UseDev { get; set; }
    public Dictionary<string, string> Game { get; set; } = new();

    static string FilePath => Path.Combine(AppContext.BaseDirectory, "launcher.json");
    static readonly JsonSerializerOptions Json = new() { WriteIndented = true };

    public static LauncherConfig Load()
    {
        try
        {
            if (File.Exists(FilePath))
                return JsonSerializer.Deserialize<LauncherConfig>(File.ReadAllText(FilePath)) ?? new();
        }
        catch (Exception) { } // unreadable config: start fresh rather than refuse to open
        return new();
    }

    public void Save() => File.WriteAllText(FilePath, JsonSerializer.Serialize(this, Json));
}

static class Toml
{
    // Reads `key = value` lines (quotes stripped). The game's file is flat.
    public static Dictionary<string, string> Read(string path)
    {
        var values = new Dictionary<string, string>();
        if (!File.Exists(path)) return values;
        foreach (var line in File.ReadAllLines(path))
        {
            var t = line.Trim();
            int eq = t.IndexOf('=');
            if (t.StartsWith('#') || eq <= 0) continue;
            values[t[..eq].Trim()] = t[(eq + 1)..].Trim().Trim('"');
        }
        return values;
    }

    // Replaces managed keys in place, appends missing ones, keeps every other
    // line (comments and keys the launcher does not manage) untouched.
    public static string Merge(string existing, IReadOnlyDictionary<string, string> managed)
    {
        var pending = new Dictionary<string, string>(managed);
        var sb = new StringBuilder();
        foreach (var line in existing.Split('\n'))
        {
            var t = line.TrimEnd('\r');
            int eq = t.IndexOf('=');
            if (eq > 0 && !t.TrimStart().StartsWith('#'))
            {
                var key = t[..eq].Trim();
                if (pending.Remove(key, out var v)) { sb.Append(key).Append(" = ").Append(v).Append('\n'); continue; }
            }
            if (t.Length > 0) sb.Append(t).Append('\n');
        }
        foreach (var (k, v) in pending) sb.Append(k).Append(" = ").Append(v).Append('\n');
        return sb.ToString();
    }
}
