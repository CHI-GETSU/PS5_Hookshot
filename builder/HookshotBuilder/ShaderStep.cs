using System.Text.RegularExpressions;

namespace HookshotBuilder;

/// The build's last step, the shaders' preparation (nx2/make_game.py prepare_shaders): the argument that leaves it to the game's
/// first start, and what its row says once the build is done
static class ShaderStep
{
    public const string Id = "shaders";
    public const string Later = "--no-prepare-shaders";
    // (make_game.py NOT_PREPARED: a plain line, so a builder that does not know it only shows it)
    static readonly Regex NotPrepared = new(@"^the shaders were not prepared now \((.*)\): ");

    /// make_game's arguments with the player's choice (unticked: the game prepares its shaders at its first start)
    public static List<string> Args(List<string> args, bool prepare)
    {
        if (!prepare) args.Add(Later);
        return args;
    }

    /// Why make_game left the shaders to the first start, when this line says so; else null
    public static string? Reason(string line) => NotPrepared.Match(line) is { Success: true } m ? m.Groups[1].Value : null;

    /// The line under the row of a finished build when the shaders were not prepared, or null when they were (a check mark).
    /// began: the game's @@stage shaders came; reason: make_game's not-prepared line
    public static string? Skipped(bool began, string? reason) =>
        reason != null ? $"Left to the game's first start: {reason}"
        : began ? null
        : "Left to the game's first start";
}
