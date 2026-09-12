// Win7Taskbar - Localization manager
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later

using System;
using System.Collections.Generic;
using System.IO;
using System.Windows;
using System.Windows.Markup;
using RetroBar.Utilities;

namespace Win7Taskbar.Utilities
{
    /// <summary>Loads the selected WPF language dictionary and applies it to the application.</summary>
    public static class LocalizationManager
    {
        private const string LanguageDictKey = "Win7TaskbarLanguage";

        public static string CurrentLanguage => Settings.Instance.Language;

        public static void ApplyLanguage(string langCode)
        {
            if (Application.Current == null) return;
            try
            {
                langCode = NormalizeLanguageCode(langCode);

                var toRemove = new List<ResourceDictionary>();
                foreach (var dict in Application.Current.Resources.MergedDictionaries)
                {
                    if (dict.Contains(LanguageDictKey) ||
                        (dict.Source != null && dict.Source.OriginalString.Contains("Languages/")))
                    {
                        toRemove.Add(dict);
                    }
                }
                foreach (var dict in toRemove)
                    Application.Current.Resources.MergedDictionaries.Remove(dict);

                ResourceDictionary language = LoadLanguageDictionary(langCode);
                language[LanguageDictKey] = true;
                Application.Current.Resources.MergedDictionaries.Add(language);
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine($"LocalizationManager: failed to apply language {langCode}: {ex.Message}");
            }
        }

        /// <summary>
        /// Returns a supported two-letter language code. Unsupported values use English.
        /// </summary>
        private static string NormalizeLanguageCode(string? langCode)
        {
            if (string.IsNullOrWhiteSpace(langCode))
                return "en";

            string code = langCode.Trim().ToLowerInvariant();
            return code switch
            {
                "it" or "en" or "es" or "fr" or "de" or "pt" or "pl" or "ru" or "ja" or "zh" or "ar" => code,
                _ => "en",
            };
        }

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
            _ => "Italian.xaml",
        };

        private static ResourceDictionary LoadLanguageDictionary(string langCode)
        {
            string fileName = LanguageFileFor(langCode);
            string path = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Languages", fileName);
            if (File.Exists(path))
            {
                try
                {
                    using var stream = File.OpenRead(path);
                    return (ResourceDictionary)XamlReader.Load(stream);
                }
                catch
                {
                    // Fall through to the embedded resource.
                }
            }

            string packUri = $"pack://application:,,,/Win7Taskbar;component/Languages/{fileName}";
            try
            {
                return new ResourceDictionary { Source = new Uri(packUri, UriKind.Absolute) };
            }
            catch
            {
                string fallback = "pack://application:,,,/Win7Taskbar;component/Languages/English.xaml";
                return new ResourceDictionary { Source = new Uri(fallback, UriKind.Absolute) };
            }
        }

        public static string GetString(string key)
        {
            try
            {
                if (Application.Current != null && Application.Current.Resources.Contains(key) &&
                    Application.Current.Resources[key] is string value)
                {
                    return value;
                }

                if (Application.Current != null)
                {
                    foreach (var dict in Application.Current.Resources.MergedDictionaries)
                    {
                        if (dict.Contains(key) && dict[key] is string value)
                            return value;
                    }
                }
            }
            catch { }
            return key;
        }

        public static void ApplyCurrentLanguage()
        {
            ApplyLanguage(Settings.Instance.Language);
        }
    }
}
