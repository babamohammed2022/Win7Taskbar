// Win7Taskbar - indicatore della lingua di input (la "barra della lingua")
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
// v3.5 - L'INDICATORE DELLA LINGUA NELLA NOTIFICATION AREA, come in
// Windows 7/8.1/10. E' il pezzo che RetroBar chiama InputLanguage, scritto
// qui in forma autonoma: niente ManagedShell, niente WinForms, solo le
// stesse API di Windows che quelli usano dentro.
//
//   - la lingua corrente la si legge dal THREAD CON FOCUS (GetGUIThreadInfo
//     + GetKeyboardLayout, lo stesso metodo di KeyboardLayoutHelper di
//     ManagedShell e del controllo di RetroBar): il valore di
//     GetKeyboardLayout(0) sarebbe quello del nostro processo e sarebbe
//     quasi sempre sbagliato;
//   - l'elenco delle lingue installate arriva da GetKeyboardLayoutList;
//   - il cambio lingua e' il messaggio di sistema WM_INPUTLANGCHANGEREQUEST
//     in broadcast HWND_BROADCAST: e' quello che Windows manda quando
//     l'utente preme Alt+Shift, e quello che WinForms.InputLanguage usa;
//   - il menu di scelta e' il menu nativo del core (ShowContextMenuEx),
//     lo stesso delle altre voci della barra: aspetto identico a Windows 7.
//
// TRE STILI, scelti dalle Proprieta' (Settings.InputLanguageMode):
//   1 = Windows 7      : la sigla a due lettere ("IT"), testo piccolo;
//   2 = Windows 8.1    : la targhetta a due righe ("ITA" sopra, "IT" sotto);
//   3 = Windows 10/11  : la sigla a tre lettere ("ENG"), un po' piu' grande
//                        della nativa, come chiesto.
//   0 = nascosta.
// ============================================================================

using System;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Threading;

namespace Win7Taskbar.Controls
{
    /// <summary>
    /// L'indicatore della lingua di input: la sigla della lingua in uso,
    /// con il menu di cambio lingua al clic. Gli stili sono quelli di
    /// Windows 7, Windows 8.1 e Windows 10/11 (quest'ultimo leggermente
    /// piu' grande della barra nativa).
    /// </summary>
    public sealed class InputLanguageBar : Button
    {
        /// <summary>Nascondi l'indicatore.</summary>
        public const int ModeHidden = 0;
        /// <summary>Sigla a due lettere, come la barra di Windows 7.</summary>
        public const int ModeWin7 = 1;
        /// <summary>Targhetta a due righe, come il selettore di Windows 8.1.</summary>
        public const int ModeWin81 = 2;
        /// <summary>Sigla a tre lettere, come Windows 10/11 (qui un po' piu' grande).</summary>
        public const int ModeWin10 = 3;

        public static readonly DependencyProperty ModeProperty =
            DependencyProperty.Register(nameof(Mode), typeof(int),
                typeof(InputLanguageBar),
                new PropertyMetadata(ModeWin7, OnModeChanged));

        /// <summary>Stile dell'indicatore: 0 nascosta, 1 Win7, 2 Win8.1, 3 Win10/11.</summary>
        public int Mode
        {
            get => (int)GetValue(ModeProperty);
            set => SetValue(ModeProperty, value);
        }

        /// <summary>
        /// Il menu lo apre il core (menu nativo di Windows 7): la finestra
        /// principale imposta qui il ponte. Riceve (x, y, voci) e ritorna
        /// l'indice scelto a partire da 1, oppure 0.
        /// </summary>
        public Func<int, int, string, int>? ContextMenuShower { get; set; }

        private readonly DispatcherTimer _watch;
        private CultureInfo? _current;
        private int _lastHklWord = -1;

