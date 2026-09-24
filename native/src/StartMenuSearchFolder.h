/*
 * Win7Taskbar - Start Menu search: Shell Search Folder backend
 * Copyright (c) 2026 Win7Taskbar contributors
 * Licensed under the GNU General Public License version 3 or later.
 *
 * Ricostruzione da zero della pipeline documentata usata dalla ricerca del
 * menu Start di Windows 7 (comportamento osservato sulla superficie COM di
 * SearchFolder.dll: scope del connettore StartMenu, risultati come
 * IShellItem tramite la cartella di ricerca delegata). Nessun codice o
 * binario Microsoft viene copiato, incluso o caricato: qui si usano solo
 * API pubbliche (ISearchFolderItemFactory di shell32 + lo scope delle
 * cartelle conosciute; wordwheel implementato col matcher del progetto).
 */
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace w7t {
namespace startmenu {

/*
 * Backend "Windows Search": crea una cartella di ricerca Shell
 * (ISearchFolderItemFactory) con scope = cartelle conosciute dell'utente e
 * condizione wordwheel sulla query, poi enumera i risultati come IShellItem.
 *
 * Righe emesse nello stesso formato del walker di riserva:
 *   "D|P|M|V|F|R|<percorso>"  (R = cartella)
 *
 * Ritorno:
 *   0 = backend eseguito, rows riempito (anche vuoto)
 *   1 = Windows Search non disponibile -> il chiamante usi il walker
 *   2 = risultati troncati dal budget -> il chiamante segnali "T"
 * La generazione cancella una scansione obsoleta (query "calc" -> "calcu":
 * la richiesta vecchia si ferma e i suoi risultati vengono ignorati).
 */
int CollectShellSearchRows(const std::wstring& query,
                           uint32_t generation,
                           const std::atomic<uint32_t>& generationSource,
                           std::vector<std::wstring>& rows);

/* Stato del servizio Windows Search con una piccola cache (60 s):
 * il polling del servizio ad ogni carattere digitato era inutilmente
 * costoso; la TTL lo mantiene reattivo agli avvii/stop manuale. */
bool WindowsSearchRunning();

} /* namespace startmenu */
} /* namespace w7t */
