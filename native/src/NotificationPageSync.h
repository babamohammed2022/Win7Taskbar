/*
 * Win7Taskbar - sincronizzazione della pagina legacy delle icone tray.
 *
 * La pagina Control Panel con CLSID 05D7B0F4 legge una cache binaria
 * mantenuta da Explorer. Il formato dei blob non e' documentato da Microsoft:
 * questo modulo non li interpreta e non li forgia. Esegue invece il reset
 * documentato della cache, dopo avere creato un backup leggibile .reg, e
 * lascia che Explorer la ricrei mentre e' attivo.
 */

#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace w7t {

/* Vista normalizzata di una voce usata per il log e per il backfill. */
struct NotificationPageIcon {
    std::wstring exePath;
    std::wstring displayName;
    bool         promoted = false;
    DWORD        pid = 0;
    uint64_t     ownerHwnd = 0;
    uint32_t     uid = 0;
    uint32_t     order = 0;
};

class NotificationPageSync final {
public:
    /* Completa i nomi dal registro NotifyIconSettings e deduplica le voci
     * che il modello live e il registro descrivono con lo stesso eseguibile. */
    static std::vector<NotificationPageIcon> Normalize(
        std::vector<NotificationPageIcon> liveIcons);

    /* Esegue una singola passata reset-reseed della cache legacy di Explorer.
     * Il percorso e' manuale oppure viene richiesto dopo TaskbarCreated, mai
     * da un timer di polling. */
    static int32_t BackfillLegacyPage(
        const std::vector<NotificationPageIcon>& liveIcons);
};

} /* namespace w7t */
