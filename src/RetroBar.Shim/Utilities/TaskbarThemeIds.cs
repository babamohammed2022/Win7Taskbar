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
// THREE SKINS ARE IMPLEMENTED: Windows 7 (id 0, the default and the safe
// fallback), Windows 8.1 (id 1, v1.21.19) and Windows 7 Aero Basic (id 2,
// v1.21.42 - the Windows 7 skin with the Aero glass background replaced by
// an opaque light gray-blue surface; it shares Themes/Windows7.xaml and adds
// the Themes/AeroBasic.xaml override dictionary, see ThemeLoader).

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

        /// <summary>Windows 7 Aero Basic skin (v1.21.42): implemented. Same
        /// layout, buttons, tray and behavior as the Windows 7 (Aero) skin -
        /// the theme file is still Themes/Windows7.xaml, untouched - with the
        /// glass taskbar background replaced by the opaque light gray-blue
        /// surface of Themes/AeroBasic.xaml, merged on top by ThemeLoader
        /// only while this skin is selected. No glass, no blur, no
        /// transparency, no reflections.</summary>
        public const int Windows7AeroBasic = 2;

        /// <summary>
        /// True if the given skin really exists in this version: this is the
        /// single judgement that keeps the Properties dropdown and the theme
        /// file loaded at startup together. No fake skins: an id whose theme
        /// file is not shipped stays out of this list.
        /// </summary>
        public static bool IsImplemented(int themeId) =>
            themeId == Windows7 || themeId == Windows81 ||
            themeId == Windows7AeroBasic;

        /// <summary>Normalizes an id read from the configuration.</summary>
        public static int Normalize(int themeId) =>
            IsImplemented(themeId) ? themeId : Windows7;
    }
}