        public InputLanguageBar()
        {
            Template = BuildTemplate();
            SnapsToDevicePixels = true;
            Focusable = false;
            Cursor = System.Windows.Input.Cursors.Arrow;
            VerticalAlignment = VerticalAlignment.Center;
            VerticalContentAlignment = VerticalAlignment.Center;

            _watch = new DispatcherTimer(DispatcherPriority.Background)
            {
                Interval = TimeSpan.FromMilliseconds(200),
            };
            _watch.Tick += (_, _) => RefreshCurrentLanguage();

            Loaded += (_, _) =>
            {
                RefreshCurrentLanguage();
                RebuildVisual();
                UpdateWatchState();
            };
            Unloaded += (_, _) => _watch.Stop();

            // La lingua puo' cambiare mentre l'utente lavora: il tick segue
            // il thread con il focus, come fa RetroBar con lo stesso timer.
        }

        private static void OnModeChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        {
            if (d is InputLanguageBar bar)
            {
                bar.RebuildVisual();
                bar.UpdateWatchState();
            }
        }

        private void UpdateWatchState()
        {
            if (Mode == ModeHidden || !IsLoaded)
            {
                _watch.Stop();
                return;
            }
            if (!_watch.IsEnabled)
            {
                RefreshCurrentLanguage();
                _watch.Start();
            }
        }

        /* ------------------------------------------------------------ */
        /*  Lingua corrente e lingue installate                          */
        /* ------------------------------------------------------------ */

        [StructLayout(LayoutKind.Sequential)]
        private struct GUITHREADINFO
        {
            public int cbSize;
            public uint flags;
            public IntPtr hwndActive;
            public IntPtr hwndFocus;
            public IntPtr hwndCapture;
            public IntPtr hwndMenuOwner;
            public IntPtr hwndMoveSize;
            public IntPtr hwndCaret;
            public RECT rcCaret;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct RECT
        {
            public int Left, Top, Right, Bottom;
        }

        [DllImport("user32.dll")]
        private static extern bool GetGUIThreadInfo(uint idThread, ref GUITHREADINFO info);

        [DllImport("user32.dll")]
        private static extern IntPtr GetKeyboardLayout(uint idThread);

        [DllImport("user32.dll")]
        private static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);

        [DllImport("user32.dll")]
        private static extern IntPtr GetForegroundWindow();

