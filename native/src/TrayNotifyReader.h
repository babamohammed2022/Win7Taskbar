/*
 * Win7Taskbar - lettura opzionale dello stato TrayNotify di Explorer
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
 * ITrayNotify e ITrayNotifyWin8 sono interfacce COM non documentate:
 * vengono dichiarate qui come tecnica di compatibilita' / reverse engineering,
 * non come API pubblica Microsoft. Il codice e' originale e adatta il
 * meccanismo usato da ManagedShell/RetroBar senza copiare la sua UI.
 */

#ifndef W7T_TRAY_NOTIFY_READER_H
#define W7T_TRAY_NOTIFY_READER_H

#include "Common.h"

#include <cstdint>
#include <string>
#include <vector>

namespace w7t {

/* Fotografia di una voce consegnata dal callback COM. L'immagine e' gia'
 * stata copiata nel nostro processo; nessun HICON remoto esce dal reader. */
struct TrayNotifySnapshotItem {
    uint32_t     event       = 0;
    uint64_t     ownerHwnd   = 0;
    uint32_t     uid         = 0;
    GUID         guidItem    = {};
    int32_t      preference  = -1; /* PREFERENCE_SHOW_* oppure -1 */
    std::wstring exeName;           /* metadato, non identita' primaria */
    std::wstring tooltip;
    ArgbBitmap   bitmap;
};

class TrayNotifyReader {
public:
    /* Acquisisce una fotografia sincrona e poi deregistra sempre il callback.
     * Restituisce false se COM/Explorer non espongono il contratto; il
     * chiamante deve lasciare funzionare WM_COPYDATA, toolbar e UIA. */
    static bool ReadSnapshot(std::vector<TrayNotifySnapshotItem>& out);
};

} /* namespace w7t */

#endif /* W7T_TRAY_NOTIFY_READER_H */
