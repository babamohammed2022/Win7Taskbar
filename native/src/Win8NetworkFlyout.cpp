// =============================================================================
// Win7Taskbar - flyout di rete: variante "Windows 8 (ricreato)"
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// Implementazione della variante Windows 8: Administratox.
//
// Che cosa e' questo file:
//   la RICREAZIONE GRAFICA del pannello "Reti" che Windows 8 apriva al tocco
//   sull'icona di rete: pannello laterale scuro a tutta altezza che scivola
//   dal bordo destro dello schermo, righe piane con nome, stato e tacche del
//   segnale, pulsante Connect/Disconnect dentro la riga selezionata, puntini
//   animati durante la connessione, chiusura con Esc/clic fuori/toggle.
//
// Che cosa NON e':
//   NON contiene logica di rete. Enumerazione reti, stati di connessione,
//   connect/disconnect, gestione errori e notifiche sono della mod Windhawk
//   portata in Win7NetworkFlyout.cpp (v5.0.0, MIT) e vengono consumate qui
//   solo attraverso il piccolo ponte di NetLogicBridge.h: snapshot dello
//   stato da un lato, richieste eseguite DALLA LOGICA ORIGINALE sul suo
//   thread dall'altro. Mai duplicata, mai rischiato il comportamento del
//   flyout Windows 7.
//
// Principi osservati (requisiti del progetto):
//   - rilascio determinista di ogni risorsa nativa (timer, font, brush, DC,
//     finestre, heap) nei punti di uscita e su WM_NCDESTROY;
//   - try/catch attorno ai tratti rischiosi (nessun catch vuoto con il
//     silenzio: gli errori vanno nel log diagnostico);
//   - nessuna operazione WLAN sul thread di interfaccia: tutto asincrono;
//   - chiusura sicura in ogni momento (anche in mezzo allo scivolo o mentre
//     una connessione e' in corso);
//   - uso esclusivo delle traduzioni del progetto (Strings.h/.cpp), con
//     ripiego sull'inglese se una tabella manca.
// =============================================================================

#include <windows.h>
#include <windowsx.h>
#include <strsafe.h>
#include <memory>
#include <algorithm>

#include "Common.h"              /* LogTagged, AppendCoreLog, GetDpiForWindowSafe */
#include "SehGuard.h"            /* W7T_SEH_TRY/CATCH/END                          */
#include "Strings.h"             /* W8NetStringsFor, LangFromIndex                */
#include "Win7NetworkFlyout.h"   /* w7tnet::W7TNetFlyout_InitInternal (logica)    */
#include "NetLogicBridge.h"      /* w7tnet::W8NetLogic_* (stesso codice di rete)  */
#include "Win8NetworkFlyout.h"

namespace w7t {

namespace {

/* ------------------------------------------------------------------ */
/*  Costanti di geometria (DIP @96)                                    */
/* ------------------------------------------------------------------ */
constexpr int kWidthBase        = 300;   /* larghezza pannello            */
constexpr int kPadBase          = 24;    /* margine interno               */
constexpr int kTitleTopBase     = 22;
constexpr int kTitleHBase       = 34;
constexpr int kListTopBase      = 84;
constexpr int kRowHBase         = 48;    /* riga chiusa                   */
constexpr int kRowHxBase        = 96;    /* riga espansa                  */
constexpr int kBtnWBase         = 110;
constexpr int kBtnHBase         = 28;
constexpr int kSignalWBase      = 24;
constexpr int kScrollStepBase   = 48;

/* ------------------------------------------------------------------ */
/*  Colori (stile Windows 8 RTM: lastra scura piena, accent blu)       */
/* ------------------------------------------------------------------ */
constexpr COLORREF kBg       = RGB(0x1F, 0x1F, 0x1F);
constexpr COLORREF kEdge     = RGB(0x45, 0x45, 0x45);
constexpr COLORREF kRowHot   = RGB(0x2D, 0x2D, 0x2D);
constexpr COLORREF kAccent   = RGB(0x00, 0x96, 0xC6);
constexpr COLORREF kText     = RGB(0xFF, 0xFF, 0xFF);
constexpr COLORREF kSubText  = RGB(0xB0, 0xB0, 0xB0);
constexpr COLORREF kErrText  = RGB(0xE5, 0x8E, 0x8E);
constexpr COLORREF kBtnBg    = RGB(0x3A, 0x3A, 0x3A);
constexpr COLORREF kBtnHot   = RGB(0x4A, 0x4A, 0x4A);
constexpr COLORREF kBtnEdge  = RGB(0x5A, 0x5A, 0x5A);
constexpr COLORREF kBarOff   = RGB(0x55, 0x55, 0x55);

/* ------------------------------------------------------------------ */
/*  Animazione di scivolo                                              */
/* ------------------------------------------------------------------ */
constexpr int  kAnimInMs  = 220;   /* come il pannello di Windows 8  */
constexpr int  kAnimOutMs = 170;
constexpr UINT_PTR kTimerAnim    = 1;
constexpr UINT_PTR kTimerRefresh = 2;
constexpr UINT_PTR kTimerDots    = 3;
constexpr UINT kAnimTickMs       = 16;
constexpr UINT kRefreshTickMs    = 500;
constexpr UINT kDotsTickMs       = 380;

const wchar_t* kClassName     = L"Win7Taskbar_Win8NetFlyout";
const wchar_t* kLogTag        = L"W8NET";

/* Messaggi privati: tutte le chiamate da altri thread (frontend, core)
 * imboccano questa coda, cosi' finestra e stato hanno UN solo padrone:
 * il thread che ha creato il pannello. */
constexpr UINT WM_W8N_CMD = WM_APP + 0x62;
enum W8nCmd : int {
    W8N_CMD_TOGGLE  = 1,
    W8N_CMD_HIDE    = 2,
    W8N_CMD_SHOW    = 3,
    W8N_CMD_ANCHOR  = 4,   /* lParam = RECT* su heap, proprieta' al gestore */
    W8N_CMD_LANG    = 5,   /* lParam = indice lingua app                    */
    W8N_CMD_DESTROY = 6
};

/* Stato del pannello: macchina chiusa su ogni circostanza. */
enum class PaneState {
    Closed,      /* finestra distrutta o nascosta                            */
    SlidingIn,   /* animazione di apertura                                   */
    Open,
    SlidingOut   /* animazione di chiusura (Poi SW_HIDE + Closed)            */
};

/* Una riga visibile del pannello (derivata dallo snapshot del modulo logica). */
struct PaneRow {
    WCHAR  ssid[33];        /* chiave per le richieste alle logiche          */
    WCHAR  label[44];       /* nome mostrato (ssid + eventuale suffisso)      */
    int    signalPercent;
    int    state;           /* W8NetConnState                                 */
    int    secured;
    int    ethernet;        /* 1 = voce Ethernet (nessun connect/disconnect)  */
};

/* Guardia RAII per oggetti GDI usa-e-getta nei paint. */
class ScopedGdiObject {
public:
    ScopedGdiObject(HDC hdc, HGDIOBJ obj)
        : m_hdc(hdc), m_obj(obj),
          m_old(obj != nullptr ? SelectObject(hdc, obj) : nullptr) {}
    ~ScopedGdiObject() {
        if (m_obj != nullptr) {
            SelectObject(m_hdc, m_old);
            DeleteObject(m_obj);
        }
    }
    ScopedGdiObject(const ScopedGdiObject&) = delete;
    ScopedGdiObject& operator=(const ScopedGdiObject&) = delete;
    operator HGDIOBJ() const { return m_obj; }
private:
    HDC     m_hdc;
    HGDIOBJ m_obj;
    HGDIOBJ m_old;
};

class Net8Pane {
public:
    static Net8Pane& Instance() {
        static Net8Pane s_instance;
        return s_instance;
    }

