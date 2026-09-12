// Win7Taskbar - risorse grafiche della cornice delle anteprime
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
// PERCHE' QUESTO FILE ESISTE
//
// La cornice dell'anteprima e' fatta dai PNG forniti dall'utente (angoli 21x21
// e bordi da stirare) e il pulsante di chiusura dai tre stati di
// close_button.png del progetto ViGlance (lee-soft/ViGlance, GPL-3.0). Stanno
// incorporati in base64 come le altre icone (Utilities/EmbeddedAssets.cs): un
// pacchetto estratto senza la sottocartella giusta non puo' romperli e la
// decodifica avviene UNA sola volta, con l'istanza congelata (usabile da piu'
// anteprime insieme).
//
// Le dimensioni NON sono decorative: il template a griglia 3x3 di
// Themes/Overrides.xaml (TaskPreviewFrameVista) le usa per lasciare gli angoli
// alla loro misura esatta e stirare solo i bordi.
// ============================================================================

using System;
using System.IO;
using System.Windows.Media.Imaging;

namespace Win7Taskbar.Utilities
{
    /// <summary>
    /// Cornice e pulsante di chiusura dell'anteprima, incorporati nel binario.
    /// </summary>
    public static class PreviewAssets
    {
        /// <summary>14x14 px.</summary>
        private const string PreviewCloseNormalPngBase64 =
            "iVBORw0KGgoAAAANSUhEUgAAAA4AAAAOCAYAAAAfSC3RAAABhklEQVR42pXSu04CQRSA4X/ZZVmuWRSNEgslJB" +
            "JiIWijYGGstCLBxE5K3kATHoAE3sASbDTRwsonABvxEsB4CdoQQowFBYUQ1rUwLJcKpzuZ882ZOXMEXdc53dpq" +
            "5J4b80ywEgFvCVgX8tHo7dxhIrS5ETJNAos39zTzuZKw4/Hpl9d52pW7SRyOlTDx3UMkAF3T0LpdLl8+KdRbAE" +
            "QWVICROL48i65pAH/wp9dD63xTqLe4OD8BYP8gCTASxxZd/PR6A/j19MjH1RlBh4/9gyQX5ycG6KNgp8nrVZEl" +
            "xQbASEMi7XeCnaZRbRhF2u8jb5UABFHEJMsACCYRgFqtZiQJpqF9URxUFGULZruTotNP1TxDJn1koEz6iKp5hq" +
            "LTj9nuRJQtQ1BRkF0qZVQDHaeyHKeyBi6jIrtUREUZXFWy2lHcHsLoRnLYoRsH9GPF7UGy2oehDWVqmtgUxMY+" +
            "fDyWrH9dlRIBb6lSa6ytbu9NNDkPlTcSAW9JyEejALe558baf4b8F1/egSzJiuNOAAAAAElFTkSuQmCC";

        /// <summary>14x14 px.</summary>
        private const string PreviewCloseHoverPngBase64 =
            "iVBORw0KGgoAAAANSUhEUgAAAA4AAAAOCAYAAAAfSC3RAAABnUlEQVR42pWSMUtbYRSGn+9eEwOXqJGrpEFFEm" +
            "5JwC3BQROCTXFxcLDYwUKQQkr/gAF/gBL/gLTgoE6Ci5OQQehwl3DTwcVSuVCCmEGkgqSQCJ4O0ZtGaUnP9sJ5" +
            "vvc9L58SEfYzGWf322WSHiYfj1SBlNpLp52xwsdk9tVsLxxfTmwuPm9XVc6MStk+Qn587wlUky+Zn12kD4C7Fv" +
            "xqsOO4lM/rAMxbLwC69PtUrL0LbVBaTe4bt5TP6xwefALgzdsPAF16NTGK1mq2rXNmVOqlopzGNNmcy8jSckGe" +
            "ztJyQTbnMnIa06ReKkrOjIr2Z/6Fmk3q6sxze3RKXZ2xULO7bu37Wwmu6/6zJA1ABfzoQwbHVhZnJEFpY81bKG" +
            "2s4YwkOLay6EMGKuDvgHrIwDdhUhmwPKi4vkVxfcuDKwMWvgkTPWR0ourDQfyxMDM/r73lmeC198Cj9ofC6MPB" +
            "DugLDxKYGmcFWOHmIaQOz/Q4vvBgG8zHI9Wv7k1y+vU7UE2UaoB2293EfRARA6Sfil0jH49U1V46DfDfn/w3M2" +
            "Gc5P4I7F8AAAAASUVORK5CYII=";

