// Win7Taskbar - RetroBar compatibility shim
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under GPL v3 or later
// English: Public surface required by Windows7.xaml theme (derived from RetroBar, Apache 2.0)
// Italiano: Superficie pubblica minima richiesta dal tema Windows7.xaml (derivato da RetroBar, Apache 2.0). Codice scritto da zero.

using System;
using System.Collections.Generic;
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
        // Il flyout dell'orologio segue sempre il comportamento Windows 7:
        // la vecchia scelta ricreato/nativo viene mantenuta solo per leggere
        // configurazioni esistenti e non e' piu' modificabile dalla UI.
        private bool _useNativeClockFlyout = true;
        // v2.47: anteprima del desktop (Aero Peek) attiva. E' la casella
        // "Anteprima del desktop con Aero Peek" della finestra Proprieta'.
        private bool _aeroPeek = true;
        // v2.62-alpha (G3/G4): read and honour the user's own taskbar
        // grouping configuration (TaskbarGlomLevel / exceptions / group
        // icon criterion). OFF (default): exactly the current behaviour.
        private bool _taskbarGroupingPolicy;
        // v2.62-alpha (G6): pin/unpin through the canonical native verb
        // (the single .lnk write point shared with the Jump List). OFF
        // (default): the historical managed path.
        private bool _canonicalPinVerbs;
        // Never Italian by omission: until the language is detected (or chosen)
        // the safe value is English, the declared fallback of the project.
        private string _language = DefaultLanguageCode;
        // v3.0: optional app search / ricerca app opzionale.
        // v3.3: ON by default (lente a sinistra dello Start durante
        // l'esecuzione); si disattiva dalle Proprieta'.
        // v3.15: ricerca applicazioni ORA disattivata per impostazione
        // predefinita (il motore di ricerca si attiva una sola volta dalle
        // proprieta' della barra, quando serve): su richiesta dell'utente
        // la scansione non deve partire all'avvio.
        private bool _enableAppSearch = false;
        private bool _showJumpListHoverArrow = false;
        private bool _showControlCenterButton;
        private bool _showNotificationCenterButton;
        // Windows 11 only: 0 automatic, 1 modern System32, 2 legacy SysWOW64.
        private int _taskManagerMode;
        // Single persisted kill switch for the delayed BitBlt thumbnail
        // fallback. Keep it on by default; setting JSON to false restores the
        // pure RetroBar/DWM path without maintaining two separate policies.
        private bool _useThumbnailCaptureFallback = true;

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

        /// <summary>v2.62-alpha (G3/G4): honours the user's own taskbar
        /// grouping configuration. Off by default (current behaviour).</summary>
        public bool TaskbarGroupingPolicy
        {
            get => _taskbarGroupingPolicy;
            set => SetField(ref _taskbarGroupingPolicy, value);
        }

        /// <summary>v2.62-alpha (G6): canonical pin/unpin verb path.
        /// Off by default (the historical managed path).</summary>
        public bool CanonicalPinVerbs
        {
            get => _canonicalPinVerbs;
            set => SetField(ref _canonicalPinVerbs, value);
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

        /// <summary>
        /// Kept for settings.json compatibility. Thickness is the theme
        /// TaskbarHeight resource; user-controlled resizing was removed.
        /// </summary>
        public double TaskbarHeight
        {
            get => _taskbarHeight;
            set => SetField(ref _taskbarHeight, value);
        }

        // v1.21.43 / v1.3.40 - posizione della barra + blocco (RetroBar:
        // Settings.Edge + Settings.LockTaskbar). TaskbarPosition:
        // 0 = Basso, 1 = Alto. Con la barra sbloccata il drag (come RetroBar)
        // sposta solo tra questi due bordi. I valori legacy 2/3 (lati)
        // vengono convertiti a Basso quando la configurazione viene letta.

        private int _taskbarPosition = 0;
        private bool _lockTaskbar = true;

        public int TaskbarPosition
        {
            get => _taskbarPosition;
            set => SetField(ref _taskbarPosition, NormalizeTaskbarPosition(value));
        }

        private static int NormalizeTaskbarPosition(int value)
        {
            // Solo i bordi orizzontali sono piu' applicabili: qualunque valore
            // vecchio o corrotto diverso da Alto ricade in Basso.
            return value == 1 ? 1 : 0;
        }

        /// <summary>
        /// v1.3.42: hide the native Win11 XAML tray overlay by injecting
        /// explorer.exe (default ON). Safety valve in Properties.
        /// </summary>
        public bool KillWin11XamlTrayOverlay
        {
            get => _killWin11XamlTrayOverlay;
            set => SetField(ref _killWin11XamlTrayOverlay, value);
        }

        /// <summary>
        /// true = barra bloccata (default, come Win7). Sono disponibili solo
        /// le posizioni orizzontali Basso e Alto.
        /// </summary>
        public bool LockTaskbar
        {
            get => _lockTaskbar;
            set => SetField(ref _lockTaskbar, value);
        }

        private bool _windowsKeyOpensOurMenu = true;
        private bool _killWin11XamlTrayOverlay = true;

        /// <summary>
        /// v1.3.0: Windows key opens our Start Menu (default) or Windows.
        /// Mirrored to HKCU\Software\Win7Taskbar\WindowsKeyOpensOurMenu so
        /// Win7StartHelper.exe can read it without a pipe.
        /// </summary>
        public bool WindowsKeyOpensOurMenu
        {
            get => _windowsKeyOpensOurMenu;
            set
            {
                if (SetFieldReturnChanged(ref _windowsKeyOpensOurMenu, value))
                {
                    WriteWindowsKeyRegistry(value);
                }
            }
        }

        private static void WriteWindowsKeyRegistry(bool ours)
        {
            try
            {
                using var key = Microsoft.Win32.Registry.CurrentUser.CreateSubKey(
                    @"Software\Win7Taskbar");
                key?.SetValue("WindowsKeyOpensOurMenu", ours ? 1 : 0,
                    Microsoft.Win32.RegistryValueKind.DWord);
            }
            catch (Exception)
            {
                /* helper keeps the last readable value / default */
            }
        }

        /// <summary>
        /// Flyout dell'orologio fissato al comportamento Windows 7. La
        /// proprieta' resta per compatibilita' con settings.json e con il
        /// protocollo nativo, ma un valore falso non e' piu' applicabile.
        /// </summary>
        public bool UseNativeClockFlyout
        {
            get => _useNativeClockFlyout;
            set => SetField(ref _useNativeClockFlyout, true);
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
        /// Hover triangle on a running Superbar button that opens the
        /// Jump List. Drag-up remains the only default trigger. Default off.
        /// </summary>
        public bool ShowJumpListHoverArrow
        {
            get => _showJumpListHoverArrow;
            set => SetField(ref _showJumpListHoverArrow, value);
        }

        /// <summary>
        /// Optional Windows 11 Control Center button (default off).
        /// </summary>
        public bool ShowControlCenterButton
        {
            get => _showControlCenterButton;
            set => SetField(ref _showControlCenterButton, value);
        }

        /// <summary>
        /// Optional Windows 11 Notification Center button (default off).
        /// </summary>
        public bool ShowNotificationCenterButton
        {
            get => _showNotificationCenterButton;
            set => SetField(ref _showNotificationCenterButton, value);
        }

        /// <summary>
        /// Task Manager selected for the taskbar context-menu command.
        /// 0 = automatic, 1 = Windows 11 modern, 2 = legacy 32-bit.
        /// The native launcher forces automatic mode on Windows 10.
        /// </summary>
        public int TaskManagerMode
        {
            get => _taskManagerMode;
            set => SetField(ref _taskManagerMode,
                value >= 0 && value <= 2 ? value : 0);
        }

        /// <summary>
        /// Enables the one-shot, validated BitBlt fallback when a delayed
        /// screen probe cannot verify that DWM composed the live thumbnail.
        /// This is deliberately one switch, persisted in settings.json.
        /// </summary>
        public bool UseThumbnailCaptureFallback
        {
            get => _useThumbnailCaptureFallback;
            set => SetField(ref _useThumbnailCaptureFallback, value);
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
        /// v3.8: 2 = "Windows 8 (ricreato)" (variante grafica ricostruita da
        /// Administratox; la logica di rete e' la stessa del modulo Win7).
        /// </summary>
        public int NetworkFlyoutMode
        {
            get => _networkFlyoutMode;
            set => SetField(ref _networkFlyoutMode, value == 1 ? 1 : (value == 2 ? 2 : 0));
        }

        private bool _useClassicVolumeMixer;
        private bool _useBatteryFlyout;

        private int _inputLanguageMode = 1;

        /// <summary>
        /// v3.5: input language indicator in the notification area.
        /// 0 = hidden, 1 = Windows 7 style (two-letter code, default),
        /// 2 = Windows 8.1 style (two-line tile), 3 = Windows 10/11 style
        /// (three-letter code, a bit larger than the native one).
        /// </summary>
        public int InputLanguageMode
        {
            get => _inputLanguageMode;
            set => SetField(ref _inputLanguageMode,
                (value is < 0 or > 3) ? 1 : value);
        }

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

        /* ==================================================================
         * v1.21.7 - Extra settings tab of the Properties window.
         *
         * The entries of the new tab live HERE, in the usual configuration
         * file: no parallel file, no registry key, no alternative settings
         * system. Saving is the atomic one of this class and the label
         * language is the one of the Languages/ dictionaries.
         *
         * What they do NOT touch (explicit constraint): Windows
         * personalization, Windows theme, personalization registry, Windows
         * taskbar, Explorer pinning. They are preferences of the program
         * about itself.
         * ================================================================== */

        /// <summary>Colour offered while the user has not chosen one: it is
        /// the blue of the Windows 8-style interfaces. It is only the starting
        /// point of the colour picker.</summary>
        public const string DefaultFlyoutCustomColor = "#0078D7";

        /// <summary>0x00RRGGBB of the fallback colour (Windows 8 blue).</summary>
        private const int DefaultFlyoutCustomRgb = 0x0078D7;

        private int _flyoutColorMode;                       /* 0 system, 1 custom */
        private string _flyoutCustomColor = DefaultFlyoutCustomColor;
        private int _connectionPrivacyMode;                 /* 0 normal, 1 privacy */
        private int _themeSelection;                        /* 0 Windows 7, 1 Windows 8.1 */
        private List<string> _taskbarIconOrder = new List<string>();

        /// <summary>
        /// v1.21.7: colour of the flyout recreated by the prog drawn by the program: it writes nothing
        /// into the Windows personalization and changes no Windows 7 flyout,
        /// which stay exactly as they were.
        /// </summary>
        public int FlyoutColorMode
        {
            get => _flyoutColorMode;
            set => SetField(ref _flyoutColorMode, value == 1 ? 1 : 0);
        }

        /// <summary>
        /// v1.21.7: custom colour as "#RRGGBB" (the form it is shown and
        /// stored in). An invalid value never enters the configuration: the
        /// default is used instead.
        /// </summary>
        public string FlyoutCustomColor
        {
            get => _flyoutCustomColor;
            set => SetField(ref _flyoutCustomColor,
                            NormalizeColorHex(value, DefaultFlyoutCustomColor));
        }

        /// <summary>0x00RRGGBB of the custom colour, for the native core and
        /// for numeric comparisons.</summary>
        [JsonIgnore]
        public int FlyoutCustomColorRgb => ColorHexToRgb(_flyoutCustomColor);

        /// <summary>
        /// v1.21.7: privacy mode of the recreated connection flyouts.
        /// 0 = normal (real network names), 1 = privacy (generic names,
        /// "Network 1", "Network 2"...).
        ///
        /// It is presentation only: no network API is called and no Windows
        /// setting is touched - the flyout simply draws a different text. The
        /// 0/1 form used today allows more modes to be added later without
        /// changing the stored format.
        /// </summary>
        public int ConnectionFlyoutPrivacyMode
        {
            get => _connectionPrivacyMode;
            set => SetField(ref _connectionPrivacyMode, value == 1 ? 1 : 0);
        }

        /// <summary>
        /// v1.21.7: skin of the taskbar. 0 = Windows 7 (the default),
        /// 1 = Windows 8.1, 2 = Windows 7 Aero Basic, 3 = Windows 8 Beta 8148.
        ///
        /// A skin that is not implemented never becomes the stored value: no
        /// fake theme loaded by hand. See TaskbarThemeIds, which is the only
        /// judgement about availability.
        /// </summary>
        public int ThemeSelection
        {
            get => _themeSelection;
            set => SetField(ref _themeSelection,
                            TaskbarThemeIds.Normalize(value));
        }

        /// <summary>
        /// v1.21.7: icon order of OUR taskbar.
        ///
        /// It is a list of stable KEYS of the items (Application ID /
        /// AppUserModelID, else the executable path, else the launch shortcut
        /// of "shell item" pins), not of positions: the position of an icon
        /// changes by itself when a window opens or closes, the key does not.
        ///
        /// Empty list = the user never reordered anything: the usual order is
        /// used (pins in folder order, then running applications).
        ///
        /// Keys that match nothing any more are NOT deleted: if an application
        /// is uninstalled or its shortcut stops resolving, the key stays and
        /// takes effect again when the application comes back (never a
        /// destructive change).
        ///
        /// This list concerns Win7Taskbar only: the Windows taskbar is neither
        /// read nor modified.
        /// </summary>
        public List<string> TaskbarIconOrder
        {
            get => _taskbarIconOrder;
            /* Deserialization: this does NOT save (reading the configuration
             * back must not rewrite it). To change the order use
             * SetTaskbarIconOrder, which saves. */
            set => _taskbarIconOrder = NormalizeOrder(value);
        }

        /// <summary>v1.21.7: changes the icon order and saves it.</summary>
        public void SetTaskbarIconOrder(IEnumerable<string> order)
            => SetField(ref _taskbarIconOrder,
                        NormalizeOrder(order == null ? null : new List<string>(order)),
                        nameof(TaskbarIconOrder));

        /// <summary>
        /// Converts "#RRGGBB" (or "RRGGBB", or "AARRGGBB") into 0x00RRGGBB.
        /// Invalid text -> fallback colour. The hash is optional so that a
        /// hand-edited file cannot break the Properties window.
        /// </summary>
        public static int ColorHexToRgb(string? hex)
        {
            return TryParseColorHex(hex, out int rgb) ? rgb : DefaultFlyoutCustomRgb;
        }

        /// <summary>True if the text is a 6-digit hexadecimal colour (hash
        /// optional, an alpha channel is ignored).</summary>
        public static bool TryParseColorHex(string? hex, out int rgb)
        {
            rgb = 0;
            string? text = HexDigits(hex);
            if (text == null)
            {
                return false;
            }

            if (!int.TryParse(text, NumberStyles.HexNumber,
                              CultureInfo.InvariantCulture, out int parsed))
            {
                return false;
            }

            rgb = parsed & 0xFFFFFF;
            return true;
        }

        /// <summary>Canonical stored and shown form: uppercase "#RRGGBB".
        /// Invalid text becomes the fallback colour, never a broken string
        /// inside the configuration.</summary>
        public static string NormalizeColorHex(string? hex, string fallback)
        {
            int rgb = TryParseColorHex(hex, out int parsed)
                ? parsed
                : ColorHexToRgb(fallback);
            return "#" + rgb.ToString("X6", CultureInfo.InvariantCulture);
        }

        /// <summary>The six hexadecimal digits of a colour, or null.</summary>
        private static string? HexDigits(string? hex)
        {
            if (string.IsNullOrWhiteSpace(hex))
            {
                return null;
            }

            string text = hex.Trim();
            if (text.StartsWith("#", StringComparison.Ordinal))
            {
                text = text.Substring(1);
            }
            if (text.Length == 8)   /* AARRGGBB: the alpha is not needed here */
            {
                text = text.Substring(2);
            }

            if (text.Length != 6)
            {
                return null;
            }

            foreach (char c in text)
            {
                if (!Uri.IsHexDigit(c))
                {
                    return null;
                }
            }
            return text;
        }

        /// <summary>Order list without empty, repeated or oversized keys: the
        /// configuration stays readable and the comparison between two orders
        /// does not depend on spaces or letter case.</summary>
        private static List<string> NormalizeOrder(List<string>? order)
        {
            var result = new List<string>();
            if (order == null)
            {
                return result;
            }

            var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (string key in order)
            {
                if (result.Count >= 512)
                {
                    break;   /* safety cap: the list does not grow forever */
                }
                if (string.IsNullOrWhiteSpace(key))
                {
                    continue;
                }

                string trimmed = key.Trim();
                if (seen.Add(trimmed))
                {
                    result.Add(trimmed);
                }
            }
            return result;
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

            /* La UI non espone piu' il flyout ricreato: anche una
             * configurazione gia' migrata deve diventare Windows 7. */
            if (!settings._useNativeClockFlyout)
            {
                settings._useNativeClockFlyout = true;
                changed = true;
            }

            int normalizedPosition = NormalizeTaskbarPosition(settings._taskbarPosition);
            if (normalizedPosition != settings._taskbarPosition)
            {
                settings._taskbarPosition = normalizedPosition;
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
            WriteWindowsKeyRegistry(settings._windowsKeyOpensOurMenu);
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
            SetFieldReturnChanged(ref field, value, propertyName);
        }

        private bool SetFieldReturnChanged<T>(ref T field, T value,
            [CallerMemberName] string? propertyName = null)
        {
            if (Equals(field, value))
            {
                return false;
            }
            field = value;
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
            Save();
            return true;
        }
    }
}
