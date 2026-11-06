using Microsoft.Win32;
using System.Collections.ObjectModel;
using System.Globalization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;

namespace LuaDenCompanion;

public partial class MainWindow : Window
{
    private readonly PlaydenClient _client = new();
    private readonly ObservableCollection<GameInfo> _games = [];
    private readonly ObservableCollection<GameRoot> _roots = [];
    private readonly ObservableCollection<FileEntry> _entries = [];
    private GameInfo? _selectedGame;
    private GameRoot? _selectedRoot;
    private string _currentPath = "";

    public MainWindow()
    {
        InitializeComponent();
        GamesList.ItemsSource = _games;
        RootsList.ItemsSource = _roots;
        FilesList.ItemsSource = _entries;
    }

    private async void Connect_Click(object sender, RoutedEventArgs e)
    {
        await RunUiAsync(async () =>
        {
            ConfigureClient();
            if (await _client.IsPairedAsync())
            {
                StatusText.Text = "Paired. Loading games…";
                await LoadGamesAsync();
            }
            else
            {
                ClearCatalog();
                StatusText.Text = "Connected, but this PC is not paired. Start pairing on the PS5, then enter its six-digit code.";
            }
        });
    }

    private async void StartPairing_Click(object sender, RoutedEventArgs e)
    {
        await RunUiAsync(async () =>
        {
            ConfigureClient();
            await _client.StartPairingAsync();
            StatusText.Text = "Playden was asked to display a pairing code. Enter the six digits shown on the PS5.";
        });
    }

    private async void Pair_Click(object sender, RoutedEventArgs e)
    {
        await RunUiAsync(async () =>
        {
            ConfigureClient();
            var code = PairCodeBox.Text.Trim();
            if (code.Length != 6 || code.Any(c => c is < '0' or > '9'))
                throw new InvalidOperationException("Enter the six-digit code shown on the PS5.");

            await _client.ConfirmPairingAsync(code);
            if (!await _client.IsPairedAsync())
                throw new InvalidOperationException("Playden did not confirm the pairing session.");

            PairCodeBox.Clear();
            await LoadGamesAsync();
        });
    }

    private async void RefreshGames_Click(object sender, RoutedEventArgs e) =>
        await RunUiAsync(LoadGamesAsync);

    private async void GamesList_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (GamesList.SelectedItem is not GameInfo game)
        {
            _selectedGame = null;
            _roots.Clear();
            ClearFiles();
            return;
        }

