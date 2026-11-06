using System.Text.Json.Serialization;

namespace LuaDenCompanion;

public sealed class GameCatalog
{
    [JsonPropertyName("known")] public bool Known { get; set; }
    [JsonPropertyName("setting_up")] public bool SettingUp { get; set; }
    [JsonPropertyName("games")] public List<GameInfo> Games { get; set; } = [];
}

public sealed class GameInfo
{
    [JsonPropertyName("id")] public string Id { get; set; } = "";
    [JsonPropertyName("name")] public string Name { get; set; } = "";
    [JsonPropertyName("kind")] public string Kind { get; set; } = "";
    [JsonPropertyName("app")] public uint App { get; set; }
    [JsonPropertyName("drive")] public string Drive { get; set; } = "";
    [JsonPropertyName("installed")] public bool Installed { get; set; }
    [JsonPropertyName("running")] public bool Running { get; set; }
    [JsonPropertyName("setting_up")] public bool SettingUp { get; set; }
    public string Display => $"{Name}  [{Kind}]  App {App}";
}

public sealed class GameDetails
{
    [JsonPropertyName("id")] public string Id { get; set; } = "";
    [JsonPropertyName("name")] public string Name { get; set; } = "";
    [JsonPropertyName("roots")] public List<GameRoot> Roots { get; set; } = [];
}

public sealed class GameRoot
{
    [JsonPropertyName("id")] public string Id { get; set; } = "";
    [JsonPropertyName("exists")] public bool Exists { get; set; }
    public string Display => Exists ? Id : $"{Id} (not present)";
}

public sealed class FileListing
{
    [JsonPropertyName("exists")] public bool Exists { get; set; }
    [JsonPropertyName("read_only")] public bool ReadOnly { get; set; }
    [JsonPropertyName("truncated")] public bool Truncated { get; set; }
    [JsonPropertyName("unshown")] public ulong Unshown { get; set; }
    [JsonPropertyName("entries")] public List<FileEntry> Entries { get; set; } = [];
}

public sealed class FileEntry
{
    [JsonPropertyName("name")] public string Name { get; set; } = "";
    [JsonPropertyName("directory")] public bool Directory { get; set; }
    [JsonPropertyName("size")] public ulong Size { get; set; }
    [JsonPropertyName("modified")] public long Modified { get; set; }
    [JsonPropertyName("read_only")] public bool ReadOnly { get; set; }
    [JsonPropertyName("recoverable")] public bool Recoverable { get; set; }
    public string TypeLabel => Directory ? "Folder" : "File";
    public string SizeLabel => Directory ? "" : $"{Size:N0} bytes";
}
