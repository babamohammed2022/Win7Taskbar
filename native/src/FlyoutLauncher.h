/*
 * Win7Taskbar - Core nativo - Apertura dei riquadri (flyout) di sistema
 * Copyright (c) 2026 Win7Taskbar contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Questo file orchestra due vie diverse:
 *   1. i flyout immersivi di Windows 10/11 (rete, orologio, batteria,
 *      volume) -> ImmersiveFlyouts, adattato da ExplorerPatcher
 *      (valinet, GPL-2.0-or-later);
 *   2. il calendario classico Aero Clock di Vista/7/8 -> logica equivalente
 *      a RetroBar/Utilities/ClockFlyoutLauncher.cs (dremin, Apache-2.0),
 *      reimplementata in C++ nativo.
 *
 * Vedi CREDITS.txt e THIRD-PARTY-NOTICES.md per l'attribuzione completa.
 */

#ifndef WIN7TASKBAR_FLYOUTLAUNCHER_H
#define WIN7TASKBAR_FLYOUTLAUNCHER_H

#include "Common.h"
#include "ImmersiveFlyouts.h"

namespace w7t {

class FlyoutLauncher {
public:
    /**
     * Mostra il calendario/orologio vero di Windows (Aero Clock).
     *
     * @param taskbarHwnd la NOSTRA finestra della barra: serve sia come
     *        proprietario del riquadro sia per calcolare dove aprirlo.
     * @return W7T_OK se il riquadro e' stato mostrato, altrimenti un errore.
     */
    static int32_t ShowClockFlyout(HWND taskbarHwnd);

    /**
     * Mostra il vero riquadro del volume di Windows 10/11, quello con il
     * cursore e i controlli di riproduzione.
     */
    static int32_t ShowVolumeFlyout(HWND taskbarHwnd);

    /**
     * Via generica: mostra o nasconde uno qualsiasi dei quattro riquadri
     * (rete, orologio, batteria, volume). E' l'unica funzione che il livello
     * pubblico esporta per i nuovi flyout: con action = Hide il parametro
     * taskbarHwnd e' ignorato.
     */
    static int32_t InvokeFlyout(FlyoutKind kind, FlyoutAction action, HWND taskbarHwnd);

    /**
     * Come InvokeFlyout, ma l'ancora e' il RETTANGOLO dell'icona (pixel
     * fisici, coordinate schermo), non la finestra della barra.
     *
     * v2.61: il flyout di un'icona della tray va ancorato alla posizione
     * ATTUALE dell'icona, ricalcolata a ogni clic (il frontend riporta il
     * rettangolo reale con W7T_SetIconRect: layout, DPI, monitor, apertura
     * dell'overflow, riavvio di Explorer). Passare il rettangolo della barra
     * faceva comparire il riquadro nella posizione sbagliata appena la barra
     * si spostava o cambiava monitor.
     */
    static int32_t InvokeFlyoutAt(FlyoutKind kind, FlyoutAction action,
                                  const RECT& anchorRect);

    /** Riquadro del volume ancorato al rettangolo dell'icona. */
    static int32_t ShowVolumeFlyoutAt(const RECT& anchorRect);

    /** Apre il mixer volume classico (SndVol.exe). */
    static int32_t ShowVolumeMixer();

    /// Rilascia le istanze COM tenute in cache.
    static void Shutdown();
};

/* Bordo Aero dal compositor (DWM NC rendering) su un flyout classico:
 * niente thick frame, quindi niente ridimensionamento ne' scritte di
 * cornice che il flyout di Windows 7 non aveva. */
void ApplyAeroFlyoutStyle(HWND hFlyout);


} /* namespace w7t */

#endif /* WIN7TASKBAR_FLYOUTLAUNCHER_H */
