// Win7Taskbar - sfondi dei pulsanti della Superbar
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
// v2.52: le due immagini dei pulsanti fornite dall'utente (60x40, con alpha),
// incorporate in base64 come tutte le altre risorse grafiche del progetto.
//
//   Hover  (image-1) : pulsante blu pieno lucido - passaggio del mouse e
//                      pulsanti in esecuzione della barra
//   Active (image-2) : pulsante azzurro chiaro - finestra in primo piano
//
// Si usano a TRE PEZZI (bordo sinistro 6 px, centro stirato, bordo destro
// 6 px): vedi i template TaskButtonFrameHover / TaskButtonFrameActive in
// Themes/Overrides.xaml. Non riguardano il pulsante Start ne' le icone
// dell'area di notifica, che hanno i loro sfondi.
// ============================================================================

using System;
using System.IO;
using System.Windows.Media.Imaging;

namespace Win7Taskbar.Utilities
{
    /// <summary>
    /// Sfondi dei pulsanti della Superbar (applicazioni), incorporati nel binario.
    /// </summary>
    public static class TaskButtonAssets
    {
        /// <summary>60x40 px, con alpha.</summary>
        private const string HoverPngBase64 =
            "iVBORw0KGgoAAAANSUhEUgAAADwAAAAoCAYAAACiu5n/AAAFYklEQVR42uWZzW4bNxDH/xyuPmHUveXQXnJogC" +
            "LoG+QS9JoHyQsU6BPlEKBo7n2Fon2Dwk2BAkkNOY4kr3bJmelBw82YWbtfSWMrAwja5VISf/zPDIdUALAE8A2A" +
            "r+36XZi6FwBI1aZVm4y819fXtf3V8xbAzwC+CwC+ffTo0edPnz79dD6ffwlg+i8B4QcdQhBVrSHYPasHxdW1Ah" +
            "BV5Wv6eUgemzwiyuv1evX48eP05MmTkwDgh7Ozs9+Pj4/vAVhb5+BgaOSLcEWbHwBUNVWDZADJ3hlAtvbk7n2b" +
            "uPtc4GOMzMwyNkHl92OMyswSYyQR+ez09HR+586dSQDwo4gkAGf2I1QB++uroMUAazdlIhIRqQELWJkALfdElF" +
            "3/4TNExMzMYyrbBIyFhgJAjDGKyFcxxtDYwJchhN9EREMIsYL00EMMhhBUROoJYAAwSAEgIpJrpdx7PQlc9WcD" +
            "zQaUK1BhZjEl1VSv8wJyzomICEDfDNKJdKZSUTiMKDwkIRefA2j5kQLrlCuqMBEVKHZKJeunfiJUVZiZDYwr92" +
            "UDLdCXlLXxaSVUblxD52I2EFEQEQ+t17g2X5GFPVQ2xfkKdUv/TEQFlA2GR5JVgR5AXSIEAG2apihcklpu3AA9" +
            "MESEKpfWMdeu4oXrCTFFfUwKESUR0RH3FlVNBnEpZk1JrbO5QbKDK8DIOdcrSG7c4Hb2INo9jbi0B5ex7OziFx" +
            "aTUsVertrYBs5jHmGxmZk5Wdb3mRwV3HXGALixD0+KwrQfcbgGltwkhcqdL8E61fPIdTLXrV2/XO9UdZdzTk3T" +
            "JK/cfyiGpHEDaIkoioha/NJI7I4tUyCiOlFpBXxpOSlLjMH6dfdCVbcGOwCmlN5V9TcAC4DOK0tEEJHolByD15" +
            "GCQ0cqIPFu61TNAM5jjGsr/96bWc5A42JiV9Rz6qKCHVui4Nz4rZrWILUqJF6r6rmpqW8+/v4sxogCnGz22xCC" +
            "X4PHqi2qqiwyiODWv6HMc+snA+hjjKcppTPvrv+3NQB6S/uJmYNlRRpJVB5SPSSAEGPMzBzc0qQG+1pETlV1vb" +
            "/9YBa8womZW+wDLDjAWuHapQu0GIxa4S455/OmaV52XdfiZpgCCEXhbC6nzOwTFWKMZBNRuzDqbA0gqOqq7/s/" +
            "VLX7wIrWRgCoACcAO1PXq0o26FBt6GulNca47fv+xXQ67XAzbQBOAJKq9tVDVLE7tgYHW85exhjb6XSKG257hQ" +
            "12V5WTwQpwsuIb/r5pGu77/hUznwNA27Y3mnQ2mwUAcYhhVc22LMG2iRpCCDnnS2twzhmqut3tdq9sCbotFgtw" +
            "KnHcNE2wUo4MGvUuaTKZrNq27XD7jDxwVtXc9/swnk6n5OrYMJlMtO/7nplfp5QEt9MuKzydTvu+7wkADNwnqD" +
            "bn3C6XS72lsLB9whvgrut4pGyEiGy3220CgO12e1t5sVgsIoCGrOioj1N1NpslZl7bieatNyIaXDoDSLPZjHe7" +
            "nc7n8wAAFxcXnR3NHITZDnAA7tu2ZVtPVUTSh9zRvM/NElWn/LJYLPKBwkYAkdwZsC4WC95sNozDtFCAFUBeLp" +
            "dywLDDZqgUGGmz2QgO2waF4U4pDt32ChMR4+0TyYNUuZxmfCwKBwJAMcaPBXh/7MHM/dHRUTxUyKOjowXsoJ8A" +
            "8GazeZFz/sIeHJRNJpNjZn6wWq1+AbANAL6+e/cuPXv2TO7fv/8JgMU1aX14xRipOtIlOzEph/n+Ovo2++8q4s" +
            "1/0eUPPN8P1XeFqu3v2vnJycmvDx8+TM+fPw/lRx4AuAfguAYbedE76hPGJrJqA8bPxP+prQD8BOD7PwEWs7vj" +
            "5sxuggAAAABJRU5ErkJggg==";

