// Win7Taskbar - voce "lingua" della tray (text language switcher)
// Copyright (c) 2026 Win7Taskbar contributors
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//
// ============================================================================
// v1.4: THE LANGUAGE LABEL IS DRAWN HERE, in the tray XAML, in the same
// style as the other entries (battery, network, volume): the active
// abbreviation ("ENG", "ITA", ...) comes from the native core (a port of
// the "Windows 7/8.1 Language Switcher Restorer" mod), which reads it from
// the thread that owns the keyboard focus.
//
// v1.7.5: the click is back on the DEDICATED NATIVE POPUP of the port
// (the Win7-menu / Win8-flyout switcher): the managed side only posts a
// show request to the popup thread and returns immediately - no managed
// code runs inside the popup. The popup itself was hardened: the manual
// GDI+ loading, the SEH longjmp wrappers and the managed callback were
// removed from it (RAII guards + try/catch everywhere), so the old exit-
// on-click class has nothing left to bite on. If the native popup cannot
// be shown (missing export, failed post), the v1.7.4 shell-menu path
// remains as the fallback. Language changes arrive via the 200 ms poll
// timer (the same polling mechanic ManagedShell uses).
// ============================================================================

using System;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Threading;
using Win7Taskbar.Interop;

namespace Win7Taskbar.Controls
{
    /// <summary>
    /// Tray "language" entry: the active language abbreviation, drawn in
    /// the same style as the other icons; the click opens the core's
    /// native selector (a Win32 window, port of the mod).
    /// </summary>
    public sealed class InputLanguageBar : Border
    {
        /* Styles selectable in Properties (InputLanguageMode). */
        public const int ModeHidden = 0;
        public const int ModeWin7 = 1;
        public const int ModeWin81 = 2;
        public const int ModeWin10 = 3;

        public static readonly DependencyProperty ModeProperty =
            DependencyProperty.Register(nameof(Mode), typeof(int),
                typeof(InputLanguageBar),
                new FrameworkPropertyMetadata(ModeWin7, OnModeChanged));

        /// <summary>0 hidden, 1 Windows 7, 2 Windows 8.1, 3 Windows 10/11.</summary>
        public int Mode
        {
            get => (int)GetValue(ModeProperty);
            set => SetValue(ModeProperty, value);
        }

        private readonly TextBlock _primary = new TextBlock();
        private readonly TextBlock _secondary = new TextBlock();
        private readonly StackPanel? _tile;
        private readonly DispatcherTimer _pollTimer;
        private uint _lastLangId;

        public InputLanguageBar()
        {
            IsHitTestVisible = true;
            Cursor = System.Windows.Input.Cursors.Hand;
            Background = Brushes.Transparent;
            Child = BuildContent(out _tile);

            _pollTimer = new DispatcherTimer(DispatcherPriority.Background)
            {
                Interval = TimeSpan.FromMilliseconds(200),
            };
            _pollTimer.Tick += (sender, args) => RefreshFromNative();
            Loaded += (sender, args) =>
            {
                /* v1.5: the visibility is enforced here too. The XAML starts
                 * the control Collapsed, and a mode equal to the current
                 * value never fires OnModeChanged (a DependencyProperty
                 * ignores no-op writes), so without this explicit sync the
                 * entry stayed invisible forever with the default mode. */
                SyncModeVisuals();
                /* v1.7.4: the native change callback is NOT registered
                 * anymore. The shell-menu rewrite below needs no callback:
                 * the abbreviation refreshes through the poll timer that
                 * reads the core directly. No managed delegate ever leaves
                 * the process now. */
                RefreshFromNative();
                _pollTimer.Start();
            };
            Unloaded += (sender, args) => _pollTimer.Stop();
        }

        private static void OnModeChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        {
            var bar = (InputLanguageBar)d;
            bar.SyncModeVisuals();
        }

        /// <summary>Applies the mode unconditionally: visibility and layout
        /// are refreshed even when the value did not change (the exact case
        /// that kept the entry hidden with the default Windows 7 mode).</summary>
        public void ApplyMode(int mode)
        {
            if (mode < ModeHidden || mode > ModeWin10 || mode == Mode)
            {
                SyncModeVisuals();
                return;
            }
            Mode = mode;   /* OnModeChanged syncs the visuals. */
        }

        private void SyncModeVisuals()
        {
            Visibility = Mode == ModeHidden
                ? Visibility.Collapsed
                : Visibility.Visible;
            RefreshLayout();
        }

