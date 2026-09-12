// Win7Taskbar - Localization manager
// English: Manages language switching between Italian and English
// Italiano: Gestisce il cambio lingua tra Italiano e Inglese
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later

using System;
using System.IO;
using System.Windows;
using System.Windows.Markup;
using System.Xml.Linq;
using RetroBar.Utilities;

namespace Win7Taskbar.Utilities
{
    /// <summary>
    /// Loads language ResourceDictionary from Languages folder and injects into Application resources.
    /// English: one dictionary per supported language (it, en, es, fr, de, pt,
    /// pl, ru, ja, zh, ar). Anything unsupported falls back to English, never
    /// to Italian: Italian is a translation like the others, not the default.
    /// </summary>
    public static class LocalizationManager
    {
        private const string LanguageDictKey = "Win7TaskbarLanguage";

        /// <summary>
        /// Get current language code: "it" or "en"
        /// </summary>
        public static string CurrentLanguage => Settings.Instance.Language;

        /// <summary>
        /// Load language dictionary and merge into Application.Resources
        /// Call on startup and when language changes.
        /// </summary>
        public static void ApplyLanguage(string langCode)
        {
            if (Application.Current == null) return;
            try
            {
                if (string.IsNullOrEmpty(langCode)) langCode = Settings.DefaultLanguageCode;
                langCode = NormalizeLanguageCode(langCode);

                // Remove previous language dictionary if present
                var toRemove = new System.Collections.Generic.List<ResourceDictionary>();
                foreach (var dict in Application.Current.Resources.MergedDictionaries)
                {
                    if (dict.Contains(LanguageDictKey) || dict.Source != null && dict.Source.OriginalString.Contains("Languages/"))
                    {
                        toRemove.Add(dict);
                    }
                }
                foreach (var d in toRemove)
                    Application.Current.Resources.MergedDictionaries.Remove(d);

                // Load new dictionary
                ResourceDictionary langDict = LoadLanguageDictionary(langCode);
                // Mark it
                langDict[LanguageDictKey] = true;
                Application.Current.Resources.MergedDictionaries.Add(langDict);
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine($"LocalizationManager: failed to apply language {langCode}: {ex.Message}");
            }
        }

        /// <summary>
        /// Normalizes a language code against the single list in Settings (which
        /// mirrors the list of the native core): an unsupported or unknown code
        /// becomes English, never Italian.
        /// Italiano: normalizza il codice sull'elenco di Settings; codice
        /// sconosciuto o non supportato -> inglese, mai italiano.
        /// </summary>
        public static string NormalizeLanguageCode(string? langCode)
        {
            if (string.IsNullOrWhiteSpace(langCode))
                return Settings.DefaultLanguageCode;

            string code = langCode.Trim().ToLowerInvariant();
            return Array.IndexOf(Settings.SupportedLanguages, code) >= 0
                ? code
                : Settings.DefaultLanguageCode;
        }

        /// <summary>Code -> dictionary file (English is the fallback).
        /// Codice -> file dizionario (ripiego: Inglese).</summary>
        private static string LanguageFileFor(string langCode) => langCode switch
        {
            "en" => "English.xaml",
            "es" => "Spanish.xaml",
            "fr" => "French.xaml",
            "de" => "German.xaml",
            "pt" => "Portuguese.xaml",
            "pl" => "Polish.xaml",
            "ru" => "Russian.xaml",
            "ja" => "Japanese.xaml",
            "zh" => "Chinese.xaml",
            "ar" => "Arabic.xaml",
            _ => "English.xaml",
        };

        private static ResourceDictionary LoadLanguageDictionary(string langCode)
        {
            string fileName = LanguageFileFor(langCode);
            string path = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Languages", fileName);
            if (File.Exists(path))
            {
                try
                {
                    // Load XAML from file to allow hot-swap without rebuild
                    using var stream = File.OpenRead(path);
                    var dict = (ResourceDictionary)XamlReader.Load(stream);
                    return dict;
                }
                catch
                {
                    // fallback to pack URI
                }
            }

            // Fallback to pack URI from assembly resources
            string packUri = $"pack://application:,,,/Win7Taskbar;component/Languages/{fileName}";
            try
            {
                return new ResourceDictionary { Source = new Uri(packUri, UriKind.Absolute) };
            }
            catch
            {
                // Ultimate fallback: embedded English
                string fallback = "pack://application:,,,/Win7Taskbar;component/Languages/English.xaml";
                return new ResourceDictionary { Source = new Uri(fallback, UriKind.Absolute) };
            }
        }

        /// <summary>
        /// Helper to get localized string by key, with fallback to key itself.
        /// </summary>
        public static string GetString(string key)
        {
            try
            {
                if (Application.Current != null && Application.Current.Resources.Contains(key))
                {
                    if (Application.Current.Resources[key] is string s)
                        return s;
                }
                // Search merged dictionaries
                if (Application.Current != null)
                {
                    foreach (var dict in Application.Current.Resources.MergedDictionaries)
                    {
                        if (dict.Contains(key) && dict[key] is string str)
                            return str;
                    }
                }
            }
            catch { }
            return key;
        }

        /// <summary>
        /// Apply language from settings
        /// </summary>
        public static void ApplyCurrentLanguage()
        {
            ApplyLanguage(Settings.Instance.Language);
        }
    }
}