        [DllImport("user32.dll")]
        private static extern int GetKeyboardLayoutList(int size, [Out] IntPtr[]? layouts);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern IntPtr LoadKeyboardLayout(string keyboardLayoutId, uint flags);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern bool PostMessageW(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

        [DllImport("user32.dll")]
        private static extern bool GetCursorPos(out POINT point);

        [StructLayout(LayoutKind.Sequential)]
        private struct POINT
        {
            public int X, Y;
        }

        private const uint WM_INPUTLANGCHANGEREQUEST = 0x0050;
        private const uint KlfSubstituteOk = 0x0001;
        private const uint KlfActivate = 0x0001;
        private static readonly IntPtr HwndBroadcast = new(-1);

        /// <summary>
        /// Il thread con il FOCUS in questo momento: hwndFocus prima, poi
        /// hwndActive, poi la finestra in primo piano. E' la stessa catena
        /// di KeyboardLayoutHelper (ManagedShell), che RetroBar usa: alcune
        /// applicazioni tengono il focus reale su una finestra figlia di
        /// un altro thread rispetto alla finestra in primo piano.
        /// </summary>
        private static uint FocusedThreadId()
        {
            var info = new GUITHREADINFO();
            info.cbSize = Marshal.SizeOf(info);
            if (GetGUIThreadInfo(0, ref info))
            {
                IntPtr focused = info.hwndFocus != IntPtr.Zero
                    ? info.hwndFocus
                    : info.hwndActive;
                if (focused != IntPtr.Zero)
                {
                    uint threadId = GetWindowThreadProcessId(focused, out _);
                    if (threadId != 0)
                    {
                        return threadId;
                    }
                }
            }
            return GetWindowThreadProcessId(GetForegroundWindow(), out _);
        }

        /// <summary>
        /// La lingua del thread che ha il FOCUS adesso (non la nostra): e'
        /// il metodo di KeyboardLayoutHelper (ManagedShell) e del controllo
        /// InputLanguage di RetroBar.
        /// </summary>
        private void RefreshCurrentLanguage()
        {
            try
            {
                IntPtr hkl = GetKeyboardLayout(FocusedThreadId());
                int langWord = (int)(hkl.ToInt64() & 0xFFFF);
                if (langWord == _lastHklWord && _current != null)
                {
                    return;   /* niente cambiamenti: non toccare la UI */
                }
                _lastHklWord = langWord;
                _current = CultureInfo.GetCultureInfo(langWord);
                RebuildVisual();
            }
            catch (CultureNotFoundException)
            {
                /* Un HKL non standard (layout tascabile di terze parti):
                 * si mostra l'ultima lingua valida e si riprova al tick. */
            }
            catch (Exception)
            {
                /* Mai far cadere la barra per l'indicatore. */
            }
        }

        /// <summary>Le lingue di input installate (HKL), nell'ordine di sistema.</summary>
        private static IntPtr[] InstalledLayouts()
        {
            try
            {
                int count = GetKeyboardLayoutList(0, null);
                if (count <= 0)
                {
                    return Array.Empty<IntPtr>();
                }
                var layouts = new IntPtr[count];
                int read = GetKeyboardLayoutList(count, layouts);
                if (read <= 0)
                {
                    return Array.Empty<IntPtr>();
                }
                if (read < count)
                {
                    Array.Resize(ref layouts, read);
                }
                return layouts;
            }
            catch (Exception)
            {
                return Array.Empty<IntPtr>();
            }
        }

        private static CultureInfo CultureOf(IntPtr hkl)
            => CultureInfo.GetCultureInfo((int)(hkl.ToInt64() & 0xFFFF));

        /* ------------------------------------------------------------ */
        /*  Visuale                                                      */
        /* ------------------------------------------------------------ */

        private void RebuildVisual()
        {
            Visibility = Mode == ModeHidden ? Visibility.Collapsed : Visibility.Visible;
            if (Mode == ModeHidden)
            {
                return;
            }

            string two = (_current?.TwoLetterISOLanguageName ?? "--").ToUpperInvariant();
            string three = (_current?.ThreeLetterISOLanguageName ?? "---").ToUpperInvariant();

            switch (Mode)
            {
                case ModeWin81:
                {
                    /* La targhetta di Windows 8.1: sigla a tre lettere in
                     * alto, sigla della lingua a due sotto, tutto stretto. */
                    var stack = new StackPanel { Orientation = Orientation.Vertical };
                    var top = new TextBlock
                    {
                        Text = three,
                        FontSize = 11,
                        FontWeight = FontWeights.SemiBold,
                        HorizontalAlignment = HorizontalAlignment.Center,
                        Margin = new Thickness(0, 0, 0, -2),
                    };
                    top.SetResourceReference(TextBlock.ForegroundProperty,
                        "InputLanguageForeground");
                    top.SetResourceReference(TextBlock.FontFamilyProperty,
                        "GlobalFontFamily");
                    var bottom = new TextBlock
                    {
                        Text = two,
                        FontSize = 10,
                        FontWeight = FontWeights.Normal,
                        HorizontalAlignment = HorizontalAlignment.Center,
                        Margin = new Thickness(0, 0, 0, 1),
                    };
                    bottom.SetResourceReference(TextBlock.ForegroundProperty,
                        "InputLanguageForeground");
                    bottom.SetResourceReference(TextBlock.FontFamilyProperty,
                        "GlobalFontFamily");
                    stack.Children.Add(top);
                    stack.Children.Add(bottom);
                    Content = stack;
                    Padding = new Thickness(4, 1, 4, 1);
                    break;
                }
                case ModeWin10:
                {
                    /* La sigla di Windows 10/11 ("ENG"). La nativa resta
                     * piccola dentro l'area di notifica: qui la vogliamo
                     * leggermente piu' grande, come chiesto (14 contro i
                     * 12 del resto della barra). */
                    var text = new TextBlock
                    {
                        Text = three,
                        FontSize = 14,
                        FontWeight = FontWeights.Normal,
                    };
                    text.SetResourceReference(TextBlock.ForegroundProperty,
                        "InputLanguageForeground");
                    text.SetResourceReference(TextBlock.FontFamilyProperty,
                        "GlobalFontFamily");
                    Content = text;
                    Padding = new Thickness(6, 0, 6, 0);
                    break;
                }
                default:
                {
                    /* Windows 7: la sigla a due lettere con lo stile del
                     * tema (InputLanguage di Base.xaml + tema Win7). */
                    var text = new TextBlock
                    {
                        Text = two,
                    };
                    text.SetResourceReference(TextBlock.StyleProperty,
                        "InputLanguage");
                    Content = text;
                    Padding = new Thickness(4, 0, 4, 0);
                    break;
                }
            }
        }

        private static ControlTemplate BuildTemplate()
        {
            var border = new FrameworkElementFactory(typeof(Border), "Bd");
            border.SetValue(Border.BackgroundProperty, Brushes.Transparent);
            border.SetValue(Border.CornerRadiusProperty, new CornerRadius(2));

            var presenter = new FrameworkElementFactory(typeof(ContentPresenter), "Content");
            presenter.SetValue(ContentPresenter.VerticalAlignmentProperty,
                VerticalAlignment.Center);
            presenter.SetBinding(MarginProperty, new System.Windows.Data.Binding("Padding")
            {
                RelativeSource = new RelativeSource(RelativeSourceMode.TemplatedParent),
            });

            border.AppendChild(presenter);

            var template = new ControlTemplate(typeof(Button))
            {
                VisualTree = border,
            };

            var hover = new Trigger { Property = IsMouseOverProperty, Value = true };
            hover.Setters.Add(new Setter(Border.BackgroundProperty,
                new SolidColorBrush(Color.FromArgb(0x26, 0xFF, 0xFF, 0xFF)))
            {
                TargetName = "Bd",
            });
            var pressed = new Trigger { Property = IsPressedProperty, Value = true };
            pressed.Setters.Add(new Setter(Border.BackgroundProperty,
                new SolidColorBrush(Color.FromArgb(0x44, 0xFF, 0xFF, 0xFF)))
            {
                TargetName = "Bd",
            });
            template.Triggers.Add(hover);
            template.Triggers.Add(pressed);
            return template;
        }

        /* ------------------------------------------------------------ */
        /*  Menu di cambio lingua                                        */
        /* ------------------------------------------------------------ */

        protected override void OnClick()
        {
            base.OnClick();
            if (ContextMenuShower == null)
            {
                return;
            }

            var layouts = InstalledLayouts();
            if (layouts.Length == 0)
            {
                return;
            }

            if (!GetCursorPos(out POINT cursor))
            {
                cursor.X = 0;
                cursor.Y = 0;
            }

            /* Le voci sono i nomi delle lingue cosi' come Windows li mostra
             * ("Italiano (Italia)"); la lingua in uso porta il '*' di spunta
             * (mini-linguaggio di ShowContextMenuEx). */
            var items = new StringBuilder();
            foreach (IntPtr hkl in layouts)
            {
                if (items.Length > 0)
                {
                    items.Append('\n');
                }
                try
                {
                    CultureInfo culture = CultureOf(hkl);
                    if (_current != null && culture.LCID == _current.LCID)
                    {
                        items.Append('*');
                    }
                    items.Append(culture.DisplayName);
                }
                catch (CultureNotFoundException)
                {
                    items.Append('?');
                }
            }

            int chosen = ContextMenuShower(cursor.X, cursor.Y, items.ToString());
            if (chosen < 1 || chosen > layouts.Length)
            {
                return;   /* menu chiuso senza scelta */
            }

            /* Cambio lingua come fa KeyboardLayoutHelper.SetKeyboardLayout:
             * si carica/attiva il layout della lingua scelta
             * (KLF_SUBSTITUTE_OK | KLF_ACTIVATE) e poi si manda in broadcast
             * WM_INPUTLANGCHANGEREQUEST, lo stesso messaggio di Alt+Shift. */
            try
            {
                int langWord = (int)(layouts[chosen - 1].ToInt64() & 0xFFFF);
                IntPtr hkl = LoadKeyboardLayout(langWord.ToString("X8"),
                    KlfSubstituteOk | KlfActivate);
                PostMessageW(HwndBroadcast, WM_INPUTLANGCHANGEREQUEST,
                    IntPtr.Zero, hkl);
                RefreshCurrentLanguage();
            }
            catch (Exception)
            {
                /* Mai far cadere la barra per un cambio lingua rifiutato. */
            }
        }
    }
}
