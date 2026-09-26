/*
 * Win7Taskbar - Core nativo - Cambio lingua: un solo punto d'ingresso
 * Copyright (c) 2026 Win7Taskbar contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ====================================================================
 * PERCHE' QUESTO FILE ESISTE
 *
 * Il nativo disegna menu contestuali, finestra Proprieta', ricerca
 * applicazioni e flyout batteria: tutte stringhe generate dall'app, tutte
 * da cambiare insieme quando l'utente sceglie un'altra lingua.
 *
 * W7T_SetLanguage() e' QUELL'UNICO INGRESSO. Il managed lo chiama a ogni
 * cambio (compreso il primo avvio, quando la lingua viene rilevata dal
 * sistema): da qui si aggiornano
 *
 *   - le stringhe brevi e le due tabelle lunghe (Strings.cpp);
 *   - il flyout batteria;
 *   - il flyout di rete (W7TNetFlyout_SetLanguage fa la sua mappa interna).
 *
 * Non esistono piu' due sistemi paralleli: W7T_BatteryFlyoutSetLanguage e
 * W7T_NetFlyoutSetLanguage restano esportati per compatibilita' e passano
 * tutti da qui.
 *
 * Il file esporta anche l'ELENCO DELLE LINGUE, cosi' il livello gestito non
 * deve tenerne una copia propria: indice, codice a due lettere e nome nativo
 * arrivano da w7t::Languages().
 * ====================================================================
 */

#include "Strings.h"
#include "BatteryFlyout.h"
#include "Win7NetworkFlyout.h"
#include "Win8NetworkFlyout.h"   /* v3.8: anche la variante Windows 8 cambia lingua */
#include "SehGuard.h"

#include <windows.h>
#include <cstdint>

extern "C" __declspec(dllexport) void __stdcall W7T_SetLanguage(const char* twoLetterCode) {
    W7T_SEH_TRY {
        const w7t::Lang lang = w7t::LangFromCode(twoLetterCode);
        const int index = w7t::LangIndex(lang);

        w7t::SetLanguage(twoLetterCode);

        /* Batteria e rete usano lo STESSO indice dell'elenco unico: il
         * flyout di rete ha una sua tabella storica (porting della mod
         * Windhawk) e traduce l'indice al proprio interno, vedi
         * W7TNetFlyout_SetLanguage. */
        w7t::BatteryFlyout::Instance().SetLanguage(index);
        w7tnet::W7TNetFlyout_SetLanguage(index);
        /* v3.8/1.21.15: la stessa chiamata raggiunge anche la variante
         * Windows 8 del flyout di rete, che pero' per scelta di brief
         * segue sempre la LINGUA DI SISTEMA (l indice e' ignorato). */
        w7t::Win8NetworkFlyout::Instance().SetLanguage(index);
    } W7T_SEH_CATCH {} W7T_SEH_END
}

/* ---------------------------------------------------------------------- */
/*  Elenco delle lingue (per il livello gestito)                           */
/* ---------------------------------------------------------------------- */

extern "C" __declspec(dllexport) int32_t __stdcall W7T_GetLanguageCount(void) {
    return w7t::kLangCount;
}

extern "C" __declspec(dllexport) const char* __stdcall W7T_GetLanguageCode(int32_t index) {
    const char* code = "en";
    W7T_SEH_TRY
        code = w7t::LangCode(w7t::LangFromIndex(index));
    W7T_SEH_CATCH
    W7T_SEH_END
    return code;
}

extern "C" __declspec(dllexport) const wchar_t* __stdcall W7T_GetLanguageName(int32_t index) {
    const wchar_t* name = L"English";
    W7T_SEH_TRY
        name = w7t::LangName(w7t::LangFromIndex(index));
    W7T_SEH_CATCH
    W7T_SEH_END
    return name;
}

/* Lingua attualmente attiva nel core (indice dell'elenco unico). */
extern "C" __declspec(dllexport) int32_t __stdcall W7T_GetLanguageIndex(void) {
    int32_t index = 1;
    W7T_SEH_TRY
        index = w7t::LangIndex(w7t::CurrentLanguage());
    W7T_SEH_CATCH
    W7T_SEH_END
    return index;
}

/* Lingua dell'interfaccia di Windows, filtrata sulle lingue supportate:
 * l'app la usa al primo avvio, quando l'utente non ha ancora scelto. */
extern "C" __declspec(dllexport) int32_t __stdcall W7T_DetectSystemLanguageIndex(void) {
    int32_t index = 1;
    W7T_SEH_TRY
        index = w7t::LangIndex(w7t::DetectSystemLanguage());
    W7T_SEH_CATCH
    W7T_SEH_END
    return index;
}
