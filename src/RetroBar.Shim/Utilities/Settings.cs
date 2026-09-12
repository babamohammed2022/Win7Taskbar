using System;
using System.ComponentModel;
using System.Globalization;
using System.IO;
using System.Runtime.CompilerServices;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace RetroBar.Utilities
{
    public sealed class Settings : INotifyPropertyChanged
    {
        private static readonly Lazy<Settings> LazyInstance = new Lazy<Settings>(Load, true);
        public static Settings Instance => LazyInstance.Value;
        public event PropertyChangedEventHandler? PropertyChanged;

        private bool _showClockSeconds;
        private bool _collapseNotifyIcons = true;
        private bool _showClock = true;
        private double _taskbarHeight = 40d;
        private bool _useNativeClockFlyout;
        private bool _aeroPeek = true;
        private string _language = "it";
        private bool _enableAppSearch = true;
        private int _networkFlyoutMode;
        private bool _useClassicVolumeMixer;
        private bool _useBatteryFlyout;

        public bool ShowClockSeconds { get => _showClockSeconds; set => SetField(ref _showClockSeconds, value); }
        public bool AeroPeek { get => _aeroPeek; set => SetField(ref _aeroPeek, value); }
        public bool CollapseNotifyIcons { get => _collapseNotifyIcons; set => SetField(ref _collapseNotifyIcons, value); }
        public bool ShowClock { get => _showClock; set => SetField(ref _showClock, value); }
        public double TaskbarHeight { get => _taskbarHeight; set => SetField(ref _taskbarHeight, value); }
        public bool UseNativeClockFlyout { get => _useNativeClockFlyout; set => SetField(ref _useNativeClockFlyout, value); }
        public bool EnableAppSearch { get => _enableAppSearch; set => SetField(ref _enableAppSearch, value); }

        internal static readonly string[] SupportedLanguages =
            { "it", "en", "es", "fr", "de", "pt", "pl", "ru", "ja", "zh", "ar" };

        public string Language
        {
            get => _language;
            set
            {
                string normalized = value != null && Array.IndexOf(SupportedLanguages, value) >= 0 ? value : "en";
                SetField(ref _language, normalized);
            }
        }

        [JsonIgnore]
        public bool IsEnglish => _language == "en";

        public int NetworkFlyoutMode { get => _networkFlyoutMode; set => SetField(ref _networkFlyoutMode, value == 1 ? 1 : 0); }
        public bool UseClassicVolumeMixer { get => _useClassicVolumeMixer; set => SetField(ref _useClassicVolumeMixer, value); }
        public bool UseBatteryFlyout { get => _useBatteryFlyout; set => SetField(ref _useBatteryFlyout, value); }
        public bool ClockFlyoutChoiceMigrated { get; set; }
        public bool ClockFlyoutNativeMigrated195 { get; set; }
        public bool LanguageMigrated { get; set; }

        [JsonIgnore]
        public static string ConfigPath => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Win7Taskbar", "settings.json");

        private static readonly JsonSerializerOptions SerializerOptions = new() { WriteIndented = true };

        private static Settings Load()
        {
            try
            {
                string path = ConfigPath;
                if (File.Exists(path))
                {
                    Settings? loaded = JsonSerializer.Deserialize<Settings>(File.ReadAllText(path), SerializerOptions);
                    if (loaded != null) return Migrate(loaded);
                }
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException) { }
            return Migrate(new Settings());
        }

        private static Settings Migrate(Settings settings)
        {
            bool changed = false;
            if (!settings.ClockFlyoutChoiceMigrated)
            {
                settings._useNativeClockFlyout = false;
                settings.ClockFlyoutChoiceMigrated = true;
                changed = true;
            }
            if (!settings.ClockFlyoutNativeMigrated195)
            {
                settings._useNativeClockFlyout = true;
                settings.ClockFlyoutNativeMigrated195 = true;
                changed = true;
            }
            if (!settings.LanguageMigrated)
            {
                try
                {
                    string systemLanguage = CultureInfo.CurrentUICulture.TwoLetterISOLanguageName.ToLowerInvariant();
                    settings._language = Array.IndexOf(SupportedLanguages, systemLanguage) >= 0 ? systemLanguage : "en";
                }
                catch { settings._language = "en"; }
                settings.LanguageMigrated = true;
                changed = true;
            }
            if (changed) settings.Save();
            return settings;
        }

        public void Save()
        {
            try
            {
                string path = ConfigPath;
                string? dir = Path.GetDirectoryName(path);
                if (!string.IsNullOrEmpty(dir)) Directory.CreateDirectory(dir);
                string temp = path + ".tmp";
                File.WriteAllText(temp, JsonSerializer.Serialize(this, SerializerOptions));
                File.Move(temp, path, true);
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { }
        }

        private void SetField<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
        {
            if (Equals(field, value)) return;
            field = value;
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
            Save();
        }
    }
}
