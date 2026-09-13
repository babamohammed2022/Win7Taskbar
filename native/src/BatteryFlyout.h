// Win7Taskbar - flyout batteria nativo Win32, stile Windows 7
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// v2.38: flyout batteria con ICONE REALI ritagliate dalla striscia
// fornita dall'utente (BatteryAssets.inc), bordo Aero via DWM con lo
// STESSO helper condiviso degli altri flyout (ApplyAeroFlyoutStyle).
// Nessuna icona inventata: ogni stato (colore x livello, vuota, assente,
// avviso, errore carica, spina AC) e' un glifo ritagliato dalla bitmap.

#pragma once
#include <windows.h>

namespace w7t {

class BatteryFlyout {
public:
    static BatteryFlyout& Instance();

    /* Ancora il flyout sopra il rettangolo icona (coordinate schermo). */
    void ShowAt(const RECT& iconRect);
    void Hide();
    bool IsVisible() const;

    /* Lingua dell'app (0=it,1=en,2=es,3=fr,4=de,5=pt,6=pl,7=ru,8=ja,9=zh). */
    void SetLanguage(int appLang);

    /* v2.41: rilascia le bitmap GDI+ (DLL_PROCESS_DETACH). */
    void Shutdown();

private:
    BatteryFlyout() = default;
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void RegisterClassOnce();
    void DecodeIconsOnce();
    void OnPaint(HWND hwnd);
    RECT LinkRect() const;

    HWND m_hwnd = nullptr;
    bool m_classRegistered = false;
    bool m_linkHot = false;          /* v1.7: hover sul link (stile overflow) */
    int  m_lang = 0;

    /* Icone decodificate UNA volta: HBITMAP 32bpp premoltiplicato. */
    HBITMAP m_icons[31] = {};
    int     m_iconW[31] = {};
    int     m_iconH[31] = {};
    /* v2.41: stesse icone anche come bitmap GDI+ per il disegno ad
     * alta qualita' (ripiego GDI se gdiplus.dll non e' disponibile). */
    void*   m_gdip[31] = {};
    bool    m_iconsReady = false;
};

} // namespace w7t
