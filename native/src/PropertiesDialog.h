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
    int32_t lang;           // 0=it, 1=en
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
    /* v3.5: stile dell'indicatore della lingua di input.
     * 0 = nascosta, 1 = Windows 7, 2 = Windows 8.1, 3 = Windows 10/11. */
    int32_t inputLanguageMode;
    /* Windows 11 only: 0 automatic, 1 modern, 2 legacy 32-bit. */
    int32_t taskManagerMode;
    /* v1.21.7 - "Extra settings" tab, fields APPENDED AT THE END like the
     * previous ones (same rule: the receiver reads only what the packet
     * really carries, see cbData in HandlePropsCopyData). */
    int32_t flyoutColorMode;       // 0 = system colour, 1 = custom colour
    int32_t flyoutColorRgb;        // 0x00RRGGBB of the custom colour
    int32_t connectionPrivacyMode; // 0 = normal, 1 = privacy
    int32_t themeSelection;        // 0 = Windows 7, 1 = Windows 8.1,
                               // 2 = Windows 7 Aero Basic, 3 = Windows 8 Beta 8148
    /* v1.21.37 - "avvio automatico con Windows" della scheda Informazioni.
     * Ultimo campo, AGGIUNTO IN CODA come tutti gli altri: il ricevente lo
     * legge solo se il pacchetto contiene davvero 80 byte (cbData). L'effetto
     * (scrivere/togliere il valore Run nel registro) lo applica il gestito,
     * con la stessa logica di RetroBar (vedi AutoStart.cs e CREDITS.txt). */
    int32_t autoStart;             // 0/1 avvia il programma all'avvio di Windows
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
              int32_t toolbarLinks, int32_t inputLanguageMode,
              int32_t taskManagerMode,
              int32_t flyoutColorMode, int32_t flyoutColorRgb,
              int32_t connectionPrivacyMode, int32_t themeSelection,
              int32_t autoStart);

    /* v2.47: il font del dialogo e' un oggetto GDI: si crea una volta per
     * apertura e si distrugge alla chiusura, nel distruttore della classe
     * (RAII). Prima veniva creato a ogni WM_INITDIALOG e mai liberato: un
     * HFONT perso per ogni apertura della finestra. */
    ~PropertiesDialog();

private:
    static INT_PTR CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
    void SendApply(bool openSearch, bool closeApp);

    /* v1.21.7: resolves the colour of the swatch on the "Extra settings"
     * tab and redraws the control (SS_OWNERDRAW, see WM_DRAWITEM).
     * With "system colour" it asks the system every time, so a change of the
     * Windows accent is followed without reopening the Properties. */
    void RefreshExtraSwatchColor();

    /* v1.21.8: places OK / Cancel / Apply in the lower-right corner of the
     * dialog, in measured pixels (see the definition for the reasoning) and
     * keeps the window inside the work area of its monitor. Both are called
     * once, at the end of WM_INITDIALOG, when fonts, texts and tab pages are
     * already in place. */
    void LayoutCommandButtons(HWND hwnd);
    void FitDialogToWorkArea(HWND hwnd);

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
    int32_t m_inputLanguageMode = 1;   /* v3.5: stile Windows 7 di default */
    int32_t m_taskManagerMode = 0;     /* automatico */
    /* v1.21.7 - Extra settings. */
    int32_t m_flyoutColorMode = 0;     /* 0 = system colour */
    int32_t m_flyoutColorRgb = 0x0078D7;   /* chosen custom colour */
    int32_t m_connectionPrivacyMode = 0;
    int32_t m_themeSelection = 0;      /* Windows 7 (default e ripiego) */
    /* v1.21.37: stato corrente dell'avvio automatico (letto dal registro dal
     * gestito prima di aprire il dialogo, come fa RetroBar in LoadAutoStart). */
    int32_t m_autoStart = 0;
    /* Colour shown by the swatch next to the two entries: with the "system
     * colour" mode it is read from the system (see
     * RefreshExtraSwatchColor). */
    uint32_t m_extraSwatchRgb = 0x0078D7;
    HFONT m_font = nullptr;   /* RAII: vive quanto il dialogo (v2.47) */
};

} /* namespace w7t */