        private UIElement BuildContent(out StackPanel? tile)
        {
            _primary.SetResourceReference(TextBlock.ForegroundProperty,
                "InputLanguageForeground");
            _primary.SetResourceReference(TextBlock.FontFamilyProperty,
                "GlobalFontFamily");

            /* v1.21.28 - anche la riga secondaria della card Windows 8.1 deve
             * usare il foreground del tema (bianco): senza, il TextBlock parte
             * nero di default e la scritta "ITA / IT" esce meta' bianca e meta'
             * nera, mentre su Windows 7 l'unica riga e' bianca. */
            _secondary.SetResourceReference(TextBlock.ForegroundProperty,
                "InputLanguageForeground");
            _secondary.SetResourceReference(TextBlock.FontFamilyProperty,
                "GlobalFontFamily");

            /* The Windows 8.1 card: three-letter abbreviation on top,
             * two-letter below (RefreshLayout fills the content). */
            var stack = new StackPanel
            {
                Orientation = Orientation.Vertical,
                VerticalAlignment = VerticalAlignment.Center,
            };
            stack.Children.Add(_primary);
            stack.Children.Add(_secondary);
            tile = stack;
            return stack;
        }

        private void RefreshLayout()
        {
            switch (Mode)
            {
                case ModeWin81:
                    _primary.FontSize = 11;
                    _primary.FontWeight = FontWeights.SemiBold;
                    _primary.HorizontalAlignment = HorizontalAlignment.Center;
                    _primary.Margin = new Thickness(0, 0, 0, -2);
                    _secondary.FontSize = 10;
                    _secondary.FontWeight = FontWeights.Normal;
                    _secondary.HorizontalAlignment = HorizontalAlignment.Center;
                    _secondary.Margin = new Thickness(0, 0, 0, 1);
                    _secondary.Visibility = Visibility.Visible;
                    Padding = new Thickness(4, 1, 4, 1);
                    break;

                case ModeWin10:
                    _primary.FontSize = 14;
                    _primary.FontWeight = FontWeights.Normal;
                    _primary.Margin = new Thickness(0);
                    _secondary.Visibility = Visibility.Collapsed;
                    Padding = new Thickness(6, 0, 6, 0);
                    break;

                default:
                    /* Windows 7: the three-letter abbreviation, sized for
                     * the taskbar. */
                    _primary.FontSize = 11;
                    _primary.FontWeight = FontWeights.SemiBold;
                    _primary.Margin = new Thickness(0);
                    _secondary.Visibility = Visibility.Collapsed;
                    Padding = new Thickness(4, 0, 4, 0);
                    break;
            }
        }

        /// <summary>The click takes the foreground away from the user's
        /// window: the layout-switch target must be captured BEFORE
        /// (PreviewMouseDown), then the native popup opens.</summary>
        protected override void OnPreviewMouseDown(
            System.Windows.Input.MouseButtonEventArgs e)
        {
            base.OnPreviewMouseDown(e);
            if (e.ChangedButton != System.Windows.Input.MouseButton.Left ||
                Mode == ModeHidden)
            {
                return;
            }

            try
            {
                ShowLanguageSwitcher();
            }
            catch (DllNotFoundException) { }
            catch (EntryPointNotFoundException) { }
            catch (Exception ex)
            {
                /* Never let the taskbar fall because of the indicator. */
                Utilities.DiagnosticLogger.WriteException("LANGSW", ex);
            }
        }

        /// <summary>
        /// v1.7.5: the click first tries the dedicated native popup
        /// (W7T_LangSwitcherShow): the call only POSTS a request to the
        /// popup's own thread and returns immediately, so the WPF input
        /// thread never touches the popup's windows or state, and no
        /// managed callback comes back (the callback machinery was
        /// removed from the native side; the abbreviation refreshes via
        /// the poll timer). If the popup path is unavailable, the v1.7.4
        /// shell menu (W7T_ShowContextMenuEx) remains as the fallback.
        /// </summary>
        private void ShowLanguageSwitcher()
        {
            try
            {
                IntPtr fg = NativeMethods.GetForegroundWindow();
                var source = PresentationSource.FromVisual(this) as HwndSource;
                ulong owner = source != null ? (ulong)source.Handle : 0;
                NativeMethods.W7T_LangSwitcherShow(
                    owner, fg != IntPtr.Zero ? (ulong)fg : 0, Mode);
                Utilities.DiagnosticLogger.Write("LANGSW",
                    "click: native switcher popup requested");
                return;
            }
            catch (DllNotFoundException dnfe)
            {
                Utilities.DiagnosticLogger.Write("LANGSW",
                    "native popup unavailable (DLL missing): " + dnfe.Message);
            }
            catch (EntryPointNotFoundException epnfe)
            {
                Utilities.DiagnosticLogger.Write("LANGSW",
                    "native popup unavailable (export missing): " +
                    epnfe.Message);
            }

            /* Fallback: the v1.7.4 plain Win32 menu. */
            ShowLanguageMenuViaShell();
        }

