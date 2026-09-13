/*
 * Win7Taskbar - Core nativo - Icone di RIPIEGO dell'area di notifica
 * Copyright (c) 2026 Win7Taskbar contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ---------------------------------------------------------------------------
 * PERCHE' QUESTO FILE ESISTE
 *
 * Su alcune macchine le icone di sistema (rete, volume, batteria) restano
 * INVISIBILI nella barra: Explorer accetta la lettura della toolbar ma
 * rifiuta CopyIcon, oppure la cattura dei pixel fallisce, e l'icona arriva
 * al modello senza alcuna bitmap. Prima di questo file non c'era nulla da
 * mostrare e il posto restava vuoto.
 *
 * Qui vivono le icone DISEGNATE DA NOI (TrayIconAssets.inc, con la batteria
 * che riusa i glifi reali di BatteryAssets.inc) usate SOLO in quel caso.
 *
 * Regola d'oro: il ripiego entra in scena soltanto quando manca la bitmap
 * vera. Un'icona leggibile da Explorer non viene MAI sostituita, e ogni
 * ripiego adottato lascia una riga nel log una volta sola.
 *
 * L'identificazione dell'icona e' per PROPRIETARIO, mai per posizione:
 *   1) guidItem della NOTIFYICONDATA (le tre GUID documentate della shell);
 *   2) in mancanza, il modulo della finestra proprietaria (pnidui.dll per
 *      la rete, SndVolSSO.dll per il volume, stobject.dll per la batteria),
 *      la stessa verifica cross-process gia' usata dal resto del core.
 * ---------------------------------------------------------------------------
 */

#ifndef W7T_TRAY_FALLBACK_ICONS_H
#define W7T_TRAY_FALLBACK_ICONS_H

#include "Common.h"

namespace w7t {

/* Tipo di icona di sistema di una voce dell'area di notifica. */
enum class SystemIconKind : int32_t {
    None    = 0,
    Network = 1,
    Volume  = 2,
    Battery = 3,
};

class TrayFallbackIcons {
public:
    /* Classifica una voce. Costosa la prima volta per ogni voce (apre il
     * processo proprietario): il chiamante deve memorizzare il risultato. */
    static SystemIconKind Identify(uint64_t ownerHwnd, const GUID& guidItem);

    /* Icona di ripiego corrispondente allo STATO CORRENTE del sistema.
     * Ritorna false se non c'e' nulla da disegnare o se la decodifica dei
     * PNG incorporati non e' disponibile: in quel caso il chiamante lascia
     * stare la bitmap che ha. */
    static bool Render(SystemIconKind kind, ArgbBitmap& out);

    /* Una riga nel log la PRIMA volta che un tipo di ripiego entra in uso,
     * con il motivo fornito dal chiamante. */
    static void LogFirstUse(SystemIconKind kind, const wchar_t* reason);

    /* Stato di rete corrente, esposto anche per la diagnostica:
     *   1..5 barre = collegato (5 = segnale pieno o rete cablata);
     *   0          = collegato senza accesso a Internet (avviso);
     *  -1          = non collegato. */
    static int NetworkLevel();
};

} /* namespace w7t */

#endif /* W7T_TRAY_FALLBACK_ICONS_H */