    /* --------------------- ciclo di vita modulare --------------------- */
    bool Init();
    void Uninit();

    /* punto di ingresso pubblico, da QUALSIASI thread */
    void SetAnchorRect(const RECT& rc);
    void Toggle();
    void Show();
    void Hide();
    void SetLanguage(int lang);
    bool IsVisible() const;

    /* v3.8: come BatteryFlyout::ShutdownIfCreated: rilascia i font GDI se
     * il singleton e' stato costruito (chiamato da DLL_PROCESS_DETACH). */
    static void ShutdownIfCreated();

private:
    Net8Pane() = default;
    ~Net8Pane() { FreeFonts(); }

    /* non copiabile */
    Net8Pane(const Net8Pane&) = delete;
    Net8Pane& operator=(const Net8Pane&) = delete;

    /* --------------------- implementazione interna --------------------- */
    bool EnsureWindow();                 /* solo sul thread proprietario      */
    void RegisterClassOnce();
    int  Scale(int dip) const { return MulDiv(dip, m_dpi, 96); }
    void EnsureFonts();
    void FreeFonts();

    /* macchina a stati del pannello */
    void DoToggle();
    void DoShow();
    void DoHide();
    void BeginSlide(bool opening);
    void OnAnimTick();
    void OnPaneShown();                  /* ingresso nello stato Open         */
    void OnPaneGone();                   /* pulizia comune di Closed          */
    void CascadeRefreshOnShow();         /* prima lettura + domanda asincrona */

    /* dati */
    void RefreshState(bool forceLogicAsk);
    bool CaptureFromLogic();
    void RebuildRows();
    const wchar_t* W8s(const wchar_t* W8NetStrings::* field) const;
    const W8NetStrings& Strings() const;

    /* geometria */
    void ComputePlacement(int& outX, int& outCX, int& outCY, int& outTop) const;
    int  ContentHeight() const;
    int  RowTop(int index) const;        /* DIP -> device, senza scroll       */
    int  RowHeight(int index) const;
    RECT ButtonRectForRow(int index) const;  /* coordinate CLIENTE            */
    int  HitRow(const POINT& ptClient) const;  /* -1 = niente                 */

    /* azioni */
    void OnSelect(int index);
    void OnActivateRow(int index);
    void TryConnect(const PaneRow& row);
    void TryDisconnect(const PaneRow& row);

    /* disegno */
    void OnPaint();
    void DrawBackground(HDC hdc, const RECT& client);
    void DrawTitle(HDC hdc, const RECT& client);
    void DrawRows(HDC hdc, const RECT& client);
    void DrawRowContent(HDC hdc, int index, const RECT& rowRc);
    void DrawSignalBars(HDC hdc, const RECT& rc, int qualityPercent) const;
    void DrawEthernetGlyph(HDC hdc, const RECT& rc) const;
    void DrawEmpty(HDC hdc, const RECT& client);
    void DrawWorkingDots(HDC hdc, int x, int y) const;

    /* finestre */
    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    /* ------------------------- stato ------------------------- */
    HWND        m_hwnd    = nullptr;
    DWORD       m_ownerTid = 0;
    bool        m_classRegistered = false;
    PaneState   m_state   = PaneState::Closed;
    UINT        m_dpi     = 96;
    int         m_langHeld = 0;   /* indice lingua app (0=it .. 10=ar)     */
    bool        m_tracking = false;

    RECT        m_anchor  = {};
    bool        m_haveAnchor = false;

    /* animazione */
    int         m_slideOffset = 0;    /* 0 = tutto fuori (nascosto), Scale(kWidth) = tutto dentro */
    DWORD       m_animStartTick = 0;
    int         m_animFromOffset = 0;

    /* dati: snapshot + righe */
    w7tnet::W8NetSnapshot m_snapshot = {};
    bool                  m_haveSnapshot = false;
    PaneRow               m_rows[w7tnet::kW8NetMaxItems + 1];
    int                   m_rowCount = 0;
    int                   m_selected = -1;
    int                   m_hot      = -1;
    int                   m_scrollY  = 0;
    int                   m_dotPhase = 0;
    DWORD                 m_lastEmptyAskTick = 0;

    /* errore transitorio di una richiesta andata male (mostrato sulla riga) */
    WCHAR                 m_errorSsid[33] = {};
    DWORD                 m_errorUntilTick = 0;

