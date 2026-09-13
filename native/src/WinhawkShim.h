// Win7Taskbar - shim di compatibilita' per l'API Windhawk
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// Il file Win7NetworkFlyout.cpp e' un porting fedele della mod Windhawk
// "Windows 7 Network Flyout Recreation" v5.0.0 (autore babamohammed,
// licenza MIT come da policy del repository ramensoftware/windhawk-mods:
// le mod senza licenza esplicita sono pubblicate sotto MIT).
// Questo shim sostituisce le sole funzioni dell'API di caricamento di
// Windhawk (logging, impostazioni, hooking) senza toccare la logica del
// flyout. Gli hook di funzione non sono necessari nel porting (la parte
// Pannello di controllo e' esclusa e l'intercettazione del click avviene
// nel tray di Win7Taskbar), quindi SetFunctionHook ritorna sempre false.

#pragma once

#include <windows.h>
#include <commctrl.h>
#include <cstdio>
#include <cstdarg>

/* v2.64: le righe del riquadro di rete entrano nel registro del programma.
 * Dichiarazione locale per non trascinare qui tutto Common.h; la definizione
 * e' quella di w7t::AppendCoreLog (Common.cpp). */
namespace w7t {
void AppendCoreLog(const wchar_t* line);
}

namespace w7tshim {

/* Impostazioni predefinite della mod (le stesse della mod originale):
 * intercettazione attiva, refresh 3000 ms, angoli arrotondati, icone di
 * posizione rete, tema chiaro, lingua automatica, privacy e hotkey off,
 * ripristino Pannello di controllo ESCLUSO dal porting. */
inline int DefaultIntSetting(const wchar_t* name) {
    if (_wcsicmp(name, L"interceptNativeFlyout") == 0) return 1;
    if (_wcsicmp(name, L"privacyMode") == 0) return 0;
    if (_wcsicmp(name, L"refreshInterval") == 0) return 3000;
    if (_wcsicmp(name, L"enableHotkey") == 0) return 0;
    if (_wcsicmp(name, L"useRoundedCorners") == 0) return 1;
    if (_wcsicmp(name, L"useNetworkLocationIcons") == 0) return 1;
    if (_wcsicmp(name, L"restoreClassicNetworkCenterLinks") == 0) return 0;
    return 0;
}

inline const wchar_t* DefaultStringSetting(const wchar_t* name) {
    if (_wcsicmp(name, L"theme") == 0) return L"light";
    return L"auto";   /* language */
}

} // namespace w7tshim

inline void Wh_Log(const wchar_t* fmt, ...) {
    wchar_t buf[1024];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, args);
    va_end(args);
    wchar_t line[1100];
    StringCchPrintfW(line, ARRAYSIZE(line), L"[W7TNetFlyout] %s", buf);
    OutputDebugStringW(line);
    /* v2.64 - NON SOLO AL DEBUGGER.
     *
     * Fino a ieri queste righe esistevano solo in OutputDebugString: il
     * riquadro di rete era l'unico componente del programma a non dire
     * niente nel registro, quindi un elenco vuoto o un handle WLAN non
     * disponibile erano invisibili. Ora finiscono anche in log-core.txt. */
    w7t::AppendCoreLog(line);
}

inline int Wh_GetIntSetting(const wchar_t* name) {
    return w7tshim::DefaultIntSetting(name);
}

inline const wchar_t* Wh_GetStringSetting(const wchar_t* name) {
    return w7tshim::DefaultStringSetting(name);
}

inline void Wh_ApplyHookOperations() {
    /* Gli hook di funzione non servono nel porting. */
}

inline void Wh_RemoveFunctionHook(void* /*target*/) {
    /* Nessun hook installato nel porting. */
}

namespace WindhawkUtils {

class StringSetting {
public:
    static StringSetting make(const wchar_t* name) {
        StringSetting s;
        s.value = Wh_GetStringSetting(name);
        return s;
    }
    const wchar_t* get() const { return value ? value : L""; }
private:
    const wchar_t* value = nullptr;
};

/* Gli hook di funzione della mod originale servivano solo al Pannello di
 * controllo (escluso dal porting), alla privacy nel pannello (esclusa) e
 * al percorso RetroBar (non applicabile). Il porting non ne installa. */
template <typename Target, typename Hook, typename Orig>
bool SetFunctionHook(Target /*target*/, Hook /*hook*/, Orig* orig) {
    if (orig) *orig = nullptr;
    return false;
}

/* La subclass "da qualsiasi thread" della mod originale; nel porting le
 * chiamate residue riguardano finestre create dal flyout stesso, quindi la
 * subclass diretta e' sufficiente. Template perche' la mod usa procedure a
 * 5 parametri (senza dwRefData): il parametro in piu' passa inosservato
 * nella convenzione x64, come nell'helper originale di Windhawk. */
template <typename TProc>
inline bool SetWindowSubclassFromAnyThread(HWND hWnd, TProc proc, UINT_PTR id) {
    return SetWindowSubclass(hWnd, reinterpret_cast<SUBCLASSPROC>(proc), id,
                             0) != FALSE;
}

template <typename TProc>
inline bool RemoveWindowSubclassFromAnyThread(HWND hWnd, TProc proc) {
    return RemoveWindowSubclass(hWnd, reinterpret_cast<SUBCLASSPROC>(proc),
                                0) != FALSE;
}

} // namespace WindhawkUtils
