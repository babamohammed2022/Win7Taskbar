/*
 * Win7Taskbar - Menu contestuali Win32 nativi
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
 * ====================================================================
 * PERCHE' QUESTO FILE ESISTE
 *
 * La jump-list disegnata con un ContextMenu WPF e' solo un disegno: non
 * e' il menu della finestra bersaglio. Windows 7 mostra invece il VERO
 * menu di sistema (GetSystemMenu) della finestra, con le voci abilitate
 * o disabilitate secondo lo stato reale, e lo esegue con WM_SYSCOMMAND.
 *
 * Qui usiamo TrackPopupMenuEx su un menu di sistema clonato. Il menu gira
 * nel nostro processo ma i comandi vengono inviati alla finestra bersaglio,
 * esattamente come fa la shell.
 * ====================================================================
 */

#ifndef WIN7TASKBAR_SHELLMENU_H
#define WIN7TASKBAR_SHELLMENU_H

#include "Common.h"

namespace w7t {

class ShellMenu {
public:
    /**
     * Mostra il menu di sistema reale della finestra indicata, in (x, y)
     * schermo, e inoltra il comando scelto alla finestra stessa.
     *
     * @param ownerHwnd finestra a cui appartiene il menu
     * @param x,y       posizione in coordinate schermo (pixel fisici)
     * @param bottomEdge se true il menu si apre verso l'alto (barra in basso)
     * @return W7T_OK, oppure un codice d'errore
     */
    static int32_t ShowWindowSystemMenu(HWND ownerHwnd, int32_t x, int32_t y,
                                        bool bottomEdge);

    /**
     * Menu di gruppo in stile Superbar: "Riduci a icona gruppo" /
     * "Chiudi gruppo". Costruito con le API menu native, non in WPF, per
     * avere aspetto e comportamento del sistema.
     *
     * @return 1 = minimizza gruppo, 2 = chiudi gruppo, 0 = annullato
     */
    static int32_t ShowGroupMenu(HWND ownerHwnd, int32_t x, int32_t y,
                                 bool bottomEdge,
                                 const wchar_t* minimizeText,
                                 const wchar_t* closeText);

    /**
     * Menu contestuale generico costruito con le API native, cosi' da avere
     * lo stesso aspetto degli altri menu del sistema.
     *
     * Le voci arrivano come un'unica stringa separata da '\n'; una voce
     * uguale a "-" diventa un separatore. Le voci che iniziano con '!' sono
     * mostrate disabilitate (il '!' non viene visualizzato).
     *
     * @return indice della voce scelta a partire da 1 (i separatori NON
     *         contano), oppure 0 se il menu e' stato annullato.
     */
    static int32_t ShowContextMenu(int32_t x, int32_t y, bool bottomEdge,
                                   const wchar_t* itemsSeparatedByNewline);

    /**
     * Come ShowContextMenu, ma con supporto a sottomenu e voci con spunta.
     *
     * Sintassi per riga:
     *   "-"        separatore
     *   ">Testo"   apre un sottomenu chiamato "Testo" (le righe seguenti
     *              finiscono dentro finche' non arriva una riga "<")
     *   "<"        chiude il sottomenu corrente
     *   "!Testo"   voce disabilitata
     *   "*Testo"   voce con spunta (checkbox attivo)
     *   "!*Testo" / "*!Testo"  disabilitata E spuntata
     * I prefissi '!' e '*' possono comparire in qualsiasi ordine prima del
     * testo. Le intestazioni dei sottomenu non sono selezionabili e non
     * consumano un identificatore.
     *
     * @param anchorAtCursor se true il menu si apre ESATTAMENTE sul punto
     *        (x, y) ricevuto - normalmente la posizione del cursore al
     *        momento del clic destro - senza essere riancorato alla barra.
     *        Serve ai menu della barra stessa e dell'orologio, che devono
     *        comparire dove si trova il cursore. Con false resta il
     *        comportamento storico: il menu viene ancorato al bordo
     *        superiore dell'area di lavoro (menu delle APP, che devono
     *        aprirsi sopra il pulsante della Superbar e non sotto il
     *        puntatore).
     *
     * @return indice a partire da 1 della voce scelta (contando solo le
     *         voci selezionabili nell'ordine in cui compaiono, sottomenu
     *         inclusi), oppure 0 se annullato.
     */
    static int32_t ShowContextMenuEx(int32_t x, int32_t y, bool bottomEdge,
                                     const wchar_t* itemsSeparatedByNewline,
                                     bool anchorAtCursor = false);
};

} /* namespace w7t */

#endif /* WIN7TASKBAR_SHELLMENU_H */