    HFONT   m_titleFont = nullptr;
    HFONT   m_nameFont  = nullptr;
    HFONT   m_subFont   = nullptr;
    HFONT   m_btnFont   = nullptr;
    HBRUSH  m_bgBrush   = nullptr;
};

/* Puntatore valido SOLO se Instance() e' mai stato usato: serve a DllMain
 * per non costruire il singleton durante lo scarico (come BatteryFlyout). */
static Net8Pane* g_createdPane = nullptr;

Net8Pane* PaneIfCreated() {
    return g_createdPane;
}

/* Istanza di lavoro: marca il puntatore "creata" appena il singleton esiste,
 * cosi' DllMain puo' rilasciare i font GDI senza toccare finestre. */
Net8Pane& PaneBoot() {
    Net8Pane& pane = Net8Pane::Instance();
    if (g_createdPane == nullptr) {
        g_createdPane = &pane;
    }
    return pane;
}

/* ------------------------------------------------------------------ */
/*  Facciata pubblica della classe richiesta dall'header               */
/*  (tutto instradato sul piano; ogni metodo e' pronto a essere chiamato */
/*  da thread che non sono il proprietario della finestra).            */
/* ------------------------------------------------------------------ */

bool Net8Pane::Init() {
    bool ok = false;
    W7T_SEH_TRY
    {
        RegisterClassOnce();
        /* La variante ha bisogno del modulo della logica (flyout Windows 7,
         * porting MIT): si inizializza UNA volta per processo e basta.
         * W7TNetFlyout_InitInternal NON e' idempotente, quindi si consulta
         * prima il ponte. */
        if (!w7tnet::W8NetLogic_IsInitialized()) {
            if (!w7tnet::W7TNetFlyout_InitInternal()) {
                LogTagged(kLogTag, L"init: modulo logica (porting mod) non disponibile");
            }
        }
        ok = m_classRegistered && w7tnet::W8NetLogic_IsInitialized() != FALSE;
        if (ok) {
            /* La finestra della logica (nascosta, condivisa col riquadro
             * Win7) garantita una volta: la pompa dei messaggi e delle
             * notifiche e' viva anche se l'utente non ha mai aperto il
             * riquadro Windows 7 su questa postazione. */
            const int erc = w7tnet::W8NetLogic_EnsureLogicReady();
            if (erc != 0) {
                LogTagged(kLogTag,
                          L"init: finestra della logica non instradata (rc=%d), arriva dal primo uso", erc);
            }
        }
        if (!ok) {
            LogTagged(kLogTag, L"init: classe finestra o logica indisponibili (classe=%d, logica=%d)",
                      m_classRegistered ? 1 : 0,
                      w7tnet::W8NetLogic_IsInitialized() ? 1 : 0);
        } else {
            LogTagged(kLogTag, L"init: variante Windows 8 pronta (dpi=%u)", m_dpi);
        }
    }
    W7T_SEH_CATCH
    W7T_SEH_END
    if (!ok) {
        try {
            LogTagged(kLogTag, L"init: eccezione durante l'inizializzazione del riquadro Windows 8");
        } catch (...) {
            /* ULTIMO riparo: nemmeno il log deve far saltare l'init. */
        }
    }
    return ok;
}

void Net8Pane::Uninit() {
    W7T_SEH_TRY
    {
        if (m_hwnd != nullptr) {
            if (GetCurrentThreadId() == m_ownerTid) {
                DestroyWindow(m_hwnd);
            } else {
                /* Lo smantellamento avviene sul thread proprietario; chi
                 * chiama non deve aspettare. */
                PostMessageW(m_hwnd, WM_W8N_CMD, W8N_CMD_DESTROY, 0);
            }
        }
        /* La classe la lasciamo registrata se la finestra sta per morire
         * sul suo thread (UnregisterClass di una classe con finestre vive
         * fallisce comunque); al prossimo Init RegisterClassOnce la trova. */
        FreeFonts();
        m_state = PaneState::Closed;
        m_haveSnapshot = false;
        m_rowCount = 0;
        m_selected = -1;
        LogTagged(kLogTag, L"uninit: riquadro Windows 8 smantellato");
    }
    W7T_SEH_CATCH
    W7T_SEH_END
}

void Net8Pane::SetAnchorRect(const RECT& rc) {
    W7T_SEH_TRY
    {
        if (m_hwnd != nullptr && GetCurrentThreadId() != m_ownerTid) {
            auto rcHeap = std::make_unique<RECT>(rc);
            if (PostMessageW(m_hwnd, WM_W8N_CMD, W8N_CMD_ANCHOR,
                             reinterpret_cast<LPARAM>(rcHeap.get()))) {
                rcHeap.release();
            } else {
                LogTagged(kLogTag, L"ancora: posta verso il thread del riquadro fallita");
            }
            return;
        }
        m_anchor = rc;
        m_haveAnchor = true;
    }
    W7T_SEH_CATCH
    W7T_SEH_END
}

void Net8Pane::Toggle() {
    W7T_SEH_TRY
    {
        if (m_hwnd != nullptr && GetCurrentThreadId() != m_ownerTid) {
            if (!PostMessageW(m_hwnd, WM_W8N_CMD, W8N_CMD_TOGGLE, 0)) {
                LogTagged(kLogTag, L"toggle: posta verso il thread del riquadro fallita");
            }
            return;
        }
        DoToggle();
    }
    W7T_SEH_CATCH
    W7T_SEH_END
}

void Net8Pane::Show() {
    W7T_SEH_TRY
    {
        if (m_hwnd != nullptr && GetCurrentThreadId() != m_ownerTid) {
            PostMessageW(m_hwnd, WM_W8N_CMD, W8N_CMD_SHOW, 0);
            return;
        }
        DoShow();
    }
    W7T_SEH_CATCH
    W7T_SEH_END
}

void Net8Pane::Hide() {
    W7T_SEH_TRY
    {
        if (m_hwnd != nullptr && GetCurrentThreadId() != m_ownerTid) {
            PostMessageW(m_hwnd, WM_W8N_CMD, W8N_CMD_HIDE, 0);
            return;
        }
        DoHide();
    }
    W7T_SEH_CATCH
    W7T_SEH_END
}

void Net8Pane::SetLanguage(int lang) {
    W7T_SEH_TRY
    {
        if (m_hwnd != nullptr && GetCurrentThreadId() != m_ownerTid) {
            PostMessageW(m_hwnd, WM_W8N_CMD, W8N_CMD_LANG, (LPARAM)lang);
            return;
        }
        /* Vista come modifica di stato: tutto si ridisegna. */
        m_langHeld = lang;  /* (membro sotto) */
        if (m_hwnd != nullptr) InvalidateRect(m_hwnd, nullptr, TRUE);
    }
    W7T_SEH_CATCH
    W7T_SEH_END
}

bool Net8Pane::IsVisible() const {
    W7T_SEH_TRY
    return m_hwnd != nullptr &&
           m_state != PaneState::Closed &&
           m_state != PaneState::SlidingOut &&
           IsWindowVisible(m_hwnd) != FALSE;
    W7T_SEH_CATCH
    W7T_SEH_END
    return false;
}

void Net8Pane::ShutdownIfCreated() {
    Net8Pane* pane = PaneIfCreated();
    if (pane != nullptr) {
        /* DLL_PROCESS_DETACH: niente finestre, niente messaggi: solo i
         * font GDI appartengono davvero alla DLL da scaricare. */
        pane->FreeFonts();
    }
    /* Nessuna logica esterna da chiudere: e' del modulo Win7 e ci pensa il
     * suo Uninit in separata sede. */
}

/* ------------------------------------------------------------------ */
/*  Registrazione classe                                                   */
/* ------------------------------------------------------------------ */

void Net8Pane::RegisterClassOnce() {
    if (m_classRegistered) {
        return;
    }
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &Net8Pane::WndProcThunk;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;      /* no erase flicker: tutto da WM_PAINT */
    wc.lpszClassName = kClassName;
    wc.style = CS_DBLCLKS;
    if (RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
        m_classRegistered = true;
    } else {
        LogTagged(kLogTag, L"RegisterClassExW fallita (%lu)", GetLastError());
    }
}

/* ------------------------------------------------------------------ */
/*  Creazione finestra (sul thread proprietario)                            */
/* ------------------------------------------------------------------ */

static UINT W8DpiNow(HWND hwnd) {
    const UINT dpi = GetDpiForWindowSafe(hwnd);
    return dpi < 96 ? 96 : dpi;
}

bool Net8Pane::EnsureWindow() {
    if (m_hwnd != nullptr && IsWindow(m_hwnd)) {
        return true;
    }
    RegisterClassOnce();
    if (!m_classRegistered) {
        return false;
    }
    m_dpi = W8DpiNow(nullptr);
    EnsureFonts();

    /* Il pannello e' a tutta altezza sul bordo destro (lo posizioniamo
     * fuori-schermo: la macchina degli stati lo fa scivolare dentro). */
    int baseX = 0, cx = 0, cy = 0, top = 0;
    ComputePlacement(baseX, cx, cy, top);
    const int left = baseX + Scale(kWidthBase);   /* tutto fuori, a destra */

    m_hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        kClassName, L"", WS_POPUP,
        left, top, cx, cy,
        nullptr, nullptr, GetModuleHandleW(nullptr),
        reinterpret_cast<LPVOID>(this));
    if (m_hwnd == nullptr) {
        LogTagged(kLogTag, L"CreateWindowExW fallita (%lu)", GetLastError());
        return false;
    }
    m_ownerTid = GetCurrentThreadId();
    g_createdPane = this;
    m_slideOffset = 0;
    m_scrollY = 0;
    m_selected = -1;
    m_hot = -1;
    return true;
}

/* ------------------------------------------------------------------ */
/*  Macchina a stati: apertura / chiusura                                   */
/* ------------------------------------------------------------------ */

void Net8Pane::DoToggle() {
    switch (m_state) {
        case PaneState::Open:
        case PaneState::SlidingIn:
            DoHide();
            break;
        case PaneState::SlidingOut:
            /* Re-apertura mentre esce: si inverte dal punto corrente. */
            DoShow();
            break;
        case PaneState::Closed:
            DoShow();
            break;
    }
}

void Net8Pane::DoShow() {
    if (!EnsureWindow()) {
        LogTagged(kLogTag, L"apertura: finestra non creabile");
        return;
    }
    if (m_state == PaneState::SlidingOut) {
        w7tnet::W8NetLogic_HideWin7FlyoutIfOpen();
        BeginSlide(true);        /* inversione dolce */
        return;
    }
    if (m_state != PaneState::Closed) {
        return;                  /* gia' dentro (o sta entrando) */
    }
    /* Esclusione reciproca: lo stesso angolo ospita UN solo riquadro. Se il
     * riquadro Windows 7 era rimasto visibile (es. cambio di modalita'
     * avvenuto a riquadro aperto, o hotkey della mod), si chiude. */
    w7tnet::W8NetLogic_HideWin7FlyoutIfOpen();
    /* Posizione e dpi del momento (schermo/monitor di ancoraggio). Se il
     * monitor e' cambiato di scala dall'ultima apertura i font si rifanno:
     * dimensioni testo e geometria devono raccontare lo stesso dpi. */
    const UINT newDpi = W8DpiNow(m_hwnd);
    if (newDpi != m_dpi) {
        m_dpi = newDpi;
        FreeFonts();
    }
    EnsureFonts();
    int x = 0, cx = 0, cy = 0, top = 0;
    ComputePlacement(x, cx, cy, top);
    SetWindowPos(m_hwnd, HWND_TOPMOST,
                 x + cx, top, cx, cy,              /* tutto fuori, a destra */
                 SWP_SHOWWINDOW | SWP_NOACTIVATE);
    m_slideOffset = 0;
    m_selected = -1;
    m_hot = -1;
    m_scrollY = 0;
    m_dotPhase = 0;
    m_errorSsid[0] = L'\0';

    /* Stato iniziale: prima lettura (puo' dire "non disponibile" senza
     * bloccare) + richiesta alla logica (asincrona, no-op se dorme). */
    CascadeRefreshOnShow();

    BeginSlide(true);
}

void Net8Pane::DoHide() {
    if (m_hwnd == nullptr || !IsWindow(m_hwnd)) {
        m_state = PaneState::Closed;
        return;
    }
    if (m_state == PaneState::Closed) {
        return;
    }
    BeginSlide(false);
}

/* Cascata di aggiornamento all'apertura: capture subito e domanda asincrona
 * alla logica per forzare una scansione fresca. */
void Net8Pane::CascadeRefreshOnShow() {
    RefreshState(true);
}

/* ------------------------------------------------------------------ */
/*  Animazione                                                                */
/* ------------------------------------------------------------------ */

/* ease-out cubica su 0..1 */
static double W8EaseOut(double t) {
    const double inv = 1.0 - t;
    return 1.0 - inv * inv * inv;
}

void Net8Pane::BeginSlide(bool opening) {
    if (m_hwnd == nullptr) return;
    KillTimer(m_hwnd, kTimerAnim);
    m_animFromOffset = m_slideOffset;
    m_animStartTick = GetTickCount();
    m_state = opening ? PaneState::SlidingIn : PaneState::SlidingOut;
    if (opening) {
        SetTimer(m_hwnd, kTimerAnim, kAnimTickMs, nullptr);
        /* come il pannello della batteria: il riquadro prende il primo
         * piano, cosi' il primo clic fuori lo disattiva e si chiude. */
        SetForegroundWindow(m_hwnd);
        SetTimer(m_hwnd, kTimerRefresh, kRefreshTickMs, nullptr);
    } else {
        SetTimer(m_hwnd, kTimerAnim, kAnimTickMs, nullptr);
    }
    OnAnimTick();                 /* primo passo subito: niente lampi */
}

void Net8Pane::OnAnimTick() {
    if (m_hwnd == nullptr) return;
    int x = 0, cx = 0, cy = 0, top = 0;
    ComputePlacement(x, cx, cy, top);

    const bool opening = (m_state == PaneState::SlidingIn);
    const DWORD dur = opening ? (DWORD)kAnimInMs : (DWORD)kAnimOutMs;
    const DWORD elapsed = GetTickCount() - m_animStartTick;
    double t = dur == 0 ? 1.0 : (double)elapsed / (double)dur;
    if (t > 1.0) t = 1.0;
    if (t < 0.0) t = 0.0;
    const double k = opening ? W8EaseOut(t) : t;   /* uscita lineare */
    const int from = m_animFromOffset;
    const int to = opening ? cx : 0;
    m_slideOffset = from + (int)((double)(to - from) * k);

    const int left = x + (cx - m_slideOffset);
    SetWindowPos(m_hwnd, HWND_TOPMOST, left, top, cx, cy,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(m_hwnd, nullptr, FALSE);

    if (t >= 1.0) {
        KillTimer(m_hwnd, kTimerAnim);
        if (opening) {
            m_state = PaneState::Open;
            OnPaneShown();
        } else {
            ShowWindow(m_hwnd, SW_HIDE);
            m_state = PaneState::Closed;
            OnPaneGone();
        }
    }
}

void Net8Pane::OnPaneShown() {
    /* Dots per la connessione in corso (ticker separato e leggero). */
    for (int i = 0; i < m_rowCount; ++i) {
        if (m_rows[i].state == (int)w7tnet::W8NET_STATE_CONNECTING) {
            if (m_hwnd != nullptr) SetTimer(m_hwnd, kTimerDots, kDotsTickMs, nullptr);
            break;
        }
    }
    LogTagged(kLogTag, L"riquadro aperto");
}

void Net8Pane::OnPaneGone() {
    if (m_hwnd != nullptr) {
        KillTimer(m_hwnd, kTimerAnim);
        KillTimer(m_hwnd, kTimerRefresh);
        KillTimer(m_hwnd, kTimerDots);
    }
    m_selected = -1;
    m_hot = -1;
    m_scrollY = 0;
    m_dotPhase = 0;
    LogTagged(kLogTag, L"riquadro chiuso");
}

/* ------------------------------------------------------------------ */
/*  Geometria                                                                 */
/* ------------------------------------------------------------------ */

void Net8Pane::ComputePlacement(int& outX, int& outCX, int& outCY, int& outTop) const {
    RECT rcWork = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    HMONITOR mon = nullptr;
    if (m_haveAnchor) {
        mon = MonitorFromRect(&m_anchor, MONITOR_DEFAULTTONEAREST);
    }
    if (mon == nullptr) {
        mon = MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTONEAREST);
    }
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi)) {
        rcWork = mi.rcWork;
    }
    const int cx = Scale(kWidthBase);
    /* lastra a destra a tutta altezza della work-area del monitor dell'icona */
    outCX = cx;
    outCY = rcWork.bottom - rcWork.top;
    outX  = rcWork.right - cx;      /* X "dentro": m_slideOffset decide     */
    outTop = rcWork.top;
}