        /// <summary>60x40 px, con alpha.</summary>
        private const string ActivePngBase64 =
            "iVBORw0KGgoAAAANSUhEUgAAADwAAAAoCAYAAACiu5n/AAAEJUlEQVR42uWYv47kRBDGv6r2sivECYEEORIiIC" +
            "IkICMj5xE2JUPafLSvMC9ARIAg5x2AByA84OCOPd3uzv4Zd1cRTLVV7mnPeobhtGNKstzu9tjz66+qutoE4E0A" +
            "XwP43Nr7MHUHAEjRp0WfVM5le1PfQ+O3AH4G8F0D4Kvz8/MPz87OPmHmJzsTqmoFeKi9qU+JSO15g/cMjFWvY4" +
            "zXs9ns79ls9gUB+DHG+CkzP6v8EEV7zBgqLxcHkgo1U2Ws66+o32sTkYhI6TkoJvDdtm0/OD4+/qYB8E4I4S1V" +
            "jSMAt50E8S6tqjIEYLClS6ICrkSUMqR5glRCB8ysACAiz4+Ojj4G8Ebj/mTaAa53DxFp4eLlzEt+jyk6FHelwm" +
            "LPTAVk93xmFoPrxkSEive3NeAy7tYAM5iL2+p1xSU3qT2YuMxta+PKzCoi6txaNuSOWAIPAfUghvo3xG/pbqk4" +
            "1xQWm5RycpSZuwnwio5IlrFxnUsA7DgfitGxMaxFPGIT8CY18z0O0ru/fx8q/0kysDjg8ABUz60HXBgDCqeBJC" +
            "ROTfFqmpIZKNqR27JDbZAa+zEA3AMIRMSqSgXQGtQArG7I0lXVimXKg96JyBJAy8ytgf8bEwDigZcAWFUDAFJV" +
            "egBmTOauufRQwkoAbgDciMi9V3Dl4Xup/lbAptYSAAFo7BxGJKQxSauXgAqVWwALAFdW/v2XpjC4aLN+x8zBCh" +
            "A2YNpC1bWlyyWXTl1mTiJyDeCVwSpej3XArV0sRSQYLBNRBuYaYF66XGzVYtwf98z8QkRe7pBw9maNubIAuMvK" +
            "8iprdPCF0mOSFlxRcAXgBYBXe4rFvSgsPoatJDsCQMxMIsI193a1ai1hvRSRZ68hNrcx8grfW9yKnSOAYLBZ7d" +
            "4aa4r1FGbmCxH50zzmsRn3gJnZAwZLNFwcQ4XJJYCnKaXHCAoi6oBbAOIWd3K1Krls7VX27n0L4KmqXuFxG3mF" +
            "k4hEAMlUFqduKtTOE5AA/K6qz3EYRgBCB8zMUUTYYMUydXZjseSVY/xSVX8z78ChAbe2A4mrjwTMIgIDD9m9DT" +
            "4B+ENVL3F41gPOn1fgMi9bJg6WfW9SSn+52vuggcs6l61oyP0LKwXVJaxDMy4V7u2OXJa+UtXblBIdMGwGbhq3" +
            "mVYnfW5fqurywEHXXDoDewUJwMI25wHTMK4Bd7AAEhExpmOUXVocMFtNjQkpu6Zw/qYUXCw3mJ51MawOuJ2gsl" +
            "5hbop97FRhs4UeMDNPFti+wrKP1bwXnqTRakNMTeHjUzdqHOjU45dgy8//DjillBbM/B4zX0yRVETejzHeAlg0" +
            "AH6dz+ffnp6efnlycvJkm9kaeR5qY4sxVNqjrW3b6/l8/gOAX/LHuc8AfATgbffyoYP3dE8JO3ZydrELAD8B+P" +
            "4fVo3EwQYTnRgAAAAASUVORK5CYII=";

