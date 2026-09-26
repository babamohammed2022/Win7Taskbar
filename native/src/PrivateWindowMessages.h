// Win7Taskbar - Core nativo - costanti messaggi privati
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
//
// Tutti i WM_USER/WM_APP "nostri" in un punto solo: ogni modulo li
// include da qui, nessuno sparge offset per il codice. Le basi usate
// altrove nel progetto e DA NON TOCCARE (messaggi standard/reali):
//   - ExplorerTrayReader: WM_USER+23/24/29 = TB_* toolbar (firma pubblica)
//   - JumpListWindow:     WM_APP+0x177    = interno JumpList (esistente)
//   - AppSearchWindow:    WM_APP+1/2      = ricerca (esistente)
//   - LanguageSwitcher:   WM_APP+1/2      = switch lingua (esistente)

#pragma once

#include <windows.h>

namespace w7t {

/* Callback AppBarMessage (ABN_*): wParam = notifica, lParam = handle. */
constexpr UINT kMsgAppBarNotify = WM_USER + 0x5A3C;

/* Callback icone area di notifica (NIN_* / WM_CONTEXTMENU...): il
 * sistema passa lParam=LOWORD(msg), wParam=icon id. */
constexpr UINT kMsgTrayIconCallback = WM_APP + 0x7A0;

/* TaskbarCreated (broadcast registrato) — vedi ShellHookReceiver. */

} // namespace w7t