int Net8Pane::RowHeight(int index) const {
    return Scale(index == m_selected ? kRowHxBase : kRowHBase);
}

int Net8Pane::RowTop(int index) const {
    int y = Scale(kListTopBase);
    for (int i = 0; i < index; ++i) {
        y += RowHeight(i);
    }
    return y;
}

int Net8Pane::ContentHeight() const {
    int h = Scale(kListTopBase);
    for (int i = 0; i < m_rowCount; ++i) {
        h += RowHeight(i);
    }
    return h;
}

RECT Net8Pane::ButtonRectForRow(int index) const {
    RECT rc = { 0, 0, Scale(kWidthBase), 0 };
    const int bw = Scale(kBtnWBase);
    const int bh = Scale(kBtnHBase);
    const int top = RowTop(index) + Scale(kRowHBase) + Scale(6) - m_scrollY;
    rc.left = Scale(kWidthBase) - Scale(kPadBase) - bw;
    rc.right = rc.left + bw;
    rc.top = top;
    rc.bottom = top + bh;
    return rc;
}

int Net8Pane::HitRow(const POINT& ptClient) const {
    const int listTop = Scale(kListTopBase);
    if (ptClient.y < listTop) return -1;
    int y = listTop - m_scrollY;
    for (int i = 0; i < m_rowCount; ++i) {
        const int h = RowHeight(i);
        if (ptClient.y >= y && ptClient.y < y + h) {
            return i;
        }
        y += h;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/*  Font / risorse GDI                                                      */
/* ------------------------------------------------------------------ */

static HFONT W8MakeFont(int px, int weight, const wchar_t* family, const wchar_t* fallbackFamily) {
    HFONT f = CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, family);
    if (f == nullptr && fallbackFamily != nullptr) {
        f = CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, fallbackFamily);
    }
    return f;
}

