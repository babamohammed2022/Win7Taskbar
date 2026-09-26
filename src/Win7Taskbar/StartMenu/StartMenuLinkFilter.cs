// Win7Taskbar - hide dead / leftover Start Menu shortcuts
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
//
// Inspired by Open-Shell's public behavior (do not list .lnk whose
// target is gone, skip known retired Windows 7 leftovers). Written
// from scratch — Open-Shell source is not copied.

using System;
using System.IO;

namespace Win7Taskbar.StartMenu
{
    internal static class StartMenuLinkFilter
    {
        private static readonly string[] DeadNames =
        {
            "Windows Anytime Upgrade",
            "Windows Easy Transfer",
            "Windows DVD Maker",
            "Windows Media Center",
            "Windows Journal",
            "Getting Started",
            "Windows Experience Index",
            "Windows CardSpace",
            "Windows Meeting Space",
            "Windows Ultimate Extras",
            "Windows Sidebar",
            "Desktop Gadgets",
            "Windows Marketplace",
            "Microsoft Silverlight",
            "Windows Live Messenger",
            "Windows Live Mail",
            "Windows Live Photo Gallery",
            "Windows Live Writer",
            "Windows Live Mesh",
            "Windows Live Movie Maker",
            "Aggiornamento in qualsiasi momento di Windows",
            "Trasferimento facile Windows",
            "DVD Maker di Windows",
            "Indice prestazioni Windows",
            "Barra laterale di Windows",
            "Gadget del desktop",
        };

        internal static bool Hide(string? name, string? path, string? target)
        {
            try
            {
                if (IsShellNamespace(path) || IsShellNamespace(target))
                {
                    return false;
                }
                if (NameBlocked(name) || NameBlocked(FileTitle(path)))
                {
                    return true;
                }
                string probe = string.IsNullOrWhiteSpace(target) ? (path ?? string.Empty)
                    : target;
                if (string.IsNullOrWhiteSpace(probe))
                {
                    return false;
                }
                if (JunkTarget(probe))
                {
                    return true;
                }
                if (LooksLikeFilesystem(probe) &&
                    !File.Exists(probe) &&
                    !Directory.Exists(probe))
                {
                    return true;
                }
            }
            catch (Exception)
            {
            }
            return false;
        }

        private static bool NameBlocked(string? name)
        {
            if (string.IsNullOrWhiteSpace(name))
            {
                return false;
            }
            foreach (string dead in DeadNames)
            {
                if (string.Equals(name, dead, StringComparison.OrdinalIgnoreCase))
                {
                    return true;
                }
            }
            return false;
        }

        private static bool JunkTarget(string target)
        {
            return target.IndexOf("InstallShield Installation Information",
                       StringComparison.OrdinalIgnoreCase) >= 0
                || target.IndexOf("\\Windows\\Installer\\{",
                       StringComparison.OrdinalIgnoreCase) >= 0;
        }

        private static bool IsShellNamespace(string? value)
        {
            if (string.IsNullOrWhiteSpace(value))
            {
                return false;
            }
            return value.StartsWith("shell:", StringComparison.OrdinalIgnoreCase)
                || value.StartsWith("::{", StringComparison.Ordinal)
                || value.StartsWith("ms-settings:", StringComparison.OrdinalIgnoreCase)
                || value.IndexOf("AppsFolder", StringComparison.OrdinalIgnoreCase) >= 0;
        }

        private static bool LooksLikeFilesystem(string value)
        {
            if (value.Length < 3)
            {
                return false;
            }
            if (value.StartsWith("\\\\", StringComparison.Ordinal))
            {
                return true;
            }
            return char.IsLetter(value[0]) && value[1] == ':' &&
                   (value[2] == '\\' || value[2] == '/');
        }

        private static string? FileTitle(string? path)
        {
            if (string.IsNullOrWhiteSpace(path))
            {
                return null;
            }
            try
            {
                return Path.GetFileNameWithoutExtension(path);
            }
            catch (Exception)
            {
                return null;
            }
        }
    }
}
