using System.Globalization;
using System.Net;
using System.Net.Http;
using System.Text;
using System.Text.Json;

namespace LuaDenCompanion;

/// <summary>
/// Independent client for Playden's paired LAN API.
/// The pairing cookie exists only in this process and is never persisted.
/// </summary>
public sealed class PlaydenClient : IDisposable
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true
    };

    private HttpClient? _http;
    private string? _baseAddress;

    public void Configure(string host, int port)
    {
        host = host.Trim();
        if (!IPAddress.TryParse(host, out var address) ||
            address.AddressFamily != System.Net.Sockets.AddressFamily.InterNetwork)
            throw new ArgumentException("Enter the PS5 LAN IPv4 address.");
        if (port is < 1 or > 65535)
            throw new ArgumentOutOfRangeException(nameof(port), "Port must be between 1 and 65535.");

        var baseAddress = $"http://{address}:{port}/";
        if (_http is not null && string.Equals(_baseAddress, baseAddress, StringComparison.OrdinalIgnoreCase))
            return;

        _http?.Dispose();
        var handler = new HttpClientHandler
        {
            CookieContainer = new CookieContainer(),
            UseCookies = true,
            UseProxy = false,
            AllowAutoRedirect = false
        };
        _http = new HttpClient(handler)
        {
            BaseAddress = new Uri(baseAddress, UriKind.Absolute),
            Timeout = TimeSpan.FromSeconds(15)
        };
        _baseAddress = baseAddress;
    }

    public async Task<bool> IsPairedAsync(CancellationToken cancellationToken = default)
    {
        var result = await SendJsonAsync<PairStatus>(HttpMethod.Get, "api/pair/status", false, cancellationToken);
        return result.Paired;
    }

    public async Task StartPairingAsync(CancellationToken cancellationToken = default)
    {
        _ = await SendJsonAsync<PairStart>(HttpMethod.Post, "api/pair/start", true, cancellationToken);
    }

    public async Task ConfirmPairingAsync(string code, CancellationToken cancellationToken = default)
    {
        var path = "api/pair/confirm?code=" + Uri.EscapeDataString(code);
        _ = await SendJsonAsync<PairConfirm>(HttpMethod.Post, path, true, cancellationToken);
    }

    public Task<GameCatalog> GetGamesAsync(CancellationToken cancellationToken = default) =>
        SendJsonAsync<GameCatalog>(HttpMethod.Get, "api/files/games", false, cancellationToken);

    public Task<GameDetails> GetGameAsync(string gameId, CancellationToken cancellationToken = default) =>
        SendJsonAsync<GameDetails>(
            HttpMethod.Get, "api/files/game?game=" + Uri.EscapeDataString(gameId), false, cancellationToken);

    public Task<FileListing> ListFilesAsync(
        string gameId, string rootId, string relativePath, CancellationToken cancellationToken = default)
    {
        var path = "api/files/list?game=" + Uri.EscapeDataString(gameId)
            + "&root=" + Uri.EscapeDataString(rootId)
            + "&path=" + Uri.EscapeDataString(relativePath);
        return SendJsonAsync<FileListing>(HttpMethod.Get, path, false, cancellationToken);
    }

    /// <summary>
    /// Imports only the manifest-pin portion of a LuaTools .lua profile.
    /// Playden does not execute Lua or apply unlocker/DRM-bypass directives.
    /// </summary>
    public async Task<int> ImportManifestProfileAsync(
        uint appId, string source, CancellationToken cancellationToken = default)
    {
        var bytes = Encoding.UTF8.GetBytes(source);
        if (bytes.Length == 0 || bytes.Length > 2 * 1024 * 1024)
            throw new InvalidOperationException("Profile must be non-empty and no larger than 2 MiB.");

        using var content = new ByteArrayContent(bytes);
        content.Headers.ContentType = new System.Net.Http.Headers.MediaTypeHeaderValue("text/plain")
        {
            CharSet = "utf-8"
        };

        var path = "api/files/lua-profile?game=" + appId.ToString(CultureInfo.InvariantCulture);
        var result = await SendJsonAsync<ProfileImportResult>(
            HttpMethod.Post, path, true, cancellationToken, content);
        return result.Pins;
    }

    public async Task DownloadFileAsync(
        string gameId, string rootId, string relativePath, string destination,
        CancellationToken cancellationToken = default)
    {
        var path = "api/files/download?game=" + Uri.EscapeDataString(gameId)
            + "&root=" + Uri.EscapeDataString(rootId)
            + "&path=" + Uri.EscapeDataString(relativePath);

        using var request = new HttpRequestMessage(HttpMethod.Get, path);
        using var response = await GetHttp().SendAsync(
            request, HttpCompletionOption.ResponseHeadersRead, cancellationToken);
        await EnsureSuccessAsync(response, cancellationToken);

        var temporary = destination + ".partial";
        try
        {
            await using (var output = new FileStream(
                temporary, FileMode.Create, FileAccess.Write, FileShare.None, 128 * 1024, useAsync: true))
            await using (var input = await response.Content.ReadAsStreamAsync(cancellationToken))
                await input.CopyToAsync(output, cancellationToken);
            File.Move(temporary, destination, overwrite: true);
        }
        catch
        {
            try { File.Delete(temporary); } catch { /* Preserve the original transfer error. */ }
            throw;
        }
    }

    private async Task<T> SendJsonAsync<T>(
        HttpMethod method, string path, bool requireAction,
        CancellationToken cancellationToken, HttpContent? content = null)
    {
        using var request = new HttpRequestMessage(method, path) { Content = content };
        if (requireAction)
            request.Headers.TryAddWithoutValidation("X-Playden-Action", "files");

        using var response = await GetHttp().SendAsync(
            request, HttpCompletionOption.ResponseHeadersRead, cancellationToken);
        await EnsureSuccessAsync(response, cancellationToken);
        var body = await response.Content.ReadAsStringAsync(cancellationToken);
        try
        {
            return JsonSerializer.Deserialize<T>(body, JsonOptions)
                ?? throw new JsonException("Empty JSON response.");
        }
        catch (JsonException ex)
        {
            throw new InvalidOperationException("Playden returned a response this companion could not read.", ex);
        }
    }

    private static async Task EnsureSuccessAsync(
        HttpResponseMessage response, CancellationToken cancellationToken)
    {
        if (response.IsSuccessStatusCode) return;
        var detail = (await response.Content.ReadAsStringAsync(cancellationToken)).Trim();
        var headline = response.StatusCode switch
        {
            HttpStatusCode.Unauthorized => "This PC is not paired. Pair it again.",
            HttpStatusCode.Forbidden => "Playden rejected the action. Check that the companion API is enabled.",
            HttpStatusCode.NotFound => "The selected game or file was not found.",
            HttpStatusCode.Conflict => "Playden cannot apply this operation in its current state.",
            HttpStatusCode.RequestEntityTooLarge => "The selected file is larger than Playden allows.",
            HttpStatusCode.TooManyRequests => "Pairing is temporarily rate-limited. Try again shortly.",
            HttpStatusCode.ServiceUnavailable => "Playden is busy setting up a game. Try again shortly.",
            _ => $"Playden returned HTTP {(int)response.StatusCode} ({response.StatusCode})."
        };
        throw new HttpRequestException(
            string.IsNullOrEmpty(detail) ? headline : $"{headline} {detail}",
            null, response.StatusCode);
    }

    private HttpClient GetHttp() =>
        _http ?? throw new InvalidOperationException("Connect to the PS5 first.");

    public void Dispose()
    {
        _http?.Dispose();
        _http = null;
        _baseAddress = null;
    }

    private sealed class PairStatus { public bool Paired { get; set; } }
    private sealed class PairStart { public bool Code_On_Tv { get; set; } }
    private sealed class PairConfirm { public bool Paired { get; set; } }
    private sealed class ProfileImportResult { public int Pins { get; set; } }
}
