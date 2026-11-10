using HookshotBuilder;

sealed class Tools;
static class Storage { public static string NgxSdk = ""; }
sealed class Step { public void Done() { } public void Small(string value) { } }

// Only the UI and the build launcher are spies: UpdateBuild, settings and shader arguments are production code.
sealed class Window
{
    object? download = new();
    readonly Dictionary<string, Step> rows = new() { ["download"] = new Step() };
    public static string SettingsPath = "";
    public bool prepareNow = true;
    public bool? Dlss;
    public List<string>? Args;
    void Run(Tools tools, List<string> args, bool keepRows, bool dlss)
    {
        if (!keepRows || download != null) throw new Exception("Update setup changed.");
        Dlss = dlss;
        Args = args;
    }
    /* ACTUAL_UPDATE */
}

static class Program
{
    static int checks;
    static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
        checks++;
    }

    static void Update(string game, bool expected, bool shaders = true)
    {
        var w = new Window { prepareNow = shaders };
        w.UpdateBuild(new Tools(), game);
        Check(w.Dlss == expected, "Wrong DLSS decision for the saved choice and existing installation.");
        var args = new List<string> { "--update", game };
        if (!shaders) args.Add("--no-prepare-shaders");
        Check(w.Args!.SequenceEqual(args), "The DLSS choice changed the update/shader arguments.");
    }

    static void Main()
    {
        // Authored placeholders only: no NVIDIA SDK, runtime or game data is used.
        var root = Path.Combine(Path.GetTempPath(), "hookshot-dlss-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            Window.SettingsPath = Path.Combine(root, "builder-settings.json");
            Storage.NgxSdk = Path.Combine(root, "sdk without files");
            var game = Path.Combine(root, "game with spaces é");
            Directory.CreateDirectory(game);
            // Simulates the saved, license-accepted checkbox before a self-update creates a new window.
            File.WriteAllText(Window.SettingsPath, "{\"Dlss\":true}");
            Update(game, true);
            Update(game, true, shaders: false);
            Check(!Directory.Exists(Storage.NgxSdk), "The decision unexpectedly provisioned an SDK.");
            Check(!Directory.Exists(Path.Combine(game, "ngx")), "The decision unexpectedly modified the game.");
            File.WriteAllText(Window.SettingsPath, "{\"Dlss\":false}");
            Update(game, false);
            File.WriteAllText(Window.SettingsPath, "{}");
            Update(game, false);
            File.WriteAllText(Window.SettingsPath, "not json");
            Update(game, false);
            File.Delete(Window.SettingsPath);
            Update(game, false);
            Directory.CreateDirectory(Storage.NgxSdk);
            File.WriteAllText(Path.Combine(Storage.NgxSdk, "LICENSE.txt"), "authored license placeholder");
            Update(game, false); // A license alone did not build this game's DLSS.
            Directory.CreateDirectory(Path.Combine(game, "ngx"));
            var runtime = Path.Combine(game, "ngx", "nvngx_dlss.dll");
            File.WriteAllText(runtime, "authored runtime placeholder");
            Update(game, true); // Preserve the existing update behavior for an opted-in installation.
            File.WriteAllText(Window.SettingsPath, "{\"Dlss\":false}");
            Update(game, true);
            File.Delete(Path.Combine(Storage.NgxSdk, "LICENSE.txt"));
            Update(game, false); // A leftover DLL alone does not accept NVIDIA's license.
            File.WriteAllText(Window.SettingsPath, "{\"Dlss\":true}");
            Update(game, true); // The accepted preference also repairs a partial SDK.
            Check(File.ReadAllText(runtime) == "authored runtime placeholder", "Update decision modified the existing runtime.");
            Console.WriteLine($"Builder DLSS update checks passed ({checks} assertions).");
        }
        finally { Directory.Delete(root, recursive: true); }
    }
}