        /// <summary>
        /// 60x40 px: tile arancione fornita per i pulsanti che richiedono
        /// attenzione (FlashWindowEx), mantenuta distinta dagli stati blu.
        /// </summary>
        private const string NotificationPngBase64 =
            "iVBORw0KGgoAAAANSUhEUgAAADwAAAAoCAYAAACiu5n/AAAA60lEQVR42uXa0UaDARyG8e/onZGMjN1FF7KL6WamScREZIzExMgYiUhkjJFFRIwd7KSj9X2jg3nu4Pn+PP/z3/lbFOX1u8WuDhX/2N3vaS3ao6u3OivR2466yngI3rTVEbw+UUfwT0sdwd/H6gj+OlJH8GdTHcEfDXUEL6OO4EXUETyPOoLfo47gt6gj+DXqCH6JOoKfo47gp6gjeBZ1BE+jjuDHqCN4EnUEP0QdweOoI/g+6gi+izqCR1FH8DDqCL6NOoJvoo7g66gjeBB1BF9FHcGXUUfwRdQRfB51BPeiDuA6dLDkqcts6Q8hSTT4L7jt7QAAAABJRU5ErkJggg==";

        private static BitmapImage? _hover;
        private static BitmapImage? _active;
        private static BitmapImage? _notification;

        /// <summary>Sfondo "hover", 60x40 px, decodificato una volta sola.</summary>
        public static BitmapImage Hover => _hover ??= Decode(HoverPngBase64);

        /// <summary>Sfondo "active", 60x40 px, decodificato una volta sola.</summary>
        public static BitmapImage Active => _active ??= Decode(ActivePngBase64);

        /// <summary>Sfondo notifica/attenzione, 60x40 px.</summary>
        public static BitmapImage Notification =>
            _notification ??= Decode(NotificationPngBase64);

        /// <summary>Decodifica un PNG base64 in un BitmapImage congelato.</summary>
        private static BitmapImage Decode(string base64)
        {
            byte[] bytes = Convert.FromBase64String(base64);
            using var stream = new MemoryStream(bytes, writable: false);

            var bitmap = new BitmapImage();
            bitmap.BeginInit();
            bitmap.CacheOption = BitmapCacheOption.OnLoad;
            bitmap.StreamSource = stream;
            bitmap.EndInit();
            bitmap.Freeze();
            return bitmap;
        }
    }
}