        /// <summary>14x14 px.</summary>
        private const string PreviewClosePressedPngBase64 =
            "iVBORw0KGgoAAAANSUhEUgAAAA4AAAAOCAYAAAAfSC3RAAABxElEQVR42o3Sv2sTYRzH8fflrrkLd+31Ei6BK7" +
            "UKkmhiBAdBJKO7S7DgD0QcMri30D+g0O4dupuh0MVdcImj1WIbSyCDChdaSQIS27vLc3cOsUkjKH22L3xez/N8" +
            "n+crvXj8MhbfT3C/HHKZ5dwsoVzJIT3M342fvHpK+U6e5MwMiqKQkKSpcBTHCCEIhkM+f2xR33qNMuh1yRcWOH" +
            "j7Bl3TSCYVZDkxBcMwIggEvzyP/P0HDHpdFIBIhER+wPuTmKNeDEDJlgE4/BECcCMtcdscEolR/QcKQt/nqKew" +
            "u7MNQHW5BjBVlzSfSIgJdPf32avXsQoVqss1dne2x+AcWW6TvXcNDGsBgKlmcq0Gltscn3YR5VqNqb6Vfz17u9" +
            "3+77eMTtQkMCWOyxX6TpGN9ZVxYGN9hb5T5LhcAVMaZc+hqqsYtkk/M0Gra5usrm1OcKaIYZuoujq5ampex1qy" +
            "WUy44/A1tTPeAGBRcbGWbFLz+gTq6Tmy1x2ups7Q1K/IiYuT840wivF8wemZg56eG8FZO4vbOeVetYaqhiQ1D1" +
            "ke/DU5BoGn4fsyBx86zNpZpGePnsc/PzUZ9LqXGnIjnUG/VeA3lCml9GTco10AAAAASUVORK5CYII=";

        /// <summary>21x21 px.</summary>
        private const string PreviewFrameCornerTopLeftPngBase64 =
            "iVBORw0KGgoAAAANSUhEUgAAABUAAAAVCAYAAACpF6WWAAABSklEQVR42q2TUU7DMBBEZ2Z3y91oexVoy00Kas" +
            "vBkKjgLJgP2yENSZoULI1sK/HTrGdN/N/gr8WtgNkfZ/zPW6Ccs+cfYH1rXoNOAXAqdMhdFzYEp09w2gcblc9w" +
            "2ZaKCIAkrcwCwLF7G1IFiqRIWmsmSXEGUACqI2vLzEx5mCT5lcT73BlJkxSWh7u7328OH/WQTwikAr0D9IiI1e" +
            "702RxkaqAYKb0LdEnh7hERi+X2eCYSSIAkXh5XQAs6pWwvDiMi7pbb4zuRIBLPm2Xpel5Ah1wSgJH0KndfVOBh" +
            "t0ZKqQGyyAeeoNqJ19LNLNZPr+fsEEhftXRB0gUUA8+uAQJwSe7uC5Yq9+X+QIHKquApQdXm9tXu9JYrZY0bKj" +
            "CTIFN2PdLs1WUTUm2b/UMORqVcFaAoqOO0LyRdvB4ABJFq0voJBxQgAiS+AY3sJkAsHJdYAAAAAElFTkSuQmCC";

        /// <summary>21x21 px.</summary>
        private const string PreviewFrameCornerTopRightPngBase64 =
            "iVBORw0KGgoAAAANSUhEUgAAABUAAAAVCAYAAACpF6WWAAAB4ElEQVR42qWTTY7UMBSEq56dSCw5xUjATRjYcw" +
            "H+xI8QrDkBDDAbBGtOwNAHYcGCU/Qwq1Fiv2JhZ+SOEqZbWLJenDify+UyARC7TfjPxgXoUtOhUDsAshc8Vqj2" +
            "VMp94HFmgVaAPAQeZ9vXSv0XXEvQ0EzU7Jmz95i9X1QdAXTNj1P3GVwL8NXDi2bWA5Akb6o38HaRNbU7VoSu62" +
            "4CCGYWSBrJNmY22yKvyTYBIPZ9f0OS55xdUp5q01mVTtWvix0fvP2mabT58OQo55xSSsnds7snSUlSBpAbqM/8" +
            "30lLNKp8FXH86vNvATh7/+gopTQAMHcnAFbFuVHmC6dPAOJ2uxUAPPu0gSRIgED8OHl8K6U0pJRGdx+r4lTBa6" +
            "pVoOfnRbqK4ucfN3BpAt8ZhuEy5zyBRwCpgeZF6J+LCxWFugKTxNOTMwjE93cPb4/jeJlzTpKGqnacgVtvZcEC" +
            "pk5aMcUFI0AI9998/WVmPcnQXOtQK2exIwCahQALATQDzQCWOacvj0GWWTHG3sw6kpFkbKC2lF0LISAEQzCD1Q" +
            "6yBq7Ue6+//CTZVbVrwCuwGQmjwUIBkoSRIIDTF3dBlnM0s1ZlmFmwc+MMRhQDC5AsY5E1eGWBCoxVrc083dn+" +
            "X+prIGtaaTTRAAAAAElFTkSuQmCC";

