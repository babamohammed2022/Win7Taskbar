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
// The click does NOT open a WPF menu: it calls the core's native popup (a
// Win32 window drawn with GDI/GDI+, same logic as the mod). Language
// changes arrive as a native callback; a 200 ms poll timer acts as a
// safety net (the same polling mechanic ManagedShell uses).
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
        private NativeMethods.W7TLangChangedCallback? _callbackKeepAlive;

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
                RegisterCallbackOnce();
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
                IntPtr fg = NativeMethods.GetForegroundWindow();
                var source = PresentationSource.FromVisual(this) as HwndSource;
                ulong owner = source != null ? (ulong)source.Handle : 0;
                NativeMethods.W7T_LangSwitcherShow(
                    owner, fg != IntPtr.Zero ? (ulong)fg : 0, Mode);
            }
            catch (DllNotFoundException) { }
            catch (EntryPointNotFoundException) { }
            catch (Exception)
            {
                /* Never let the taskbar fall because of the indicator. */
            }
        }

        /* ------------------------------------------------------------ */
        /*  Data from the native side (active abbreviation + change      */
        /*  callback)                                                    */
        /* ------------------------------------------------------------ */

        private bool _callbackRegistered;

        private void RegisterCallbackOnce()
        {
            if (_callbackRegistered)
            {
                return;
            }
            try
            {
                _callbackKeepAlive = OnLangChanged;
                NativeMethods.W7T_LangSwitcherSetChangedCallback(
                    _callbackKeepAlive);
                _callbackRegistered = true;
            }
            catch (DllNotFoundException) { _callbackRegistered = true; }
            catch (EntryPointNotFoundException) { _callbackRegistered = true; }
            catch (Exception) { /* the poll timer remains the safety net */ }
        }

        /* Called on the UI thread (the timer lives in the native popup,
         * which was created on this thread). */
        private void OnLangChanged(uint langId)
        {
            Dispatcher.BeginInvoke(new Action(() => ApplyLang(langId)),
                DispatcherPriority.Background);
        }

        private void RefreshFromNative()
        {
            try
            {
                uint langId = 0;
                char[] three = new char[8];
                char[] two = new char[8];
                NativeMethods.W7T_LangSwitcherGetActive(
                    ref langId, three, three.Length, two, two.Length);
                ApplyLang(langId, new string(three).TrimEnd('\0'),
                    new string(two).TrimEnd('\0'));
            }
            catch (DllNotFoundException) { }
            catch (EntryPointNotFoundException) { }
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
