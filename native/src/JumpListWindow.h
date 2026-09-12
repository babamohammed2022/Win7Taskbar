// Win7Taskbar - Jump List stile Windows 7 per i pulsanti della barra
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// v2.38: click destro su un pulsante (pinnato o in esecuzione) apre un
// popup con bordo Aero (ApplyAeroFlyoutStyle condiviso) che contiene:
//   - sezione "Recenti" DELL'APP (IApplicationDocumentLists) -- in v1 e'
//     DISATTIVATA (kEnableRecentSection=false) per stabilita': la lettura
//     delle jump list reali e' la parte piu' esposta alle app mal
//     comportate; il lettore esiste gia' (RAII + SEH) e si riattiva con
//     una sola costante;
//   - separatore;
//   - nome applicazione con icona grande, cliccabile (nuova istanza);
//   - separatore;
//   - "Fissa/Rimuovi questo programma dalla barra delle applicazioni".
// Nessuna voce "recente" viene mai inventata: o arriva dalla jump list
// registrata dall'app, o la sezione resta nascosta.

#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

namespace w7t {

/* v2.38 punto 5: la sezione Recenti resta spenta nella prima versione.
 * Il lettore COM e' implementato e protetto; riattivare qui. */
constexpr bool kEnableRecentSection = false;

struct JumpListItem {
    std::wstring displayName;   /* senza estensione */
    std::wstring path;          /* percorso completo */
};

class JumpListWindow {
public:
    static JumpListWindow& Instance();

    /* Mostra ancorata al pulsante (rettangolo in coordinate schermo).
     * iconArgb = pixel 0xAARRGGBB ad alpha dritto (puo' essere null). */
    void Show(const RECT& buttonRect,
              const std::wstring& title,
              const std::wstring& launchPath,
              const std::wstring& pinnedLnkPath,
              bool isPinned,
              const uint32_t* iconArgb, int iconW, int iconH,
              int lang);
    void Hide();
    bool IsVisible() const;

    /* Lettura VERA della jump list dell'app (IApplicationDocumentLists,
     * categoria Recenti). Ritorna al piu' 10 voci; mai crasha. */
    static std::vector<JumpListItem> ReadRecentItems(
        const wchar_t* appUserModelId);

private:
    JumpListWindow() = default;
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void RegisterClassOnce();
    void OnPaint(HWND hwnd);
    void Layout();
    RECT AppRow() const;
    RECT PinRow() const;
    void PerformPinOrUnpin();
    void LaunchApp();

    HWND m_hwnd = nullptr;
    bool m_classRegistered = false;
    int  m_lang = 0;

    std::wstring m_title;
    std::wstring m_launchPath;
    std::wstring m_pinnedLnk;
    bool m_pinned = false;

    std::vector<JumpListItem> m_recent;

    HBITMAP m_appIcon = nullptr;
    int m_recentTop = 0;    /* y della riga app */
    int m_totalH = 0;
};

} // namespace w7t