        /// <summary>21x21 px.</summary>
        private const string PreviewFrameCornerBottomLeftPngBase64 =
            "iVBORw0KGgoAAAANSUhEUgAAABUAAAAVCAYAAACpF6WWAAABJ0lEQVR42q2VPU7EMBBGn2NnORHiEPRbRHCDRd" +
            "xgCx+BKyBtkZ6aAkTJjYhp7NVkNONEiJFGdvzzxmN/dgIwAgEYgAik2paAQ/Ub4DDl+Rvg+fGeniWgVKi0Ikrp" +
            "q07PBgOmIQVYJOvl9W03tBgBFhnkcj7essOGjXSLgDe/rtYbPBiBLGArfy7n4507snqoJz4YCkhCCU0F1/qU56" +
            "/GfnpYq6FBG7B5qu2jAq8CTHn+lLBThUuoBFurld4CjFOeP/T+BZG6hm6BR5FJmvL87kGDgmpwUrBR9UUgRnWb" +
            "glF6dVc1sTOhF6wrw6iAdOpWm3lRopPeHrh7SWInNe+7bICX+F/72INa29ADy5Wv0t8zEeeJ1K/bIiW193C8v4" +
            "OpU/6wYvcd/gVODH8va+cgJAAAAABJRU5ErkJggg==";

        /// <summary>21x21 px.</summary>
        private const string PreviewFrameCornerBottomRightPngBase64 =
            "iVBORw0KGgoAAAANSUhEUgAAABUAAAAVCAYAAACpF6WWAAABJUlEQVR42o2U7U3EMAyG3ztlKCSEBEiIDW6c24" +
            "GPDTjdBh2CKZiCH0cb+/6Y4rw4TitZqSr3yZM4zu7z61uRPC8fEwDgfDzcAPixuLj3BcAMoFpISYnto0HAjWve" +
            "PqO8miVBhYA8CfZbFM/Hw20AkMgSALrLf2sthUKT7UCBDi3vDFQdUJL91fLP8DR54L2v6gZjNMt/P01s+GDHpQ" +
            "aRGWthmAEfDegjMmaw7WkLewpgS8dUAqAC0B2AZ1eISrDZjfNgstW6WKsJJYxse8Val3+xD3UA9kAuVtNd3lQ6" +
            "lV5orFmR2FTIpCbHKe2sX1Ol5BpMIDRx//ATVIMfZaPh34ViR4VvnhrYy2AvG9MlSGRA2pYM99DorhzBENkWt9" +
            "QeWDfYNfArLptVJCYKwj0AAAAASUVORK5CYII=";

        /// <summary>4x21 px.</summary>
        private const string PreviewFrameTopPngBase64 =
            "iVBORw0KGgoAAAANSUhEUgAAAAQAAAAVCAYAAABhe09AAAAAU0lEQVR42p3OwQ2AUAgD0JZiMGzoMi7jgH8Evf" +
            "5ylBsvbVICILbLfxATNCEnHBPKoKpOg+7uHXjdz2sJhe0C11qeYNhQpCSHVDrErIQGkA7+AfgAK9oHHceETDUA" +
            "AAAASUVORK5CYII=";

        /// <summary>4x21 px.</summary>
        private const string PreviewFrameBottomPngBase64 =
            "iVBORw0KGgoAAAANSUhEUgAAAAQAAAAVCAYAAABhe09AAAAAQ0lEQVR42mM8ef/rfwYkwILCY2BgYGIgJIChhY" +
            "XhP8lapizZil8LIwMDgx+qLQwMv9AFfhIU+EW6CgyB3wQF/iILAADfzhPgzIqOMwAAAABJRU5ErkJggg==";

