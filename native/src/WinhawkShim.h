// Win7Taskbar - shim di compatibilita' per l'API Windhawk
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// I file Win7NetworkFlyout.cpp e Win8NetworkFlyout.cpp sono i porting
// fedeli delle mod Windhawk "Windows 7 Network Flyout Recreation" v5.0.0
// (autore babamohammed) e "Windows 8x Network Flyout Recreation" v1.0.0
// (autore AdmXP8/Administratox), entrambe MIT secondo la policy del
// repository ramensoftware/windhawk-mods (le mod senza licenza esplicita
// sono pubblicate sotto MIT).
// Questo shim sostituisce le sole funzioni dell'API di caricamento di
// Windhawk (logging, impostazioni, hooking) senza toccare la logica dei
// flyout. Gli hook di funzione non sono necessari nei porting (la parte
// Pannello di controllo e' esclusa e l'intercettazione del click avviene
// nel tray di Win7Taskbar), quindi SetFunctionHook ritorna sempre false.

#pragma once

#include <windows.h>
#include <commctrl.h>
#include <cstdio>
#include <cstdarg>

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

/* v1.21.7 - Flyout settings chosen by the user in the Properties window,
 * extra settings tab.
 *
 * In the Windhawk mod every value came from Wh_GetIntSetting, that is from
 * the Windhawk panel. In Win7Taskbar the only configuration system is the
 * settings.json of the managed layer: what lives here is just the copy the
 * core receives from W7T_SetExtraSettings and that the flyout reads as
 * before.
 *
 * Privacy is the only mod setting the program exposes today: the default
 * stays 0 (real network names), as in the mod with no configuration. */
inline int& PrivacyModeValue() {
    static int value = 0;
    return value;
}

inline void SetPrivacyMode(int on) {
    PrivacyModeValue() = on ? 1 : 0;
}

} // namespace w7tshim

inline void Wh_Log(const wchar_t* fmt, ...) {
    wchar_t buf[1024];
    va_list args;
    va_start(args, fmt);
    vswprintf(buf, ARRAYSIZE(buf), fmt, args);
    va_end(args);
    buf[ARRAYSIZE(buf) - 1] = L'\0';
    wchar_t line[1100];
    StringCchPrintfW(line, ARRAYSIZE(line), L"[W7TNetFlyout] %s\n", buf);
    OutputDebugStringW(line);
}

inline int Wh_GetIntSetting(const wchar_t* name) {
    /* v1.21.7: the user's settings take precedence over the mod defaults
     * (see w7tshim::PrivacyModeValue). */
    if (name != nullptr && _wcsicmp(name, L"privacyMode") == 0) {
        return w7tshim::PrivacyModeValue();
    }
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
