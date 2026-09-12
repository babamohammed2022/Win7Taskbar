/*
 * Win7Taskbar - Core nativo - Sorveglianza eventi di sistema per la tray
 * Copyright (c) 2026 Win7Taskbar contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * ---------------------------------------------------------------------------
 * PERCHE' QUESTO FILE ESISTE
 *
 * Le icone di sistema (batteria, rete, volume) non passano dalla nostra
 * tray: la shell le aggiorna dentro la PROPRIA toolbar. Per restare 1:1
 * senza polling aggressivo servono gli stessi EVENTI a cui la shell si
 * aggancia, e sono tutti API pubbliche documentate:
 *
 *   - rete    : Network List Manager. La shell (pnidui) riceve il cambio
 *               di connettivita' tramite INetworkListManagerEvents::Advise;
 *               qui facciamo lo stesso con una sink COM nostra. L'header di
 *               riferimento e' netlistmgr.h (Windows SDK / MinGW-w64);
 *               interfaccia e GUID sono pubblico contratto documentato.
 *   - tray    : HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer
 *               RegNotifyChangeKeyValue(REG_NOTIFY_CHANGE_LAST_SET) sveglia
 *               il watcher quando l'utente cambia "Nascondi icone e
 *               notifiche inattive" (EnableAutoTray) o qualsiasi altra
 *               impostazione Explorer: e' lo stesso meccanismo a cui la
 *               shell affida le proprie regolazioni, niente timer.
 *   - session: WTS_SESSION_LOCK/UNLOCK arrivano via RegisterSessionNotification
 *              (WM_WTSSESSION_CHANGE): al lock la shell sospende l'aggiornamento
 *              del volume; noi ri-sincronizziamo il modello, perche' il
 *              desktop locked non invalida nulla ma l'unlock a volte si.
 *
 * Il pattern "produttore -> evento -> consumatore registrato" e' lo stesso
 * di SHChangeNotify (nel decompilato di shell32 di Windows 98, sub_66804E09):
 * l'oggetto notifica la finestra registrata con SendNotifyMessage, senza
 * loop di controllo. Qui l'oggetto-notificante e' Windows stesso.
 * ---------------------------------------------------------------------------
 */

#ifndef W7T_SYSTEM_EVENTS_WATCH_H
#define W7T_SYSTEM_EVENTS_WATCH_H

#include "Common.h"

namespace w7t {

class SystemEventsWatch {
public:
    /* Ogni fonte notifica POSTMANDO un messaggio registrato alla finestra
     * del servizio (nessuna chiamata dentro il nostro mutex dai thread COM). */

    /* Registro di Explorer: enable/disable raggruppamento, policy, tema. */
    static void StartRegistryWatch(HWND notifyWnd, UINT notifyMsg);
    static void StopRegistryWatch();

    /* Network List Manager: connettivita' cambiata (cavo, Wi-Fi, VPN). */
    static void StartNetworkWatch(HWND notifyWnd, UINT notifyMsg);
    static void StopNetworkWatch();

    /* WM_WTSSESSION_CHANGE: blocco/sblocco sessione. */
    static void StartSessionWatch(HWND notifyWnd);
    static void StopSessionWatch();
};

} /* namespace w7t */

#endif /* W7T_SYSTEM_EVENTS_WATCH_H */