        /// <summary>21x4 px.</summary>
        private const string PreviewFrameLeftPngBase64 =
            "iVBORw0KGgoAAAANSUhEUgAAABUAAAAECAYAAABhnXSoAAAAgklEQVR42o2OMQ4CMQwEx3ZCBLyMgo9Q8g0ewH" +
            "/4Es3p4sQURCeKgLA00ha7IwuQAR0YkAY7oAAl53wspRzO1/vDVLhdTqgqZkZKCTXF1BARRN7j2clHjlqrR8Qq" +
            "lulAsz0N8IC1CnggtG31TboJB93dHVEAlm7TZvCfdPr9c2k/iy/5UySlmofvaAAAAABJRU5ErkJggg==";

        /// <summary>21x4 px.</summary>
        private const string PreviewFrameRightPngBase64 =
            "iVBORw0KGgoAAAANSUhEUgAAABUAAAAECAYAAABhnXSoAAAAf0lEQVR42o2MsQ0CQQwEx/yVRjE0QBmkFEJCWw" +
            "SvAz9rm4DjJYSEfqSJdrR2X1RVUFVEBhmJJCKCzOR4vhJZXE6Hvbt3SR14AD58AgLiY7t1AUBBUVBG0ShrMEFa" +
            "wyZw90VSvMsVG37R5sXWFfvZwXYASBKQ47T4Q5s92IhtDV8/kUyowhjp2wAAAABJRU5ErkJggg==";

        private static BitmapImage? _previewCloseNormal;
        private static BitmapImage? _previewCloseHover;
        private static BitmapImage? _previewClosePressed;
        private static BitmapImage? _previewFrameCornerTopLeft;
        private static BitmapImage? _previewFrameCornerTopRight;
        private static BitmapImage? _previewFrameCornerBottomLeft;
        private static BitmapImage? _previewFrameCornerBottomRight;
        private static BitmapImage? _previewFrameTop;
        private static BitmapImage? _previewFrameBottom;
        private static BitmapImage? _previewFrameLeft;
        private static BitmapImage? _previewFrameRight;

        /// <summary>14x14 px, decodificata una volta sola.</summary>
        public static BitmapImage PreviewCloseNormal => _previewCloseNormal ??= Decode(PreviewCloseNormalPngBase64);

        /// <summary>14x14 px, decodificata una volta sola.</summary>
        public static BitmapImage PreviewCloseHover => _previewCloseHover ??= Decode(PreviewCloseHoverPngBase64);

        /// <summary>14x14 px, decodificata una volta sola.</summary>
        public static BitmapImage PreviewClosePressed => _previewClosePressed ??= Decode(PreviewClosePressedPngBase64);

        /// <summary>21x21 px, decodificata una volta sola.</summary>
        public static BitmapImage PreviewFrameCornerTopLeft => _previewFrameCornerTopLeft ??= Decode(PreviewFrameCornerTopLeftPngBase64);

        /// <summary>21x21 px, decodificata una volta sola.</summary>
        public static BitmapImage PreviewFrameCornerTopRight => _previewFrameCornerTopRight ??= Decode(PreviewFrameCornerTopRightPngBase64);

        /// <summary>21x21 px, decodificata una volta sola.</summary>
        public static BitmapImage PreviewFrameCornerBottomLeft => _previewFrameCornerBottomLeft ??= Decode(PreviewFrameCornerBottomLeftPngBase64);

        /// <summary>21x21 px, decodificata una volta sola.</summary>
        public static BitmapImage PreviewFrameCornerBottomRight => _previewFrameCornerBottomRight ??= Decode(PreviewFrameCornerBottomRightPngBase64);

        /// <summary>4x21 px, decodificata una volta sola.</summary>
        public static BitmapImage PreviewFrameTop => _previewFrameTop ??= Decode(PreviewFrameTopPngBase64);

        /// <summary>4x21 px, decodificata una volta sola.</summary>
        public static BitmapImage PreviewFrameBottom => _previewFrameBottom ??= Decode(PreviewFrameBottomPngBase64);

        /// <summary>21x4 px, decodificata una volta sola.</summary>
        public static BitmapImage PreviewFrameLeft => _previewFrameLeft ??= Decode(PreviewFrameLeftPngBase64);

        /// <summary>21x4 px, decodificata una volta sola.</summary>
        public static BitmapImage PreviewFrameRight => _previewFrameRight ??= Decode(PreviewFrameRightPngBase64);

        /// <summary>
        /// Decodifica un PNG base64 in un BitmapImage congelato (stesso schema di
        /// <see cref="EmbeddedAssets"/>).
        /// </summary>
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
