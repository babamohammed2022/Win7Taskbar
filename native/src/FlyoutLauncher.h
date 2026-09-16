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

/* ------------------------------------------------------------------------- */
/*  v2.63 - UN SOLO PUNTO DI VERITA' PER LA SCELTA DEI RIQUADRI              */
/* ------------------------------------------------------------------------- */
/*  Fino alla v2.62 la polarita' delle impostazioni era replicata in ogni    */
/*  punto che apriva un riquadro: il dialogo Proprieta', il pacchetto        */
/*  WM_COPYDATA, il livello gestito e il ramo dei clic sintetici del core    */
/*  avevano ciascuno la propria copia della stessa decisione, e bastava una  */
/*  copia scritta al contrario perche' l'utente vedesse aprirsi il riquadro  */
/*  opposto a quello scelto (o nessuno). Qui la decisione vive UNA volta:    */
/*  il frontend pubblica le quattro preferenze lette dalla configurazione    */
/*  (una sola chiamata, W7T_SetFlyoutPreferences) e ogni percorso del core   */
/*  le interroga invece di reinterpretare i propri parametri.                */
/*                                                                           */
/*  Stile del riquadro. I valori numerici sono quelli del pacchetto          */
/*  WM_COPYDATA e dell'ABI pubblica: 0 = Windows 7 (classico/Win32),         */
/*  1 = Windows 10/11 (immersivo della shell).                               */
/*  v3.8: 2 = Windows 8 (riquadro ricreato, solo per la rete; la             */
/*  variante vive in Win8NetworkFlyout.cpp, implementazione: Administratox). */
enum class FlyoutStyle : int32_t {
    Win7   = 0,
    Modern = 1,
    Win8   = 2
};

struct FlyoutPreferences {
    FlyoutStyle clock   = FlyoutStyle::Win7;
    FlyoutStyle network = FlyoutStyle::Win7;
    FlyoutStyle volume  = FlyoutStyle::Win7;
    FlyoutStyle battery = FlyoutStyle::Win7;
};

/* Pubblica le preferenze lette dal frontend. Chiamabile da qualunque thread;
 * se il frontend non chiama mai (build vecchio) restano i valori Windows 7,
 * cioe' il comportamento della v2.62. */
void SetFlyoutPreferences(const FlyoutPreferences& prefs);
FlyoutPreferences GetFlyoutPreferences();

/* Stile richiesto per un tipo di riquadro. */
FlyoutStyle PreferredStyle(FlyoutKind kind);

/* La porta dei riquadri immersivi di questa build.
 *
 * Un solo posto decide se i riquadri della shell (Windows 10/11) sono
 * utilizzabili: la build >= 22000 letta con RtlGetVersion E la presenza
 * dell'infrastruttura (combase + fabbrica ShellExperience), provata
 * davvero. Nessun launcher tiene piu' una propria copia di questo giudizio,
 * e ogni risposta viene registrata una volta sola con LogTagged(L"GATE", ..),
 * cosi' dal log si vede su quale Windows gira il programma e che cosa ha
 * deciso. */
bool IsModernFlyoutHostAvailable();

/* Infrastruttura immersiva presente (Windows 10 o successivo + fabbrica
 * ShellExperience). E' la condizione tecnica; IsModernFlyoutHostAvailable()
 * aggiunge il fatto che su questa build i riquadri immersivi sono quelli
 * XAML di Windows 11. */
bool IsImmersiveFlyoutHostUsable();

/* Da chi far aprire il riquadro. Nessun launcher decide da solo: chiede. */
enum class FlyoutRoute {
    Classic,     /* percorso Windows 7 (Aero clock, SndVol, riquadri ricreati) */
    Immersive    /* percorso Windows 10/11 (riquadro della shell)            */
};
FlyoutRoute ChooseFlyoutRoute(FlyoutKind kind);

/* Numero di build (RtlGetVersion) e preferenza, in una riga di log. */
void LogFlyoutGate(const wchar_t* where);

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

    /**
     * v2.62 - Chiude il riquadro dell'orologio DELLA SHELL se e' aperto.
     *
     * Non lo apre mai: serve solo a garantire "un solo riquadro" quando il
     * sistema ha mostrato il suo per conto (per esempio perche' il clic e'
     * arrivato anche alla barra nativa, che su alcune build resta dietro la
     * nostra). Su Windows 10 questo percorso e' quello scelto dall'utente e
     * il frontend non lo chiama.
     */
    static int32_t HideClockFlyout();

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
