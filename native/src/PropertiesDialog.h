// Win7Taskbar - finestra Proprieta' Win32 classica (stile Win7)
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// v3.7: riscritto COPIANDO la struttura della mod "Classic Taskbar and
// Start Menu Properties" fornita come riferimento: dialogo da template
// in memoria (unita' DLU, DS_SETFONT + Segoe UI 9), stesse dimensioni
// della mod (262x271 DLU), stessi pulsanti 50x14 allineati (OK |
// Annulla | Applica), tab SysTabControl32, texture tema ETDT_ENABLETAB,
// tema explorer sui figli, WM_GETMINMAXINFO che blocca il resize.
// Le impostazioni restano SOLO le nostre (+ uscita da Win7Taskbar).

#pragma once
#include <windows.h>
#include <cstdint>

namespace w7t {

/* Pacchetto applicato rimandato al taskbar (WM_COPYDATA). */
struct PropsApplyMsg {
    int32_t seconds;        // 0/1 mostra secondi
    int32_t nativeFlyout;   // 0/1 flyout orologio nativo
    int32_t enableSearch;   // 0/1 ricerca applicazioni
    int32_t lang;           // 0=it, 1=en, 2=es, 3=fr, 4=de, 5=pt, 6=pl, 7=ru, 8=ja, 9=zh, 10=ar
    int32_t openSearch;     // 1 = apri il pannello ricerca dopo l'apply
    int32_t closeApp;       // 1 = chiudi Win7Taskbar
    int32_t netFlyoutMode;  // 0 = Win7 ricreato, 1 = Windows 10/11
    int32_t classicVolume;  // 0/1 mixer volume classico (SndVol)
    int32_t batteryFlyout;  // 0/1 flyout batteria ricreato (stile Win7)
    /* v2.47: campi AGGIUNTI IN CODA (compatibilita': il ricevente legge solo
     * quelli che il pacchetto contiene davvero, vedi cbData). */
    int32_t aeroPeek;        // 0/1 anteprima del desktop (Aero Peek)
    int32_t toolbarDesktop;  // 0/1 barra degli strumenti Desktop
    int32_t toolbarAddress;  // 0/1 barra degli strumenti Indirizzi
    int32_t toolbarLinks;    // 0/1 barra degli strumenti Collegamenti
};
constexpr DWORD kPropsCopyDataId = 'W7PA';

class PropertiesDialog {
public:
    /* Mostra (una sola istanza). values = stato corrente. */
    void Show(HWND owner, int32_t lang, int32_t seconds,
              int32_t nativeFlyout, int32_t enableSearch,
              int32_t netFlyout, int32_t classicVolume,
              int32_t batteryFlyout, int32_t aeroPeek,
              int32_t toolbarDesktop, int32_t toolbarAddress,
              int32_t toolbarLinks);

    /* v2.47: il font del dialogo e' un oggetto GDI: si crea una volta per
     * apertura e si distrugge alla chiusura, nel distruttore della classe
     * (RAII). Prima veniva creato a ogni WM_INITDIALOG e mai liberato: un
     * HFONT perso per ogni apertura della finestra. */
    ~PropertiesDialog();

private:
    static INT_PTR CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
    void SendApply(bool openSearch, bool closeApp);

    HWND m_hWnd = nullptr;
    HWND m_owner = nullptr;
    int32_t m_lang = 0;
    int32_t m_seconds = 0;
    int32_t m_nativeFlyout = 0;
    int32_t m_enableSearch = 0;
    int32_t m_netFlyout = 0;
    int32_t m_classicVolume = 0;
    int32_t m_batteryFlyout = 0;
    int32_t m_aeroPeek = 1;
    int32_t m_tbDesktop = 0;
    int32_t m_tbAddress = 0;
    int32_t m_tbLinks = 0;
    HFONT m_font = nullptr;   /* RAII: vive quanto il dialogo (v2.47) */
};

} /* namespace w7t */