        /// <summary>
        /// v1.7.4 (kept as fallback): the language list opens as a plain
        /// Win32 menu through W7T_ShowContextMenuEx - the exact path the
        /// clock and the bar menus use. Picking a language posts the
        /// canonical WM_INPUTLANGCHANGEREQUEST to the foreground window.
        /// </summary>
        private void ShowLanguageMenuViaShell()
        {
            IntPtr[] layouts = NativeMethods.GetInstalledKeyboardLayouts();
            if (layouts.Length == 0)
            {
                Utilities.DiagnosticLogger.Write("LANGSW",
                    "no keyboard layouts reported; menu not shown");
                return;
            }

            // Active layout = the foreground window's thread layout.
            IntPtr foreground = NativeMethods.GetForegroundWindow();
            IntPtr active = IntPtr.Zero;
            if (foreground != IntPtr.Zero)
            {
                uint tid = NativeMethods.GetWindowThreadProcessId(
                    foreground, out _);
                if (tid != 0)
                {
                    active = NativeMethods.GetKeyboardLayout(tid);
                }
            }
            uint activeLangId = (uint)(active.ToInt64() & 0xFFFF);

            var items = new System.Text.StringBuilder();
            foreach (IntPtr hkl in layouts)
            {
                if (items.Length > 0)
                {
                    items.Append('\n');
                }
                string name = NativeMethods.GetLanguageDisplayName(hkl);
                if (string.IsNullOrEmpty(name))
                {
                    name = "0x" + ((uint)(hkl.ToInt64() & 0xFFFF)).ToString("X4");
                }
                bool isActive =
                    ((uint)(hkl.ToInt64() & 0xFFFF)) == activeLangId;
                items.Append(isActive ? "*" : "").Append(name);
            }

            // Anchor: above the indicator, bottom-aligned so the menu grows
            // upward from the taskbar (same as the app menus).
            Point origin = PointToScreen(
                new Point(0, ActualHeight));
            int x = (int)Math.Round(origin.X);
            int y = (int)Math.Round(origin.Y);
            Utilities.DiagnosticLogger.Write("LANGSW",
                $"shell menu; layouts={layouts.Length}; active=0x" +
                activeLangId.ToString("X4"));

            int choice = NativeMethods.W7T_ShowContextMenuEx(
                x, y, bottomEdge: 1, items.ToString(), anchorAtCursor: 0);
            Utilities.DiagnosticLogger.Write("LANGSW",
                $"shell menu returned choice={choice}");

            if (choice < 1 || choice > layouts.Length)
            {
                return; // cancelled
            }

            IntPtr picked = layouts[choice - 1];
            if (foreground != IntPtr.Zero)
            {
                NativeMethods.PostMessageW(
                    foreground, NativeMethods.WM_INPUTLANGCHANGEREQUEST,
                    IntPtr.Zero, picked);
                Utilities.DiagnosticLogger.Write("LANGSW",
                    $"posted WM_INPUTLANGCHANGEREQUEST hkl=0x" +
                    picked.ToInt64().ToString("X"));
            }
        }

        /* ------------------------------------------------------------ */
        /*  Data from the native side (active abbreviation)             */
        /* ------------------------------------------------------------ */

        private bool _exportProbeLogged;

        private void RefreshFromNative()
        {
            try
            {
                uint langId = 0;
                char[] three = new char[8];
                char[] two = new char[8];
                NativeMethods.W7T_LangSwitcherGetActive(
                    ref langId, three, three.Length, two, two.Length);
                if (!_exportProbeLogged)
                {
                    _exportProbeLogged = true;
                    Utilities.DiagnosticLogger.Write("LANGSW",
                        "core exports present (GetActive ok)");
                }
                ApplyLang(langId, new string(three).TrimEnd('\0'),
                    new string(two).TrimEnd('\0'));
            }
            catch (EntryPointNotFoundException)
            {
                if (!_exportProbeLogged)
                {
                    _exportProbeLogged = true;
                    Utilities.DiagnosticLogger.Write("LANGSW",
                        "core exports MISSING (old Win7TaskbarCore.dll " +
                        "loaded next to the exe?)");
                }
            }
            catch (DllNotFoundException) { }
            catch (Exception)
            {
                /* No text is better than a broken taskbar. */
            }
        }

        private void ApplyLang(uint langId, string? three = null, string? two = null)
        {
            if (langId != 0 && langId == _lastLangId && three == null)
            {
                return;
            }
            if (langId != 0)
            {
                _lastLangId = langId;
            }

            if (string.IsNullOrEmpty(three) || string.IsNullOrEmpty(two))
            {
                char[] threeBuf = new char[8];
                char[] twoBuf = new char[8];
                uint id = _lastLangId;
                try
                {
                    NativeMethods.W7T_LangSwitcherGetActive(
                        ref id, threeBuf, threeBuf.Length, twoBuf, twoBuf.Length);
                    three = new string(threeBuf).TrimEnd('\0');
                    two = new string(twoBuf).TrimEnd('\0');
                    _lastLangId = id;
                }
                catch (Exception)
                {
                    return;
                }
            }

            if (string.IsNullOrEmpty(three))
            {
                three = "---";
            }
            if (string.IsNullOrEmpty(two))
            {
                two = three.Length >= 2 ? three.Substring(0, 2) : "--";
            }

            _primary.Text = three;
            _secondary.Text = two;
        }
    }
}
