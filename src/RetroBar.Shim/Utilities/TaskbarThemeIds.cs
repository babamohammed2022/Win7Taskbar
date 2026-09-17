// Win7Taskbar - RetroBar compatibility shim
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under GPL v3 or later
//
// v1.21.7 - Taskbar skin identifiers.
//
// The list lives here (and not in the application project) because the skin
// choice is an ordinary configuration entry: the id stored in settings.json
// must be validated by whoever owns the settings, without the settings file
// having to know the names of the theme files. ThemeLoader (Win7Taskbar
// project) resolves the file name.
//
// ONLY ONE SKIN IS IMPLEMENTED TODAY: Windows 7. "Windows 8.1" is listed as
// id 1 because the value exists and is prepared, but it is not selectable:
// the Properties window shows the entry as unavailable and Settings brings
// the choice back to Windows 7. When the skin arrives it is enough to add its
// name to IsImplemented and its file to ThemeLoader.

namespace RetroBar.Utilities
{
    /// <summary>Selectable taskbar skins / Skin della barra selezionabili.</summary>
    public static class TaskbarThemeIds
    {
        /// <summary>Windows 7 skin: the default value and the safe fallback.</summary>
        public const int Windows7 = 0;

        /// <summary>Windows 8.1 skin (v1.21.19): implemented. Its theme file is
        /// Themes/Windows8.1.xaml - the name ThemeLoader.ThemeFileNameFor maps
        /// this id to - and its Start button uses the two sprites embedded in
        /// GraphicalResourceBundle (startwin81flag / startwin81flagscaled).</summary>
        public const int Windows81 = 1;

        /// <summary>
        /// True if the given skin really exists in this version: this is the
        /// single judgement that keeps the Properties dropdown and the theme
        /// file loaded at startup together. No fake skins: an id whose theme
        /// file is not shipped stays out of this list.
        /// </summary>
        public static bool IsImplemented(int themeId) =>
            themeId == Windows7 || themeId == Windows81;

        /// <summary>Normalizes an id read from the configuration.</summary>
        public static int Normalize(int themeId) =>
            IsImplemented(themeId) ? themeId : Windows7;
    }
}