void Net8Pane::EnsureFonts() {
    if (m_titleFont != nullptr) {
        return;                /* ricostruiti solo su cambio DPI/caduta      */
    }
    /* Segoe UI (con Semilight dove esiste): i pannelli di Windows 8. */
    const int titlePx = MulDiv(20, m_dpi, 96);
    m_titleFont = W8MakeFont(titlePx, FW_LIGHT, L"Segoe UI Semilight", L"Segoe UI");
    m_nameFont  = W8MakeFont(MulDiv(13, m_dpi, 96), FW_NORMAL, L"Segoe UI", L"Tahoma");
    m_subFont   = W8MakeFont(MulDiv(11, m_dpi, 96), FW_NORMAL, L"Segoe UI", L"Tahoma");
    m_btnFont   = W8MakeFont(MulDiv(11, m_dpi, 96), FW_SEMIBOLD, L"Segoe UI", L"Tahoma");
    if (m_titleFont == nullptr || m_nameFont == nullptr ||
        m_subFont == nullptr || m_btnFont == nullptr) {
        /* Senza font non si disegna: i paint faranno a meno (testo null). */
        LogTagged(kLogTag, L"EnsureFonts: creazione di un font fallita (%lu)", GetLastError());
    }
    if (m_bgBrush == nullptr) {
        m_bgBrush = CreateSolidBrush(kBg);
    }
    if (m_bgBrush == nullptr) {
        LogTagged(kLogTag, L"EnsureFonts: pennello di sfondo non creato (%lu)", GetLastError());
    }
}

void Net8Pane::FreeFonts() {
    if (m_titleFont != nullptr) { DeleteObject(m_titleFont); m_titleFont = nullptr; }
    if (m_nameFont  != nullptr) { DeleteObject(m_nameFont);  m_nameFont  = nullptr; }
    if (m_subFont   != nullptr) { DeleteObject(m_subFont);   m_subFont   = nullptr; }
    if (m_btnFont   != nullptr) { DeleteObject(m_btnFont);   m_btnFont   = nullptr; }
    if (m_bgBrush   != nullptr) { DeleteObject(m_bgBrush);   m_bgBrush   = nullptr; }
}

/* ------------------------------------------------------------------ */
/*  Traduzioni: SOLO la tabella del progetto, con ripiego sull'inglese.     */
/* ------------------------------------------------------------------ */

const W8NetStrings& Net8Pane::Strings() const {
    /* 0..10 (ar). LangFromIndex ripiega sull'inglese fuori elenco. */
    return W8NetStringsFor(LangFromIndex(m_langHeld));
}

const wchar_t* Net8Pane::W8s(const wchar_t* W8NetStrings::* field) const {
    const W8NetStrings& s = Strings();
    const wchar_t* v = s.*field;
    if (v == nullptr || v[0] == L'\0') {
        v = W8NetStringsFor(Lang::En).*field;   /* mancante -> inglese      */
    }
    return v != nullptr ? v : L"";
}

/* ------------------------------------------------------------------ */
/*  Dati: snapshot dalla logica esistente                                   */
/* ------------------------------------------------------------------ */

bool Net8Pane::CaptureFromLogic() {
    W7T_SEH_TRY
    /* Unico accesso al modulo della mod: copia dello stato. Chiamata
     * breve (lock sul modello, nessuna API WLAN, nessuna UI). */
    return w7tnet::W8NetLogic_CaptureState(&m_snapshot) != FALSE;
    W7T_SEH_CATCH
    W7T_SEH_END
    LogTagged(kLogTag, L"capture: eccezione dal ponte della logica");
    return false;
}