        _selectedGame = game;
        _selectedRoot = null;
        _roots.Clear();
        ClearFiles();
        await RunUiAsync(async () =>
        {
            var details = await _client.GetGameAsync(game.Id);
            foreach (var root in details.Roots)
                _roots.Add(root);
            StatusText.Text = $"Selected {game.Name}. Choose an available folder.";
        });
    }

    private async void OpenRoot_Click(object sender, RoutedEventArgs e)
    {
        if (_selectedGame is null || RootsList.SelectedItem is not GameRoot root || !root.Exists)
        {
            StatusText.Text = "Select a game and an available folder first.";
            return;
        }

        _selectedRoot = root;
        _currentPath = "";
        await RunUiAsync(() => LoadFilesAsync(_currentPath));
    }

    private async void FilesList_MouseDoubleClick(object sender, MouseButtonEventArgs e)
    {
        if (FilesList.SelectedItem is not FileEntry entry || !entry.Directory)
            return;

        _currentPath = string.IsNullOrEmpty(_currentPath)
            ? entry.Name
            : _currentPath + "/" + entry.Name;
        await RunUiAsync(() => LoadFilesAsync(_currentPath));
    }

    private async void Up_Click(object sender, RoutedEventArgs e)
    {
        if (string.IsNullOrEmpty(_currentPath) || _selectedGame is null || _selectedRoot is null)
            return;

        var separator = _currentPath.LastIndexOf('/');
        _currentPath = separator < 0 ? "" : _currentPath[..separator];
        await RunUiAsync(() => LoadFilesAsync(_currentPath));
    }

    private async void ImportProfile_Click(object sender, RoutedEventArgs e)
    {
        if (_selectedGame is null || _selectedGame.Kind != "steam" || _selectedGame.App == 0)
        {
            StatusText.Text = "Select a Steam game before importing a profile.";
            return;
        }

        var dialog = new OpenFileDialog
        {
            Title = "Select LuaTools profile",
            Filter = "Lua profile (*.lua)|*.lua|All files (*.*)|*.*",
            CheckFileExists = true,
            Multiselect = false
        };
        if (dialog.ShowDialog(this) != true)
            return;

        await RunUiAsync(async () =>
        {
            var info = new FileInfo(dialog.FileName);
            if (info.Length is <= 0 or > 2 * 1024 * 1024)
                throw new InvalidOperationException("The profile must be non-empty and no larger than 2 MiB.");

            var source = await File.ReadAllTextAsync(dialog.FileName);
            var pins = await _client.ImportManifestProfileAsync(_selectedGame.App, source);
            StatusText.Text = $"Imported {pins} manifest pin(s) for {_selectedGame.Name}. Other Lua statements are not executed or applied by Playden.";
        });
    }

    private async void Download_Click(object sender, RoutedEventArgs e)
    {
        if (_selectedGame is null || _selectedRoot is null ||
            FilesList.SelectedItem is not FileEntry entry || entry.Directory)
        {
            StatusText.Text = "Select a file to download.";
            return;
        }

        var relativePath = string.IsNullOrEmpty(_currentPath)
            ? entry.Name
            : _currentPath + "/" + entry.Name;
        var dialog = new SaveFileDialog
        {
            FileName = entry.Name,
            Title = "Save file from Playden"
        };
        if (dialog.ShowDialog(this) != true)
            return;

        await RunUiAsync(async () =>
        {
            await _client.DownloadFileAsync(_selectedGame.Id, _selectedRoot.Id, relativePath, dialog.FileName);
            StatusText.Text = $"Downloaded {entry.Name}.";
        });
    }

    private void ConfigureClient()
    {
        if (!int.TryParse(PortBox.Text.Trim(), NumberStyles.None, CultureInfo.InvariantCulture, out var port))
            throw new InvalidOperationException("Enter a valid port number.");
        _client.Configure(HostBox.Text, port);
    }

    private async Task LoadGamesAsync()
    {
        var catalog = await _client.GetGamesAsync();
        _games.Clear();
        foreach (var game in catalog.Games)
            _games.Add(game);

        _roots.Clear();
        ClearFiles();
        _selectedGame = null;
        _selectedRoot = null;

        StatusText.Text = !catalog.Known
            ? "Playden's library is not available yet."
            : catalog.SettingUp
                ? $"Playden is setting up a game. {catalog.Games.Count} game(s) are listed."
                : $"Loaded {catalog.Games.Count} game(s).";
    }

    private async Task LoadFilesAsync(string path)
    {
        if (_selectedGame is null || _selectedRoot is null)
            return;

        var listing = await _client.ListFilesAsync(_selectedGame.Id, _selectedRoot.Id, path);
        _entries.Clear();
        foreach (var entry in listing.Entries)
            _entries.Add(entry);

        PathLabel.Text = "/" + path;
        var truncated = listing.Truncated ? $" ({listing.Unshown} more not shown)" : "";
        StatusText.Text = (listing.ReadOnly ? "Read-only view. " : "") +
            $"{_entries.Count} item(s) in {_selectedRoot.Id}/{path}{truncated}";
    }

    private void ClearCatalog()
    {
        _games.Clear();
        _roots.Clear();
        _selectedGame = null;
        _selectedRoot = null;
        ClearFiles();
    }

    private void ClearFiles()
    {
        _entries.Clear();
        _currentPath = "";
        PathLabel.Text = "/";
    }

    private async Task RunUiAsync(Func<Task> operation)
    {
        SetBusy(true);
        try
        {
            await operation();
        }
        catch (Exception ex)
        {
            StatusText.Text = ex.Message;
        }
        finally
        {
            SetBusy(false);
        }
    }

    private void SetBusy(bool busy)
    {
        Mouse.OverrideCursor = busy ? Cursors.Wait : null;
        IsEnabled = !busy;
    }

    protected override void OnClosed(EventArgs e)
    {
        _client.Dispose();
        base.OnClosed(e);
    }
}
