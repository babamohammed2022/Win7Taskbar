// Win7Taskbar - RetroBar compatibility shim
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under GPL v3 or later
// English: Public surface required by Windows7.xaml theme (derived from RetroBar, Apache 2.0)
// Italiano: Superficie pubblica minima richiesta dal tema Windows7.xaml (derivato da RetroBar, Apache 2.0). Codice scritto da zero.

using System;
using System.ComponentModel;
using System.Globalization;
using System.IO;
using System.Runtime.CompilerServices;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace RetroBar.Utilities
{
    /// <summary>
    /// Application settings persisted to JSON file.
    /// English: Theme Windows7.xaml accesses it via {x:Static retrosettings:Settings.Instance}
    /// Italiano: Impostazioni dell'applicazione, persistite su file JSON. Il tema vi accede con {x:Static retrosettings:Settings.Instance}
    /// </summary>
    public sealed class Settings : INotifyPropertyChanged
    {
        private static readonly Lazy<Settings> LazyInstance =
            new Lazy<Settings>(Load, isThreadSafe: true);

        /// <summary>Shared singleton instance used by XAML bindings / Istanza condivisa (singleton) usata dai binding XAML.</summary>
        public static Settings Instance => LazyInstance.Value;

        public event PropertyChangedEventHandler? PropertyChanged;

        private bool _showClockSeconds;
        private bool _collapseNotifyIcons = true;
        private bool _showClock = true;
        private double _taskbarHeight = 40d;
        // English: Real Windows flyout is default, recreated is fallback
        // Italiano: Il riquadro vero di Windows e' la scelta predefinita: quello ricreato serve come alternativa
        private bool _useNativeClockFlyout = false;
        // v2.47: anteprima del desktop (Aero Peek) attiva. E' la casella
        // "Anteprima del desktop con Aero Peek" della finestra Proprieta'.
        private bool _aeroPeek = true;
        // Never Italian by omission: until the language is detected (or chosen)
        // the safe value is English, the declared fallback of the project.
        private string _language = DefaultLanguageCode;
        // v3.0: optional app search / ricerca app opzionale.
        // v3.3: ON by default (lente a sinistra dello Start durante
        // l'esecuzione); si disattiva dalle Proprieta'.
        private bool _enableAppSearch = true;

        /// <summary>
        /// Show seconds in clock, persisted to file / Mostra i secondi nell'orologio, persistita su file.
        /// </summary>
        public bool ShowClockSeconds
        {
            get => _showClockSeconds;
            set => SetField(ref _showClockSeconds, value);
        }

        /// <summary>
        /// v2.47: Aero Peek / anteprima del desktop.
        /// Inglese: true = passando il mouse sul pulsante "Mostra desktop" le
        /// finestre diventano trasparenti e si vede il desktop.
        /// Italiano: true = col puntatore sul pulsante "Mostra desktop" alla
        /// fine della barra le finestre diventano trasparenti e si vede il
        /// desktop; false = il pulsante minimizza e basta, come chiesto nella
        /// finestra Proprieta'.
        /// </summary>
        public bool AeroPeek
        {
            get => _aeroPeek;
            set => SetField(ref _aeroPeek, value);
        }

        /// <summary>
        /// When true unpinned icons go to overflow and theme shows chevron / Quando true le icone non ancorate finiscono nell'overflow
        /// </summary>
        public bool CollapseNotifyIcons
        {
            get => _collapseNotifyIcons;
            set => SetField(ref _collapseNotifyIcons, value);
        }

        public bool ShowClock
        {
            get => _showClock;
            set => SetField(ref _showClock, value);
        }

        public double TaskbarHeight
        {
            get => _taskbarHeight;
            set => SetField(ref _taskbarHeight, value);
        }

        /// <summary>
        /// Clock flyout choice: true = native Windows immersive flyout, false = WPF recreated / Scelta calendario: true = nativo, false = ricreato
        /// </summary>
        public bool UseNativeClockFlyout
        {
            get => _useNativeClockFlyout;
            set => SetField(ref _useNativeClockFlyout, value);
        }

        /// <summary>
        /// v3.0: optional app-search panel (Properties window enables it).
        /// Italiano: pannello ricerca app opzionale (si attiva da Proprietà).
        /// </summary>
        public bool EnableAppSearch
        {
            get => _enableAppSearch;
            set => SetField(ref _enableAppSearch, value);
        }

        /// <summary>
        /// Language used when the system does not speak one of the supported
        /// languages and the user has not chosen yet: English, never Italian.
        /// Italiano: lingua usata quando il sistema non parla una delle lingue
        /// supportate e l'utente non ha ancora scelto: l'inglese, mai l'italiano.
        /// </summary>
        public const string DefaultLanguageCode = "en";

        /// <summary>
        /// The eleven supported language codes, in the order the native core
        /// indexes them (Strings.h: 0=it ... 10=ar). Public because the language
        /// dictionary loader normalizes against this single list.
        /// Codici delle undici lingue supportate, nell'ordine con cui il core
        /// nativo le indicizza (Strings.h: 0=it ... 10=ar).
        /// </summary>
        public static readonly string[] SupportedLanguages =
            { "it", "en", "es", "fr", "de", "pt", "pl", "ru", "ja", "zh", "ar" };

        /// <summary>
        /// Language of the Windows interface, filtered against the supported
        /// ones: a two-letter code, or <see cref="DefaultLanguageCode"/> when
        /// Windows speaks a language this project does not translate. This is
        /// the value of the first run: Italian is NOT the fallback.
        /// Italiano: lingua dell'interfaccia di Windows filtrata sulle lingue
        /// supportate; ripiego l'inglese. L'italiano non e' il ripiego.
        /// </summary>
        public static string DetectSystemLanguage()
        {
            try
            {
                string systemLanguage = CultureInfo.CurrentUICulture.TwoLetterISOLanguageName
                    .ToLowerInvariant();
                return Array.IndexOf(SupportedLanguages, systemLanguage) >= 0
                    ? systemLanguage
                    : DefaultLanguageCode;
            }
            catch
            {
                return DefaultLanguageCode;
            }
        }

        /// <summary>
        /// Language selection (two-letter code) / Selezione lingua (codice a due lettere)
        /// </summary>
        public string Language
        {
            get => _language;
            set
            {
                string normalized = (value != null &&
                    Array.IndexOf(SupportedLanguages, value) >= 0) ? value : DefaultLanguageCode;
                SetField(ref _language, normalized);
            }
        }

        /// <summary>Helper to check if English / Helper per verificare se inglese</summary>
        [JsonIgnore]
        public bool IsEnglish => _language == "en";

        private int _networkFlyoutMode;

        /// <summary>
        /// v2.36: flyout di rete. 0 = "Windows 7 (ricreato)" (default),
        /// 1 = "Windows 10/11" (flyout moderno nativo). Persistente e
        /// mutualmente esclusivo. L'icona di rete resta sempre quella
        /// originale di Windows: cambia solo il flyout aperto al click.
        /// </summary>
        public int NetworkFlyoutMode
        {
            get => _networkFlyoutMode;
            set => SetField(ref _networkFlyoutMode, value == 1 ? 1 : 0);
        }

        private bool _useClassicVolumeMixer;
        private bool _useBatteryFlyout;

        /// <summary>
        /// v2.38: classic volume mixer (SndVol.exe) instead of the modern
        /// flyout. Default OFF; when on, a left-click on the volume tray icon
        /// launches SndVol.exe -f anchored to the icon. Silent fallback to
        /// the modern flyout if the process cannot start.
        /// Italiano: mixer volume classico (SndVol.exe) al posto del flyout
        /// moderno. Spento di default; se attivo, il click sinistro sull'icona
        /// volume avvia SndVol.exe -f ancorato all'icona. Ripiego silenzioso
        /// al flyout moderno se il processo non parte.
        /// </summary>
        public bool UseClassicVolumeMixer
        {
            get => _useClassicVolumeMixer;
            set => SetField(ref _useClassicVolumeMixer, value);
        }

        /// <summary>
        /// v2.38: recreated Windows 7-style battery flyout instead of the
        /// native one. Default OFF (opt-in from Properties).
        /// Italiano: flyout batteria ricreato in stile Windows 7 al posto di
        /// quello nativo. Spento di default (si attiva dalle Proprietà).
        /// </summary>
        public bool UseBatteryFlyout
        {
            get => _useBatteryFlyout;
            set => SetField(ref _useBatteryFlyout, value);
        }

        /// <summary>One-time migration flags / Flag migrazione una tantum</summary>
        public bool ClockFlyoutChoiceMigrated { get; set; }

        public bool ClockFlyoutNativeMigrated195 { get; set; }

        public bool LanguageMigrated { get; set; }

        /// <summary>Config file path / Percorso file configurazione.</summary>
        [JsonIgnore]
        public static string ConfigPath
        {
            get
            {
                string dir = Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                    "Win7Taskbar");
                return Path.Combine(dir, "settings.json");
            }
        }

        /// <summary>
        /// True when a configuration file already exists: it is what tells the
        /// first run (system language) from a run where the user already chose,
        /// or already accepted the detected language.
        /// Italiano: true se esiste gia' una configurazione salvata: distingue
        /// il primo avvio da un avvio in cui la scelta c'e' gia'.
        /// </summary>
        [JsonIgnore]
        public static bool HasPersistedConfig => File.Exists(ConfigPath);

        private static readonly JsonSerializerOptions SerializerOptions = new()
        {
            WriteIndented = true
        };

        private static Settings Load()
        {
            try
            {
                string path = ConfigPath;
                if (File.Exists(path))
                {
                    string json = File.ReadAllText(path);
                    Settings? loaded = JsonSerializer.Deserialize<Settings>(json, SerializerOptions);
                    if (loaded != null)
                    {
                        return Migrate(loaded);
                    }
                }
            }
            catch (Exception ex) when (ex is IOException
                                        or UnauthorizedAccessException
                                        or JsonException)
            {
                // Corrupted or unreadable config: start from defaults / Config corrotta o illeggibile: si riparte dai default.
            }

            return Migrate(new Settings());
        }

        /// <summary>
        /// One-time migrations between versions / Migrazioni una tantum fra versioni.
        /// </summary>
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
                // First run (or a configuration that never stored a choice):
                // the language of Windows when it is one of the eleven, English
                // otherwise. An explicit choice in Properties always wins and
                // does not pass through here.
                // Italiano: primo avvio (o configurazione che non diceva nulla):
                // lingua di Windows se e' una delle undici, altrimenti inglese.
                settings._language = DetectSystemLanguage();
                settings.LanguageMigrated = true;
                changed = true;
            }

            if (changed)
            {
                settings.Save();
            }
            return settings;
        }

        /// <summary>Writes settings atomically to disk / Scrive le impostazioni su disco in modo atomico.</summary>
        public void Save()
        {
            try
            {
                string path = ConfigPath;
                string? dir = Path.GetDirectoryName(path);
                if (!string.IsNullOrEmpty(dir))
                {
                    Directory.CreateDirectory(dir);
                }

                string json = JsonSerializer.Serialize(this, SerializerOptions);

                // Write to temp + move: avoids half-written file / Scrittura su temp + move
                string temp = path + ".tmp";
                File.WriteAllText(temp, json);
                File.Move(temp, path, overwrite: true);
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            {
                // Persistence must never crash UI / La persistenza non deve mai far cadere la UI.
            }
        }

        private void SetField<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
        {
            if (Equals(field, value))
            {
                return;
            }
            field = value;
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
            Save();
        }
    }
}
