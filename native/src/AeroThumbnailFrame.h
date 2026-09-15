// Win7Taskbar - cornice 9-slice delle anteprime (Aero thumbnail frame)
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
//
// v3.9: disegno GDI della cornice delle anteprime con la tecnica del
// 9-slice. Le otto porzioni del bordo vivono nella cartella Resources/
// accanto all'eseguibile (top_left, top_center, top_right, mid_left,
// mid_right, bottom_left, bottom_center, bottom_right; lo slot centrale
// NON esiste: resta trasparente, li' ci vive il contenuto vero della
// thumbnail o il fill di ripiego del chiamante).
//
// Contratto: DrawAeroThumbnailFrame9Slice ritorna false quando il
// rettangolo di destinazione non contiene la somma dei bordi o quando
// anche una sola immagine non si carica; in quel caso il chiamante
// disegna il rettangolo tradizionale con
// DrawAeroThumbnailFrameFallback, cosi' a schermo c'e' comunque
// qualcosa. Nessun hook, nessuna dipendenza esterna: solo WIC (decodifica
// PNG, gia' in uso nel progetto) e GDI (AlphaBlend via DrawBitmapScaled).

#pragma once
#include <windows.h>

namespace w7t {

/* Spessori del bordo, in pixel, riferiti al bordo sorgente originale
 * (DWMBorder.png, 236x166): la fascia superiore ospita il chrome col
 * titolo (38 px), i fianchi e il fondo sono il bordo sottile. */
constexpr int kAeroFrameLeft   = 17;
constexpr int kAeroFrameRight  = 17;
constexpr int kAeroFrameTop    = 38;
constexpr int kAeroFrameBottom = 19;

/* Disegna la cornice a 9 parti in `dst`.
 *
 * - i 4 angoli sono disegnati 1:1, senza scaling, ancorati agli angoli;
 * - i 4 bordi sono stirati SOLO nella direzione lungo cui corrono
 *   (top_center/bottom_center in orizzontale, mid_left/mid_right in
 *   verticale), mantenendo lo spessore fisso nell'altra direzione;
 * - il centro NON viene toccato: li' ci va il contenuto della thumbnail.
 *
 * `accent` == 0: le slice sono disegnate come sono su disco.
 * `accent` != 0: le slice (che nascono come maschera grigia, come
 * DWMBorder.png) vengono tinte con quel colore mantenendo l'alfa:
 * e' l'equivalente GDI di cio' che fa il percorso WPF con
 * DwmPreviewAccentBrush + opacity mask.
 *
 * Ritorna false se lo spazio e' insufficiente (larghezza < left+right
 * oppure altezza < top+bottom) o se le immagini non si sono caricate:
 * il chiamante ripiega sul rettangolo tradizionale. */
bool DrawAeroThumbnailFrame9Slice(HDC hdc, const RECT& dst,
                                  COLORREF accent = 0);

/* Ripiego "rettangolo tradizionale": fill pieno scuro + bordo semplice.
 * Ha senso solo quando DrawAeroThumbnailFrame9Slice ha ritornato false. */
void DrawAeroThumbnailFrameFallback(HDC hdc, const RECT& dst);

} /* namespace w7t */