void Net8Pane::RefreshState(bool askLogic) {
    W7T_SEH_TRY
    {
        if (askLogic) {
            /* Domanda asincrona al thread del modulo: la risposta la
             * leggeremo al prossimo capture. */
            w7tnet::W8NetLogic_RequestRefresh();
        }
        m_haveSnapshot = CaptureFromLogic();
        RebuildRows();

        /* Riga assente/sparita: niente selezioni che puntano nel vuoto. */
        if (m_selected >= m_rowCount) m_selected = -1;

        /* Connessione in corso? Serve il dots ticker. */
        bool anyConnecting = false;
        for (int i = 0; i < m_rowCount; ++i) {
            if (m_rows[i].state == (int)w7tnet::W8NET_STATE_CONNECTING) anyConnecting = true;
        }
        if (m_hwnd != nullptr) {
            if (anyConnecting) {
                SetTimer(m_hwnd, kTimerDots, kDotsTickMs, nullptr);
            } else {
                KillTimer(m_hwnd, kTimerDots);
            }
        }

        /* Se la lista resta vuota a pannello aperto, un nuovo giro di
         * richiesta ogni ~4s al massimo (guardato dal tick). */
        if (m_rowCount == 0 && m_haveSnapshot) {
            const DWORD now = GetTickCount();
            if (now - m_lastEmptyAskTick > 4000) {
                m_lastEmptyAskTick = now;
                w7tnet::W8NetLogic_RequestRefresh();
            }
        }

        if (m_hwnd != nullptr) InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    W7T_SEH_CATCH
    W7T_SEH_END
}

void Net8Pane::RebuildRows() {
    m_rowCount = 0;
    if (!m_haveSnapshot) {
        return;
    }
    /* Ethernet for first: la connessione cablata sta QUI in Windows 8. */
    if (m_snapshot.ethernetConnected && m_rowCount < w7tnet::kW8NetMaxItems + 1) {
        PaneRow& r = m_rows[m_rowCount++];
        ZeroMemory(&r, sizeof(r));
        r.ethernet = 1;
        r.secured = 1;
        r.signalPercent = 100;
        r.state = (int)w7tnet::W8NET_STATE_CONNECTED;
        StringCchCopyW(r.ssid, ARRAYSIZE(r.ssid), m_snapshot.ethernetName);
        StringCchCopyW(r.label, ARRAYSIZE(r.label), m_snapshot.ethernetName);
        if (r.label[0] == L'\0') {
            StringCchCopyW(r.label, ARRAYSIZE(r.label), L"Ethernet");
        }
    }
    for (int i = 0; i < m_snapshot.itemCount && m_rowCount < w7tnet::kW8NetMaxItems + 1; ++i) {
        const w7tnet::W8NetItem& src = m_snapshot.items[i];
        PaneRow& r = m_rows[m_rowCount++];
        ZeroMemory(&r, sizeof(r));
        StringCchCopyW(r.ssid, ARRAYSIZE(r.ssid), src.ssid);
        if (src.displaySuffix >= 2) {
            StringCchPrintfW(r.label, ARRAYSIZE(r.label), L"%s %d", src.ssid, src.displaySuffix);
        } else {
            StringCchCopyW(r.label, ARRAYSIZE(r.label), src.ssid);
        }
        r.signalPercent = src.signalPercent;
        r.state = src.connState;
        r.secured = src.secured;
    }
    /* Ordine: rete connessa in cima (come fa il pannello di Windows 8),
     * le altre nello stesso ordine della logica (che le ha gia' ordinate
     * per disponibilita'/segnale). */
    if (m_rowCount > 1) {
        std::stable_sort(m_rows, m_rows + m_rowCount,
            [](const PaneRow& a, const PaneRow& b) {
                const bool ca = a.state == (int)w7tnet::W8NET_STATE_CONNECTED;
                const bool cb = b.state == (int)w7tnet::W8NET_STATE_CONNECTED;
                return ca && !cb;
            });
    }
}

/* ------------------------------------------------------------------ */
/*  Azioni dell'utente -> istruzioni alla STESSA logica                     */
/* ------------------------------------------------------------------ */

void Net8Pane::OnSelect(int index) {
    m_selected = (m_selected == index) ? -1 : index;
    if (m_hwnd != nullptr) InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Net8Pane::OnActivateRow(int index) {
    if (index < 0 || index >= m_rowCount) return;
    const PaneRow& r = m_rows[index];
    if (r.ethernet) {
        /* Riusinga il modello del pannello Windows 8: la voce Ethernet
         * non si connette/disconnette. */
        return;
    }
    if (r.state == (int)w7tnet::W8NET_STATE_CONNECTED ||
        r.state == (int)w7tnet::W8NET_STATE_CONNECTING) {
        TryDisconnect(r);
    } else {
        TryConnect(r);
    }
}

void Net8Pane::TryConnect(const PaneRow& row) {
    W7T_SEH_TRY
    {
        const int rc = w7tnet::W8NetLogic_Connect(row.ssid);
        if (rc != 0) {
            LogTagged(kLogTag, L"connect richiesta non instradata (rc=%d)", rc);
            StringCchCopyW(m_errorSsid, ARRAYSIZE(m_errorSsid), row.ssid);
            m_errorUntilTick = GetTickCount() + 6000;
        } else {
            LogTagged(kLogTag, L"connect richiesto alla logica");
            m_errorSsid[0] = L'\0';
            /* Chiedi pure un rinfresco: lo stato "Connecting..." arriva
             * dallo snapshot successivo (modulo che vive sul suo thread). */
            w7tnet::W8NetLogic_RequestRefresh();
        }
        RefreshState(false);
    }
    W7T_SEH_CATCH
    W7T_SEH_END
}

void Net8Pane::TryDisconnect(const PaneRow& row) {
    W7T_SEH_TRY
    {
        const int rc = w7tnet::W8NetLogic_Disconnect(row.ssid);
        if (rc != 0) {
            LogTagged(kLogTag, L"disconnect richiesta non instradata (rc=%d)", rc);
            StringCchCopyW(m_errorSsid, ARRAYSIZE(m_errorSsid), row.ssid);
            m_errorUntilTick = GetTickCount() + 6000;
        } else {
            LogTagged(kLogTag, L"disconnect richiesto alla logica");
            m_errorSsid[0] = L'\0';
            w7tnet::W8NetLogic_RequestRefresh();
        }
        RefreshState(false);
    }
    W7T_SEH_CATCH
    W7T_SEH_END
}

/* ------------------------------------------------------------------ */
/*  Disegno                                                                   */
/* ------------------------------------------------------------------ */

void Net8Pane::DrawBackground(HDC hdc, const RECT& client) {
    if (m_bgBrush != nullptr) {
        FillRect(hdc, &client, m_bgBrush);
    }
    /* Bordo sinistro 1px: il taglio che separa la lastra da cio' che resta sotto. */
    HPEN pen = CreatePen(PS_SOLID, 1, kEdge);
    if (pen != nullptr) {
        ScopedGdiObject penSel(hdc, pen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, client.left, client.top, client.right, client.bottom);
        SelectObject(hdc, oldBrush);
    }
}

void Net8Pane::DrawTitle(HDC hdc, const RECT& client) {
    if (m_titleFont == nullptr) return;
    RECT rc = { client.left + Scale(kPadBase), client.top + Scale(kTitleTopBase),
                client.right - Scale(kPadBase), client.top + Scale(kTitleTopBase) + Scale(kTitleHBase) };
    HGDIOBJ oldFont = SelectObject(hdc, m_titleFont);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, kText);
    DrawTextW(hdc, W8s(&W8NetStrings::title), -1, &rc,
              DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(hdc, oldFont);
}

void Net8Pane::DrawSignalBars(HDC hdc, const RECT& rc, int qualityPercent) const {
    /* Cinque taccheggi "a cuneo" crescenti, come il simbolo di Windows 8. */
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;
    const int bars = 5;
    const int gap = MulDiv(2, m_dpi, 96);
    const int unit = (w - (bars - 1) * gap) / bars;
    if (unit <= 0) return;
    const int filled = qualityPercent <= 0 ? 0
        : (qualityPercent + 19) / 20;                /* 0..5 */
    HPEN penOff = CreatePen(PS_SOLID, unit, kBarOff);
    HPEN penOn = CreatePen(PS_SOLID, unit, kText);
    HGDIOBJ oldPen = SelectObject(hdc, penOn != nullptr ? penOn : penOff);
    for (int i = 0; i < bars; ++i) {
        const int x = rc.left + i * (unit + gap) + unit / 2;
        const int yTop = rc.bottom - ((i + 1) * h / bars) + MulDiv(1, m_dpi, 96);
        HPEN use = (i < filled) ? penOn : penOff;
        if (use != nullptr) SelectObject(hdc, use);
        MoveToEx(hdc, x, rc.bottom - MulDiv(1, m_dpi, 96), nullptr);
        LineTo(hdc, x, yTop);
    }
    SelectObject(hdc, oldPen);
    if (penOn != nullptr) DeleteObject(penOn);
    if (penOff != nullptr) DeleteObject(penOff);
}

void Net8Pane::DrawEthernetGlyph(HDC hdc, const RECT& rc) const {
    /* Piccolo monitor col cavo: l'icona delle reti cablate in Windows 8. */
    const int sw = Scale(18), sh = Scale(12);
    const int x = rc.left + (rc.right - rc.left - sw) / 2;
    const int y = rc.top + (rc.bottom - rc.top - sh - Scale(4)) / 2;
    HPEN pen = CreatePen(PS_SOLID, Scale(1), kSubText);
    if (pen == nullptr) return;
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, x, y, x + sw, y + sh);
    MoveToEx(hdc, x + sw / 2, y + sh, nullptr);
    LineTo(hdc, x + sw / 2, y + sh + Scale(3));
    MoveToEx(hdc, x + sw / 4, y + sh + Scale(3), nullptr);
    LineTo(hdc, x + sw - sw / 4, y + sh + Scale(3));
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void Net8Pane::DrawWorkingDots(HDC hdc, int x, int y) const {
    /* Fibbia dei 4 puntini di Windows 8 durante la connessione. */
    const int r = Scale(2);
    const int step = Scale(8);
    HBRUSH on = CreateSolidBrush(kText);
    HBRUSH off = CreateSolidBrush(RGB(0x70, 0x70, 0x70));
    HGDIOBJ oldBrush = nullptr;
    for (int i = 0; i < 4; ++i) {
        HBRUSH use = (i == (m_dotPhase % 4)) ? on : off;
        if (use == nullptr) continue;
        const RECT rc = { x + i * step - r, y - r, x + i * step + r, y + r };
        oldBrush = SelectObject(hdc, use);
        Ellipse(hdc, rc.left, rc.top, rc.right, rc.bottom);
    }
    if (oldBrush != nullptr) SelectObject(hdc, oldBrush);
    if (on != nullptr) DeleteObject(on);
    if (off != nullptr) DeleteObject(off);
}

void Net8Pane::DrawRows(HDC hdc, const RECT& client) {
    if (m_rowCount == 0) {
        DrawEmpty(hdc, client);
        return;
    }
    const int listTop = Scale(kListTopBase);
    int clipSave = SaveDC(hdc);
    RECT clip = { client.left, client.top + listTop, client.right, client.bottom };
    IntersectClipRect(hdc, clip.left, clip.top, clip.right, clip.bottom);

    SetBkMode(hdc, TRANSPARENT);
    int y = listTop - m_scrollY;
    for (int i = 0; i < m_rowCount; ++i) {
        const int h = RowHeight(i);
        RECT rcRow = { client.left, y, client.right, y + h };
        if (rcRow.bottom >= clip.top && rcRow.top <= clip.bottom) {
            DrawRowContent(hdc, i, rcRow);
        }
        y += h;
    }
    RestoreDC(hdc, clipSave);
}

void Net8Pane::DrawRowContent(HDC hdc, int index, const RECT& rowRc) {
    const PaneRow& r = m_rows[index];
    const bool hot  = (index == m_hot);
    const bool open = (index == m_selected);

    if (hot || open) {
        HBRUSH hb = CreateSolidBrush(kRowHot);
        if (hb != nullptr) {
            FillRect(hdc, &rowRc, hb);
            DeleteObject(hb);
        }
    }
    if (open) {
        /* Barra di accento a sinistra (il tocco "tema" dei pannelli Win8). */
        RECT bar = { rowRc.left, rowRc.top, rowRc.left + Scale(4), rowRc.bottom };
        HBRUSH hb = CreateSolidBrush(kAccent);
        if (hb != nullptr) {
            FillRect(hdc, &bar, hb);
            DeleteObject(hb);
        }
    }

    const int textLeft = rowRc.left + Scale(kPadBase);
    const int iconW = r.ethernet ? Scale(kSignalWBase) : Scale(kSignalWBase);
    RECT rcName = { textLeft, rowRc.top + Scale(6),
                    rowRc.right - Scale(kPadBase) - iconW,
                    rowRc.top + Scale(6) + Scale(18) };
    RECT rcSub  = { textLeft, rowRc.top + Scale(26),
                    rowRc.right - Scale(kPadBase) - iconW,
                    rowRc.bottom - Scale(4) };

    /* ----------------------- nome rete ----------------------- */
    if (m_nameFont != nullptr) {
        HGDIOBJ oldFont = SelectObject(hdc, m_nameFont);
        SetTextColor(hdc, kText);
        DrawTextW(hdc, r.label, -1, &rcName,
                  DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(hdc, oldFont);
    }

    /* ------------------- sottotitolo di stato ------------------ */
    const wchar_t* sub = nullptr;
    COLORREF subColor = kSubText;
    switch ((w7tnet::W8NetConnState)r.state) {
        case w7tnet::W8NET_STATE_CONNECTED:     sub = W8s(&W8NetStrings::connected); break;
        case w7tnet::W8NET_STATE_CONNECTING:    sub = W8s(&W8NetStrings::connecting); break;
        case w7tnet::W8NET_STATE_DISCONNECTING: sub = W8s(&W8NetStrings::disconnecting); break;
        case w7tnet::W8NET_STATE_ERROR:
            sub = W8s(&W8NetStrings::cantConnect);
            subColor = kErrText;
            break;
        default:
            sub = r.ethernet ? L"" : (r.secured ? W8s(&W8NetStrings::secured)
                                                 : W8s(&W8NetStrings::open));
            break;
    }
    /* errore da richiesta non instradata: prevale sullo stato se la riga e'
     * ancora presente ed e' la stessa rete */
    if (m_errorSsid[0] != L'\0' && GetTickCount() < m_errorUntilTick &&
        lstrcmpiW(m_errorSsid, r.ssid) == 0) {
        sub = W8s(&W8NetStrings::cantConnect);
        subColor = kErrText;
    }

    const bool connecting = ((w7tnet::W8NetConnState)r.state ==
                             w7tnet::W8NET_STATE_CONNECTING);

    /* --------------------- icona di destra --------------------- */
    RECT rcIcon = { rowRc.right - Scale(kPadBase) - iconW + Scale(2),
                    rowRc.top + (Scale(kRowHBase) - Scale(14)) / 2,
                    rowRc.right - Scale(kPadBase) - Scale(2),
                    rowRc.top + (Scale(kRowHBase) + Scale(14)) / 2 };
    if (r.ethernet) {
        DrawEthernetGlyph(hdc, rcIcon);
    } else if (connecting) {
        DrawWorkingDots(hdc, rcIcon.left, rcIcon.top + (rcIcon.bottom - rcIcon.top) / 2);
    } else {
        DrawSignalBars(hdc, rcIcon, r.signalPercent);
    }

    if (sub != nullptr && sub[0] != L'\0' && m_subFont != nullptr) {
        HGDIOBJ oldFont = SelectObject(hdc, m_subFont);
        SetTextColor(hdc, subColor);
        RECT rcSubDraw = rcSub;
        DrawTextW(hdc, sub, -1, &rcSubDraw,
                  DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(hdc, oldFont);
    }

    /* -------------- area espansa: pulsante Connect/Disconnect -------------- */
    if (open && !r.ethernet) {
        const RECT rcBtn = ButtonRectForRow(index);
        const bool canConnect = ((w7tnet::W8NetConnState)r.state ==
                                 w7tnet::W8NET_STATE_IDLE) ||
                                ((w7tnet::W8NetConnState)r.state ==
                                 w7tnet::W8NET_STATE_ERROR);
        const bool canDisconnect = ((w7tnet::W8NetConnState)r.state ==
                                    w7tnet::W8NET_STATE_CONNECTED) ||
                                   ((w7tnet::W8NetConnState)r.state ==
                                    w7tnet::W8NET_STATE_CONNECTING);
        if (canConnect || canDisconnect) {
            HBRUSH fill = CreateSolidBrush(m_hot == -2 ? kBtnHot : kBtnBg); /* -2 = pulsante */
            if (fill != nullptr) {
                FillRect(hdc, &rcBtn, fill);
                DeleteObject(fill);
            }
            HPEN edgePen = CreatePen(PS_SOLID, Scale(1), kBtnEdge);
            if (edgePen != nullptr) {
                HGDIOBJ oldPen = SelectObject(hdc, edgePen);
                HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Rectangle(hdc, rcBtn.left, rcBtn.top, rcBtn.right, rcBtn.bottom);
                SelectObject(hdc, oldBrush);
                SelectObject(hdc, oldPen);
                DeleteObject(edgePen);
            }
            if (m_btnFont != nullptr) {
                HGDIOBJ oldFont = SelectObject(hdc, m_btnFont);
                SetTextColor(hdc, kText);
                RECT rcTxt = rcBtn;
                DrawTextW(hdc, canDisconnect ? W8s(&W8NetStrings::disconnect)
                                             : W8s(&W8NetStrings::connect),
                          -1, &rcTxt, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hdc, oldFont);
            }
        }
    }
}

void Net8Pane::DrawEmpty(HDC hdc, const RECT& client) {
    /* Due casi: pannello in attesa della logica / logica non disponibile. */
    const wchar_t* line = !m_haveSnapshot
        ? W8s(&W8NetStrings::notAvailable)
        : W8s(&W8NetStrings::noNetworks);
    if (m_subFont == nullptr) return;
    RECT rc = { client.left + Scale(kPadBase), client.top + Scale(kListTopBase) + Scale(24),
                client.right - Scale(kPadBase), client.bottom - Scale(kPadBase) };
    HGDIOBJ oldFont = SelectObject(hdc, m_subFont);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, kSubText);
    DrawTextW(hdc, line, -1, &rc, DT_LEFT | DT_TOP | DT_WORDBREAK);
    SelectObject(hdc, oldFont);
}

void Net8Pane::OnPaint() {
    if (m_hwnd == nullptr) return;
    PAINTSTRUCT ps = {};
    HDC hdc = BeginPaint(m_hwnd, &ps);
    if (hdc == nullptr) return;

    RECT client = {};
    GetClientRect(m_hwnd, &client);
    const int w = client.right - client.left;
    const int h = client.bottom - client.top;

    HDC hdcMem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
    if (hdcMem != nullptr && bmp != nullptr) {
        HGDIOBJ oldBmp = SelectObject(hdcMem, bmp);
        DrawBackground(hdcMem, client);
        DrawTitle(hdcMem, client);
        DrawRows(hdcMem, client);
        BitBlt(hdc, 0, 0, w, h, hdcMem, 0, 0, SRCCOPY);
        SelectObject(hdcMem, oldBmp);
    } else {
        LogTagged(kLogTag, L"paint: backbuffer non allocabile, disegno diretto");
        DrawBackground(hdc, client);
        DrawTitle(hdc, client);
        DrawRows(hdc, client);
    }
    if (bmp != nullptr) DeleteObject(bmp);
    if (hdcMem != nullptr) DeleteDC(hdcMem);
    EndPaint(m_hwnd, &ps);
}

/* ------------------------------------------------------------------ */
/*  WndProc                                                                     */
/* ------------------------------------------------------------------ */

LRESULT Net8Pane::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        W7T_SEH_TRY
            OnPaint();
        W7T_SEH_CATCH
        W7T_SEH_END
        return 0;

    case WM_W8N_CMD: {
        WPARAM cmd = wp;
        if (cmd == W8N_CMD_ANCHOR) {
            std::unique_ptr<RECT> heapRc(reinterpret_cast<RECT*>(lp));
            if (heapRc) {
                m_anchor = *heapRc;
                m_haveAnchor = true;
            }
        } else if (cmd == W8N_CMD_LANG) {
            m_langHeld = (int)lp;
            RefreshState(false);
        } else if (cmd == W8N_CMD_TOGGLE) {
            DoToggle();
        } else if (cmd == W8N_CMD_HIDE) {
            DoHide();
        } else if (cmd == W8N_CMD_SHOW) {
            DoShow();
        } else if (cmd == W8N_CMD_DESTROY) {
            DestroyWindow(hwnd);
        }
        return 0;
    }

    case WM_TIMER:
        if (wp == kTimerAnim) {
            OnAnimTick();
        } else if (wp == kTimerRefresh) {
            /* Esclusione reciproca anche via hotkey della mod (che apre il
             * riquadro Windows 7 senza passare dal gestore del click): se
             * il riquadro Windows 7 e' comparso, questo si eclissa (la
             * scelta dello stile resta una sola per angolo). */
            if (w7tnet::W8NetLogic_IsWin7FlyoutVisible()) {
                DoHide();
                return 0;
            }
            RefreshState(false);
        } else if (wp == kTimerDots) {
            m_dotPhase = (m_dotPhase + 1) % 4;
            if (m_hwnd != nullptr) InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_MOUSEMOVE: {
        const int row = HitRow({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
        /* -2 = sopra il pulsante della riga espansa */
        int hot = row;
        if (row >= 0 && row == m_selected && m_rowCount > row &&
            !m_rows[row].ethernet) {
            const RECT rcBtn = ButtonRectForRow(row);
            const POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            if (PtInRect(&rcBtn, pt)) hot = -2;
        }
        if (hot != m_hot) {
            m_hot = hot;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        if (!m_tracking) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            m_tracking = TrackMouseEvent(&tme) != FALSE;
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        m_tracking = false;
        if (m_hot != -1) {
            m_hot = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN: {
        const POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        const int row = HitRow(pt);
        if (row >= 0 && row == m_selected && !m_rows[row].ethernet) {
            const RECT rcBtn = ButtonRectForRow(row);
            if (PtInRect(&rcBtn, pt)) {
                OnActivateRow(row);
                return 0;
            }
        }
        if (row >= 0) {
            OnSelect(row);
        } else {
            /* clic sul titolo/vuoto: lascia aperta (il clic FUORI chiude) */
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        const int delta = GET_WHEEL_DELTA_WPARAM(wp);
        RECT client = {}; GetClientRect(hwnd, &client);
        const int listTop = Scale(kListTopBase);
        const int visible = client.bottom - listTop;
        int maxScroll = (ContentHeight() - listTop) - visible;
        if (maxScroll < 0) maxScroll = 0;
        m_scrollY -= (delta * Scale(kScrollStepBase)) / WHEEL_DELTA;
        if (m_scrollY < 0) m_scrollY = 0;
        if (m_scrollY > maxScroll) m_scrollY = maxScroll;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_KEYDOWN:
        switch (wp) {
        case VK_ESCAPE:
            DoHide();
            return 0;
        case VK_UP:
        case VK_DOWN:
            if (m_rowCount > 0) {
                if (m_selected < 0) {
                    m_selected = (wp == VK_UP) ? m_rowCount - 1 : 0;
                } else {
                    m_selected += (wp == VK_UP) ? -1 : 1;
                    if (m_selected < 0) m_selected = m_rowCount - 1;
                    if (m_selected >= m_rowCount) m_selected = 0;
                }
                /* tieni la riga visibile */
                const int listTop = Scale(kListTopBase);
                const int topAbs = RowTop(m_selected);
                const int botAbs = topAbs + RowHeight(m_selected);
                const int visTopAbs = m_scrollY + listTop;
                RECT client = {}; GetClientRect(hwnd, &client);
                const int visBotAbs = m_scrollY + client.bottom;
                if (topAbs < visTopAbs) m_scrollY = topAbs - listTop;
                else if (botAbs > visBotAbs) m_scrollY = botAbs - client.bottom;
                if (m_scrollY < 0) m_scrollY = 0;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case VK_RETURN:
            if (m_selected >= 0) OnActivateRow(m_selected);
            return 0;
        }
        break;

    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE &&
            (m_state == PaneState::Open || m_state == PaneState::SlidingIn)) {
            /* come in Windows 8: il pannello si chiude quando perde il fuoco. */
            DoHide();
        }
        break;

    case WM_DPICHANGED: {
        const UINT newDpi = LOWORD(wp);
        if (newDpi != m_dpi) {
            m_dpi = newDpi < 96 ? 96 : newDpi;
            FreeFonts();
            EnsureFonts();
            m_scrollY = 0;
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        break;
    }

    case WM_NCDESTROY:
        KillTimer(hwnd, kTimerAnim);
        KillTimer(hwnd, kTimerRefresh);
        KillTimer(hwnd, kTimerDots);
        m_hwnd = nullptr;
        m_state = PaneState::Closed;
        LogTagged(kLogTag, L"finestra distrutta");
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK Net8Pane::WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Net8Pane* pane = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        pane = reinterpret_cast<Net8Pane*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pane));
    } else {
        pane = reinterpret_cast<Net8Pane*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (pane != nullptr) {
        return pane->WndProc(hwnd, msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

/* ------------------------------------------------------------------ */
/*  Singleton di facciata (header pubblico)                                 */
/* ------------------------------------------------------------------ */

Win8NetworkFlyout& Win8NetworkFlyout::Instance() {
    static Win8NetworkFlyout s_instance;
    return s_instance;
}

/* La facciata passa tutto al pannello interno: le eccezioni sono gia'
 * gestite dentro (W7T_SEH_TRY) e ogni chiamata e' thread-safe perche'
 * convergono verso il thread proprietario della finestra. */

bool Win8NetworkFlyout::Init() {
    return PaneBoot().Init();
}

void Win8NetworkFlyout::Uninit() {
    PaneBoot().Uninit();
}

void Win8NetworkFlyout::SetAnchorRect(const RECT& iconRect) {
    PaneBoot().SetAnchorRect(iconRect);
}

void Win8NetworkFlyout::Toggle() {
    PaneBoot().Toggle();
}

void Win8NetworkFlyout::Show() {
    PaneBoot().Show();
}

void Win8NetworkFlyout::Hide() {
    PaneBoot().Hide();
}

bool Win8NetworkFlyout::IsVisible() const {
    return PaneIfCreated() != nullptr && PaneIfCreated()->IsVisible();
}

void Win8NetworkFlyout::SetLanguage(int appLang) {
    PaneBoot().SetLanguage(appLang);
}

void Win8NetworkFlyout::ShutdownIfCreated() {
    /* Vedi la controparte interna: rilascia i font GDI senza costruire il
     * singleton dallo scarico della DLL. */
    Net8Pane::ShutdownIfCreated();
}

} // namespace w7t
