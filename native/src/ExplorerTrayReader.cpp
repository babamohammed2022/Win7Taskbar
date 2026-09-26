/*
 * Win7Taskbar - Core nativo - Lettura delle icone gia' presenti nella tray
 *                             di Explorer
 * Copyright (c) 2026 Win7Taskbar contributors
 * Licensed under the GNU General Public License
 * version 3 or later (see the LICENSE file).
 *
 * Logica equivalente a ManagedShell.WindowsTray/ExplorerTrayService.cs
 * (cairoshell/ManagedShell, Apache 2.0). Codice C++ originale.
 */

#include "ExplorerTrayReader.h"
#include "../include/RaiiWrappers.h"
#include "ScopeGuards.h"


#include <atomic>
#include <cwchar>
#include <cwctype>
#include <set>
#include <utility>
#include <vector>

namespace w7t {
namespace {

/* Messaggi della common control Toolbar. */
constexpr UINT kTbButtonCount = WM_USER + 24;
constexpr UINT kTbGetButton   = WM_USER + 23;   /* variante Unicode: +23 */
constexpr UINT kTbGetItemRect = WM_USER + 29;   /* TB_GETITEMRECT */

/* fsState: il pulsante e' nascosto (icona nell'overflow di Explorer). */
constexpr BYTE kTbStateHidden = 8;

/* Il nome di classe Shell_TrayWnd puo' appartenere anche al nostro shim:
 * accettiamo solo la finestra il cui proprietario e' davvero Explorer. */
bool IsExplorerProcess(DWORD pid) {
    const std::wstring path = GetProcessImagePath(pid);
    if (path.empty()) {
        return false;
    }
    const size_t slash = path.find_last_of(L"\\\\/");
    const std::wstring name = slash == std::wstring::npos
        ? path : path.substr(slash + 1);
    return _wcsicmp(name.c_str(), L"explorer.exe") == 0;
}

/* Attesa massima per un messaggio alla toolbar di Explorer. Abbastanza lunga
 * da tollerare un Explorer sotto carico all'avvio, abbastanza corta da non
 * congelare la nostra barra se Explorer non risponde. */
constexpr UINT kSendTimeoutMs = 1000;

/* v2.37 punto 15: bandiera di interruzione per l'uscita del processo.
 * Le letture della toolbar di Explorer sono l'unica operazione nativa
 * che puo' durare diversi secondi (tanti pulsanti x timeout di 1 s
 * ciascuno): durante la chiusura devono fermarsi subito. Le funzioni
 * pubbliche RequestAbortReads()/ReadsAbortRequested() sono definite
 * piu' sotto, fuori da questo namespace anonimo. */
std::atomic<bool> g_abortReads{ false };

/* Tetto al numero di icone che accettiamo di leggere. */
constexpr int kMaxTrayButtons = 512;

/* Struttura interna di Explorer puntata da TBBUTTON::dwData.
 *
 * Non e' documentata da Microsoft ma e' stabile da Windows 7 a Windows 11.
 * I campi che ci interessano stanno tutti all'inizio; la coda serve solo a
 * dare alla struttura la dimensione giusta perche' la lettura di memoria
 * riesca. */
#pragma pack(push, 1)
struct ExplorerTrayItemRaw {
    HWND     hWnd;
    UINT     uID;
    UINT     uCallbackMessage;
    DWORD    dwState;
    UINT     uVersion;
    HICON    hIcon;
    HANDLE   uIconDemoteTimerId;
    DWORD    dwUserPref;
    DWORD    dwLastSoundTime;
    WCHAR    szExeName[260];
    WCHAR    szIconText[260];
    UINT     uNumSeconds;
    GUID     guidItem;
};

/* TBBUTTON a 64 bit. fsState e fsStyle sono due byte seguiti da padding. */
struct TbButton64 {
    int      iBitmap;
    int      idCommand;
    BYTE     fsState;
    BYTE     fsStyle;
    BYTE     bReserved[6];
    ULONG_PTR dwData;
    INT_PTR  iString;
};
#pragma pack(pop)

/* Trova la ToolbarWindow32 dell'area di notifica.
 *
 * @param overflow true per la finestra dell'overflow ("icone nascoste"),
 *                 che su Windows 11 e' una finestra separata.
 */

/* Conta i pulsanti di una toolbar con timeout breve: serve SOLO a
 * scegliere fra piu' candidati, non alla lettura vera. */
int QuickButtonCount(HWND toolbar) {
    if (toolbar == nullptr) {
        return -1;
    }
    DWORD_PTR buttons = 0;
    if (SendMessageTimeoutW(toolbar, kTbButtonCount, 0, 0,
                            SMTO_ABORTIFHUNG | SMTO_NORMAL,
                            250, &buttons) == 0) {
        return -1;
    }
    return static_cast<int>(buttons);
}

HWND FindTrayToolbar(bool overflow) {
    if (overflow) {
        /* Windows 10/11: finestra a se' stante.
         *
         * v2.1: possono esistere PIU' finestre di overflow
         * contemporaneamente (Explorer che ne ricrea una mentre la
         * vecchia viene distrutta; strumenti terzi). Prendere la prima
         * in ordine Z significava leggere ogni tanto la finestra
         * TRANSITORIA con meno pulsanti di quelli reali: le icone
         * "mancanti" venivano poi rimosse dal modello. Si enumerano
         * tutte le candidate (entrambe le classi, tutti gli ordini Z) e
         * si sceglie la toolbar col maggior numero di pulsanti: quella
         * viva. Le finestre del NOSTRO processo sono escluse. */
        const DWORD ourPid = GetCurrentProcessId();
        HWND best = nullptr;
        int bestCount = -1;

        static const wchar_t* const kHostClasses[] = {
            L"NotifyIconOverflowWindow",
            L"TopLevelWindowForOverflowXamlIsland",  /* Win11 22H2+ */
        };
        for (const wchar_t* cls : kHostClasses) {
            HWND host = nullptr;
            while ((host = FindWindowExW(nullptr, host, cls, nullptr)) != nullptr) {
                DWORD pid = 0;
                GetWindowThreadProcessId(host, &pid);
                if (pid == 0 || pid == ourPid) {
                    continue;
                }
                HWND bar = FindWindowExW(host, nullptr, L"ToolbarWindow32", nullptr);
                if (bar == nullptr) {
                    continue;
                }
                const int count = QuickButtonCount(bar);
                if (count > bestCount) {
                    bestCount = count;
                    best = bar;
                }
            }
        }
        return best;
    }

    /* Shell_TrayWnd e' ANCHE la classe del nostro server tray: la registra
     * per ricevere le Shell_NotifyIcon delle applicazioni. FindWindowW
     * restituisce la prima della classe in ordine Z, cioe' potenzialmente la
     * NOSTRA: la lettura delle icone visibili tornerebbe vuota e ogni icona
     * importata verrebbe marcata nascosta, spingendo tutto nell'overflow.
     * Si filtra per processo, come fa AppBarService per la barra nativa. */
    const DWORD ourPid = GetCurrentProcessId();
    HWND tray = nullptr;
    while ((tray = FindWindowExW(nullptr, tray, L"Shell_TrayWnd", nullptr)) != nullptr) {
        DWORD pid = 0;
        GetWindowThreadProcessId(tray, &pid);
        if (pid != ourPid && IsExplorerProcess(pid)) {
            break;
        }
    }
    if (tray == nullptr) {
        return nullptr;
    }

    HWND notify = FindWindowExW(tray, nullptr, L"TrayNotifyWnd", nullptr);
    if (notify == nullptr) {
        return nullptr;
    }

    HWND pager = FindWindowExW(notify, nullptr, L"SysPager", nullptr);
    if (pager == nullptr) {
        /* Su alcune build la toolbar e' figlia diretta di TrayNotifyWnd. */
        return FindWindowExW(notify, nullptr, L"ToolbarWindow32", nullptr);
    }

    return FindWindowExW(pager, nullptr, L"ToolbarWindow32", nullptr);
}

/* ------------------------------------------------------------------ */
/* Windows 11: stato promosso nell'overflow                            */
/* ------------------------------------------------------------------ */

/* Il formato osservato delle sottochiavi NotifyIconSettings e' una stringa
 * decimale (in genere il valore unsigned a 64 bit visualizzato da regedit),
 * ma Microsoft non pubblica né l'algoritmo né un contratto per il nome.
 * Validiamo solo questa forma e trattiamo il nome come opaco: non
 * ricostruiamo un hash inventato a partire da exe e UID. */
bool IsNotifyIconSettingsId(const wchar_t* name)
{
    if (name == nullptr || name[0] == L'\0') {
        return false;
    }
    size_t length = 0;
    for (; name[length] != L'\0'; ++length) {
        if (name[length] < L'0' || name[length] > L'9' || length >= 20) {
            return false;
        }
    }
    return length > 0 && length <= 20;
}

bool ReadNotifyDword(HKEY key, const wchar_t* value, DWORD& out)
{
    DWORD type = 0;
    DWORD size = sizeof(out);
    if (key == nullptr || RegQueryValueExW(key, value, nullptr, &type,
                                           reinterpret_cast<BYTE*>(&out),
                                           &size) != ERROR_SUCCESS) {
        return false;
    }
    return type == REG_DWORD && size == sizeof(out);
}

bool ReadNotifyString(HKEY key, const wchar_t* value, std::wstring& out)
{
    out.clear();
    if (key == nullptr) {
        return false;
    }
    DWORD type = 0;
    DWORD bytes = 0;
    if (RegQueryValueExW(key, value, nullptr, &type, nullptr, &bytes)
            != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) ||
        bytes == 0 || bytes > 64 * 1024 || bytes % sizeof(wchar_t) != 0) {
        return false;
    }
    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1, L'\0');
    DWORD capacity = bytes;
    if (RegQueryValueExW(key, value, nullptr, &type,
                         reinterpret_cast<BYTE*>(buffer.data()), &capacity)
            != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) ||
        capacity > bytes) {
        return false;
    }
    buffer.back() = L'\0';
    out.assign(buffer.data());
    return !out.empty();
}

std::wstring NormalizeNotifyPath(const std::wstring& input)
{
    std::wstring result = input;
    while (!result.empty() &&
           (result.front() == L'"' || iswspace(result.front()))) {
        result.erase(result.begin());
    }
    while (!result.empty() &&
           (result.back() == L'"' || iswspace(result.back()))) {
        result.pop_back();
    }
    /* Alcune installazioni memorizzano la variabile d'ambiente invece del
     * percorso espanso. Se l'espansione entra nel buffer, confrontiamo la
     * forma risultante; altrimenti conserviamo quella originale. */
    wchar_t expanded[32768] = {};
    const DWORD expandedLength = ExpandEnvironmentStringsW(
        result.c_str(), expanded, ARRAYSIZE(expanded));
    if (expandedLength > 1 && expandedLength < ARRAYSIZE(expanded)) {
        result.assign(expanded, expandedLength - 1);
    }
    for (wchar_t& c : result) {
        if (c == L'/') {
            c = L'\\';
        }
        c = static_cast<wchar_t>(towlower(c));
    }
    return result;
}

std::wstring NotifyPathFileName(const std::wstring& input)
{
    const std::wstring normalized = NormalizeNotifyPath(input);
    const size_t slash = normalized.find_last_of(L"\\");
    return slash == std::wstring::npos
        ? normalized : normalized.substr(slash + 1);
}

bool NotifyExecutableMatches(const std::wstring& registeredPath,
                             const std::wstring& processPath,
                             const std::wstring& rawExeName)
{
    const std::wstring registered = NormalizeNotifyPath(registeredPath);
    if (registered.empty()) {
        return false;
    }
    const std::wstring process = NormalizeNotifyPath(processPath);
    const std::wstring raw = NormalizeNotifyPath(rawExeName);
    if ((!process.empty() && registered == process) ||
        (!raw.empty() && registered == raw)) {
        return true;
    }

    /* ExecutablePath può contenere il prefisso della cartella nota di
     * Windows (per esempio {GUID}\\programma.exe), mentre il percorso
     * ottenuto dal processo è il percorso espanso. Con UID uguale, il nome
     * finale è un disambiguatore sufficiente e non è un'euristica per l'id. */
    const std::wstring registeredName = NotifyPathFileName(registered);
    return (!process.empty() &&
            registeredName == NotifyPathFileName(process)) ||
           (!raw.empty() && registeredName == NotifyPathFileName(raw));
}

bool ReadNotifyIconPromotion(uint64_t ownerHwnd, uint32_t uid,
                             const std::wstring& rawExeName,
                             bool& known, bool& promoted)
{
    known = false;
    promoted = true;

    DWORD pid = 0;
    GetWindowThreadProcessId(
        reinterpret_cast<HWND>(static_cast<uintptr_t>(ownerHwnd)), &pid);
    const std::wstring processPath = GetProcessImagePath(pid);

    HKEY rawRoot = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Control Panel\\NotifyIconSettings", 0, KEY_READ,
                      &rawRoot) != ERROR_SUCCESS) {
        return false;
    }
    raii::RegKeyHandle root(rawRoot);

    for (DWORD index = 0;; ++index) {
        wchar_t name[64] = {};
        DWORD nameLength = static_cast<DWORD>(ARRAYSIZE(name) - 1);
        FILETIME lastWrite{};
        const LSTATUS enumStatus = RegEnumKeyExW(
            root.get(), index, name, &nameLength, nullptr, nullptr, nullptr,
            &lastWrite);
        if (enumStatus == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (enumStatus != ERROR_SUCCESS || !IsNotifyIconSettingsId(name)) {
            continue;
        }

        HKEY rawChild = nullptr;
        if (RegOpenKeyExW(root.get(), name, 0, KEY_READ, &rawChild)
                != ERROR_SUCCESS) {
            continue;
        }
        raii::RegKeyHandle child(rawChild);

        DWORD registryUid = 0;
        DWORD registryPromotion = 0;
        std::wstring registeredPath;
        if (!ReadNotifyDword(child.get(), L"UID", registryUid) ||
            registryUid != uid ||
            !ReadNotifyString(child.get(), L"ExecutablePath", registeredPath) ||
            !NotifyExecutableMatches(registeredPath, processPath, rawExeName) ||
            !ReadNotifyDword(child.get(), L"IsPromoted", registryPromotion) ||
            registryPromotion > 1) {
            continue;
        }

        known = true;
        promoted = registryPromotion == 1;
        return true;
    }
    return false;
}

/* Cattura con PrintWindow cio' che Explorer DISEGNA davvero nella toolbar
 * dell'area di notifica. E' la fonte autorevole dei pixel: la batteria che
 * cambia stato aggiorna il disegno del pulsante, non l'HICON conservato
 * nella NOTIFYICONDATA (che resta quello della registrazione: ecco perche'
 * il polling sull'HICON non vedeva mai il cambio). PrintWindow funziona
 * fra processi senza alcuna iniezione: il disegno avviene nel thread di
 * Explorer, dentro un DC nostro. */
struct ToolbarCapture {
    bool ok = false;
    int  width = 0;
    int  height = 0;
    std::vector<uint8_t> pixels;   /* BGRA, dall'alto in basso */
};

bool CaptureToolbar(HWND toolbar, ToolbarCapture& cap) {
    RECT rc = {};
    if (!GetClientRect(toolbar, &rc)) {
        return false;
    }
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        return false;
    }

    // RAII: il DC della finestra usa ReleaseDC, anche su ritorni anticipati.
    const WindowDcGuard screen(nullptr, GetDC(nullptr));
    if (!screen.valid()) {
        return false;
    }
    raii::CompatibleDcHandle mem(CreateCompatibleDC(screen.get()));
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;   /* top-down: righe come le nostre */
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    raii::BitmapHandle bmp(CreateDIBSection(screen.get(), &bi, DIB_RGB_COLORS, &bits, nullptr, 0));
    if (!mem || !bmp || bits == nullptr) {
        return false;
    }

    // Azzera il DIB: se PrintWindow non tocca l'alpha, resta 0 e lo rileviamo
    if (bits != nullptr) {
        memset(bits, 0, size_t(w) * h * 4);
    }

    raii::GdiSelector sel(mem.get(), bmp.get());
    BOOL printed = FALSE;
    DWORD_PTR printedResult = 0;

    // Tentativo 1: WM_PRINT diretto (funziona anche se barra nascosta)
    printed = SendMessageTimeoutW(toolbar, WM_PRINT,
                                  reinterpret_cast<WPARAM>(mem.get()),
                                  PRF_CLIENT | PRF_ERASEBKGND | PRF_CHILDREN,
                                  SMTO_ABORTIFHUNG | SMTO_NORMAL,
                                  kSendTimeoutMs, &printedResult) != FALSE;

    // Tentativo 2: WM_PRINT solo client
    if (printed == FALSE) {
        printed = SendMessageTimeoutW(toolbar, WM_PRINT,
                                      reinterpret_cast<WPARAM>(mem.get()),
                                      PRF_CLIENT,
                                      SMTO_ABORTIFHUNG | SMTO_NORMAL,
                                      kSendTimeoutMs, &printedResult) != FALSE;
    }

    // Tentativo 3: PrintWindow con PW_CLIENTONLY (0x1) - più affidabile su toolbar nascosta
    if (printed == FALSE) {
        printed = PrintWindow(toolbar, mem.get(), 1); // PW_CLIENTONLY
    }

    // Tentativo 4: PrintWindow con 0 (tutto)
    if (printed == FALSE) {
        printed = PrintWindow(toolbar, mem.get(), 0);
    }

    if (printed != FALSE) {
        cap.pixels.assign(static_cast<const uint8_t*>(bits),
                          static_cast<const uint8_t*>(bits) + size_t(w) * h * 4);
        cap.width  = w;
        cap.height = h;
        cap.ok     = true;

        // Se l'alpha è tutto 0 (PrintWindow non imposta alpha), impostiamo alpha=255
        bool hasAlpha = false;
        for (size_t i = 3; i < cap.pixels.size(); i += 4) {
            if (cap.pixels[i] != 0) { hasAlpha = true; break; }
        }
        if (!hasAlpha) {
            for (size_t i = 0; i + 3 < cap.pixels.size(); i += 4) {
                uint8_t b = cap.pixels[i];
                uint8_t g = cap.pixels[i+1];
                uint8_t r = cap.pixels[i+2];
                if (r != 0 || g != 0 || b != 0) {
                    cap.pixels[i+3] = 255;
                }
            }
        }
    }

    // RAII handles auto-cleanup: no DeleteObject/DeleteDC needed
    return cap.ok;
}

bool CropCapture(const ToolbarCapture& cap, const RECT& r, ArgbBitmap& out) {
    const long l = (std::max)(0L, r.left);
    const long t = (std::max)(0L, r.top);
    const long ri = (std::min)(static_cast<long>(cap.width), r.right);
    const long b = (std::min)(static_cast<long>(cap.height), r.bottom);
    if (ri - l <= 0 || b - t <= 0) {
        return false;
    }

    int w = static_cast<int>(ri - l);
    int h = static_cast<int>(b - t);
    out.width  = w;
    out.height = h;
    out.pixels.resize(size_t(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        const uint8_t* src = cap.pixels.data()
                           + (size_t(t + y) * cap.width + size_t(l)) * 4;
        memcpy(out.pixels.data() + size_t(y) * w * 4, src, size_t(w) * 4);
    }

    /* Ritaglio sui pixel opachi: il rettangolo del pulsante comprende il
     * passo della toolbar (margini inclusi), e quei margini farebbero
     * vedere l'icona rimpicciolita dentro la sua cella (centro operativo
     * piccolo dopo la 1.9.16). Si tiene solo il bounding box del disegno.
     * FIX batteria: se l'alpha è 0 ovunque (PrintWindow senza alpha), usa
     * differenza colore dallo sfondo invece dell'alpha. */
    {
        long minX = w, minY = h, maxX = -1, maxY = -1;
        bool foundByAlpha = false;
        for (int yy = 0; yy < h; ++yy) {
            for (int xx = 0; xx < w; ++xx) {
                if (out.pixels[(size_t(yy) * w + xx) * 4 + 3] >= 8) {
                    if (xx < minX) minX = xx;
                    if (xx > maxX) maxX = xx;
                    if (yy < minY) minY = yy;
                    if (yy > maxY) maxY = yy;
                    foundByAlpha = true;
                }
            }
        }

        // Se non trovato con alpha, prova con differenza colore (per batteria quando barra nascosta)
        if (!foundByAlpha && !out.pixels.empty()) {
            // Campiona lo sfondo dall'angolo in alto a sinistra
            uint8_t br = out.pixels[0];
            uint8_t bg = out.pixels[1];
            uint8_t bb = out.pixels[2];
            // Se l'angolo è nero (0,0,0) prova angolo in basso a destra
            if (br == 0 && bg == 0 && bb == 0 && out.pixels.size() >= 4) {
                size_t last = out.pixels.size() - 4;
                br = out.pixels[last];
                bg = out.pixels[last+1];
                bb = out.pixels[last+2];
            }
            for (int yy = 0; yy < h; ++yy) {
                for (int xx = 0; xx < w; ++xx) {
                    size_t idx = (size_t(yy) * w + xx) * 4;
                    int dr = int(out.pixels[idx]) - br;
                    int dg = int(out.pixels[idx+1]) - bg;
                    int db = int(out.pixels[idx+2]) - bb;
                    // Se il colore differisce abbastanza dallo sfondo, è parte dell'icona
                    if (abs(dr) > 12 || abs(dg) > 12 || abs(db) > 12) {
                        if (xx < minX) minX = xx;
                        if (xx > maxX) maxX = xx;
                        if (yy < minY) minY = yy;
                        if (yy > maxY) maxY = yy;
                    }
                }
            }
            // Se trovato, imposta alpha a 255 per quei pixel
            if (maxX >= minX) {
                for (int yy = minY; yy <= maxY; ++yy) {
                    for (int xx = minX; xx <= maxX; ++xx) {
                        size_t idx = (size_t(yy) * w + xx) * 4;
                        int dr = int(out.pixels[idx]) - br;
                        int dg = int(out.pixels[idx+1]) - bg;
                        int db = int(out.pixels[idx+2]) - bb;
                        if (abs(dr) > 12 || abs(dg) > 12 || abs(db) > 12) {
                            out.pixels[idx+3] = 255;
                        }
                    }
                }
                foundByAlpha = true; // riusato come flag generico
            }
        }

        if (maxX >= minX && maxY >= minY &&
            (maxX - minX + 1 < w || maxY - minY + 1 < h)) {
            ArgbBitmap trimmed;
            trimmed.width  = static_cast<int>(maxX - minX + 1);
            trimmed.height = static_cast<int>(maxY - minY + 1);
            trimmed.pixels.resize(size_t(trimmed.width) * trimmed.height * 4);
            for (int yy = 0; yy < trimmed.height; ++yy) {
                const uint8_t* src = out.pixels.data()
                    + (size_t(minY + yy) * w + size_t(minX)) * 4;
                memcpy(trimmed.pixels.data() + size_t(yy) * trimmed.width * 4,
                       src, size_t(trimmed.width) * 4);
            }
            out = std::move(trimmed);
            w = out.width;
            h = out.height;
        }
        if (maxX < minX) {
            return false;   /* nessun pixel opaco: cattura inutile */
        }
    }

    /* Se la toolbar ha dipinto uno sfondo piatto (barra nascosta, tema...),
     * lo si toglie campionando l'angolo: quei pixel diventano trasparenti.
     * Se il DIB era gia' trasparente (alpha 0) non si tocca nulla. */
    if (!out.pixels.empty() && out.pixels.size() >= 4) {
        // Trova un angolo che sia sfondo (alpha basso o colore uniforme)
        uint8_t br = out.pixels[0];
        uint8_t bg = out.pixels[1];
        uint8_t bb = out.pixels[2];
        uint8_t ba = out.pixels[3];
        // Se l'alpha dell'angolo è 0, non fare background removal (già trasparente)
        if (ba != 0) {
            for (size_t i = 0; i + 3 < out.pixels.size(); i += 4) {
                const int dr = int(out.pixels[i])     - br;
                const int dg = int(out.pixels[i + 1]) - bg;
                const int db = int(out.pixels[i + 2]) - bb;
                if (dr > -12 && dr < 12 && dg > -12 && dg < 12 && db > -12 && db < 12) {
                    out.pixels[i + 3] = 0;
                }
            }
        }
    }
    return true;
}

/* Legge i pulsanti di una toolbar e ne estrae le icone.
 *
 * La toolbar appartiene a Explorer: TB_GETBUTTON scrive il risultato in un
 * indirizzo del processo PROPRIETARIO, non del nostro. Bisogna quindi
 * allocare il buffer dentro Explorer con VirtualAllocEx e rileggerlo con
 * ReadProcessMemory. */
/* RAII wrappers: use generic handles and custom deleters
 * English: Use RAII to ensure handles and remote buffers are released
 */
struct ProcessHandle {
    raii::GenericHandle handle;
    HANDLE get() const { return handle.get(); }
    operator HANDLE() const { return handle.get(); }
};
struct RemoteBuffer {
    HANDLE process = nullptr;
    void*  address = nullptr;
    ~RemoteBuffer() {
        if (address != nullptr && process != nullptr) {
            VirtualFreeEx(process, address, 0, MEM_RELEASE);
        }
    }
    void reset(HANDLE proc, void* addr) { process = proc; address = addr; }
};

/* True se la passata ha letto davvero la toolbar (conteggio e pulsanti).
 * `capture` controlla la sola parte costosa: la cattura PrintWindow del
 * disegno della toolbar. Stampare la toolbar di un altro processo e' un
 * favore che Explorer ci fa: va chiesto quando serve (prima passata,
 * eventi di sistema), non a ogni giro.
 *
 * v2.1: `incompleteOut` (se non null) riceve true quando la toolbar e'
 * raggiungibile ma ALMENO UN pulsante non e' stato leggibile (timeout di
 * TB_GETBUTTON, ReadProcessMemory fallita). Una lettura incompleta NON
 * autorizza la riconciliazione a contare assenze: il pulsante mancante
 * esiste quasi certamente ancora nella toolbar di Explorer. */
bool ReadToolbar(HWND toolbar, bool fromOverflow,
                 std::vector<ExplorerTrayItem>& out, bool wantPixels,
                 bool* incompleteOut = nullptr) {
    if (toolbar == nullptr) {
        return false;
    }
    bool incomplete = false;

    /* SendMessageTimeoutW, non SendMessageW: la toolbar appartiene a
     * Explorer, quindi un SendMessage senza timeout ci bloccherebbe per
     * tutto il tempo in cui Explorer e' occupato. */
    DWORD_PTR buttons = 0;
    if (SendMessageTimeoutW(toolbar, kTbButtonCount, 0, 0,
                            SMTO_ABORTIFHUNG | SMTO_NORMAL,
                            kSendTimeoutMs, &buttons) == 0) {
        wchar_t line[160];
        wsprintfW(line, L"import toolbar=%016I64X: conteggio assente",
                  reinterpret_cast<unsigned long long>(toolbar));
        AppendCoreLog(line);
        return false;
    }

    const int count = static_cast<int>(buttons);
    if (count <= 0 || count > kMaxTrayButtons) {
        return true;   /* toolbar valida, semplicemente vuota */
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(toolbar, &pid);
    if (pid == 0) {
        return false;
    }

    ProcessHandle process;
    process.handle.reset(OpenProcess(
        PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION,
        FALSE, pid));
    if (!process.handle) {
        AppendCoreLog(L"import: OpenProcess su Explorer negato");
        return false;
    }

    RemoteBuffer remote;
    remote.process = process.handle.get();
    remote.address = VirtualAllocEx(process.handle.get(), nullptr, sizeof(TbButton64),
                                    MEM_COMMIT, PAGE_READWRITE);
    if (remote.address == nullptr) {
        return false;
    }

    /* Una sola cattura della toolbar per passata, e solo se la passata la
     * chiede: il ritaglio per pulsante costa solo memcpy. Il log di riga
     * esce solo quando cambia qualcosa, per non allagare il file come fa-
     * ceva il ciclo a 6 secondi. */
    ToolbarCapture capture;
    if (wantPixels) {
        CaptureToolbar(toolbar, capture);
    }
    static int lastVisibleCount = -1;
    static int lastOverflowCount = -1;
    const bool countChanged = fromOverflow
        ? (count != lastOverflowCount)
        : (count != lastVisibleCount);
    if (countChanged) {
        if (!fromOverflow) { lastVisibleCount = count; }
        else { lastOverflowCount = count; }
        wchar_t line[160];
        wsprintfW(line, L"import toolbar=%016I64X overflow=%d pulsanti=%d",
                  reinterpret_cast<unsigned long long>(toolbar),
                  fromOverflow ? 1 : 0, count);
        AppendCoreLog(line);
    }

    RemoteBuffer remoteRect;
    remoteRect.process = process.handle.get();
    remoteRect.address = VirtualAllocEx(process.handle.get(), nullptr, sizeof(RECT),
                                        MEM_COMMIT, PAGE_READWRITE);

    for (int i = 0; i < count; i++) {
        /* v2.37 punto 15: durante la chiusura non ha senso finire la
         * passata: si interrompe subito. La lettura risulta incompleta
         * (nessuna rimozione di icone: comportamento sicuro). */
        if (ReadsAbortRequested()) {
            incomplete = true;
            break;
        }
        DWORD_PTR ignored = 0;
        if (SendMessageTimeoutW(toolbar, kTbGetButton, static_cast<WPARAM>(i),
                                reinterpret_cast<LPARAM>(remote.address),
                                SMTO_ABORTIFHUNG | SMTO_NORMAL,
                                kSendTimeoutMs, &ignored) == 0
            || ignored == 0) {
            /* v2.1: NON e' un pulsante inesistente: e' un pulsante che
             * Explorer non ci ha lasciato leggere ADESSO (thread occupato,
             * SMTO_ABORTIFHUNG). La lettura e' incompleta: nessuna
             * conclusione sull'assenza dell'icona puo' essere tratta. */
            incomplete = true;
            continue;
        }

        TbButton64 button = {};
        SIZE_T read = 0;
        if (!ReadProcessMemory(process.handle.get(), remote.address, &button, sizeof(button), &read)
            || read != sizeof(button)
            || button.dwData == 0) {
            incomplete = true;
            continue;
        }

        ExplorerTrayItemRaw raw = {};
        if (!ReadProcessMemory(process.handle.get(), reinterpret_cast<void*>(button.dwData),
                               &raw, sizeof(raw), &read)
            || read != sizeof(raw)) {
            incomplete = true;
            continue;
        }

        if (raw.hWnd == nullptr || !IsWindow(raw.hWnd)) {
            /* Proprietario gia' morto: Explorer lo togliera' da solo a
             * breve (e il nostro EVENT_OBJECT_DESTROY lo rimuove dal
             * modello). Non inficia la validita' della lettura. */
            continue;
        }

        ExplorerTrayItem item;
        item.ownerHwnd       = reinterpret_cast<uint64_t>(raw.hWnd);
        item.uid             = raw.uID;
        item.callbackMessage = raw.uCallbackMessage;
        /* La versione con cui l'icona si e' registrata decide COME va
         * inoltrato il clic (impaccamento v4 + NIN.SELECT/WM_CONTEXTMENU).
         * Senza di essa le icone della shell (volume, rete, batteria),
         * registrate con NOTIFYICON_VERSION_4, restavano sorde: il core
         * usava l'impaccamento vecchio. Valori fuori range = dati non
         * inizializzati: si ripiega sul vecchio comportamento. */
        item.version         = (raw.uVersion <= 4) ? raw.uVersion : 0;
        item.guidItem        = raw.guidItem;
        /* TBSTATE_HIDDEN: Explorer considera l'icona nascosta (sta nel SUO
         * overflow). E' la stessa fonte dati della shell: la nostra zona
         * nascosta parte sincronizzata con quella di Explorer, come chiede
         * l'utente, invece che da un'azione manuale sull'icona. Un pulsante
         * letto dalla finestra dell'overflow e' nascosto per definizione,
         * qualunque sia il suo stato nel pager. */
        item.hidden          = fromOverflow
                            || (button.fsState & 0x0008 /*TBSTATE_HIDDEN*/) != 0;

        raw.szIconText[259] = L'\0';
        raw.szExeName[259]  = L'\0';
        item.tooltip = raw.szIconText;
        item.exeName = raw.szExeName;

        /* IsPromoted è un indizio della preferenza privata di Windows 11:
         * 1 tende alla zona visibile, 0 tende al cassetto overflow. La
         * chiave privata usa un nome decimale opaco; il lettore ha verificato
         * UID + ExecutablePath solo per associare il metadato. In assenza di
         * una corrispondenza, la toolbar resta la fonte visuale reale. */
        bool promotionKnown = false;
        bool promoted = true;
        if (ReadNotifyIconPromotion(item.ownerHwnd, item.uid, item.exeName,
                                    promotionKnown, promoted) &&
            promotionKnown) {
            /* IsPromoted è una preferenza privata di Windows 11, non una
             * fotografia assoluta: la posizione visuale resta quella della
             * toolbar/overflow appena letta. Conserviamo il dato solo come
             * metadato diagnostico per la fusione successiva. */
            item.promotionKnown = true;
            item.promoted = promoted;
        }

        /* L'HICON letto dalla memoria di Explorer e' un handle della
         * sessione (cosi' la shell disegna le icone altrui): copiarlo e'
         * pratica di shell, ma se fallisce non e' un dramma: l'icona
         * arrivera' col prossimo aggiornamento dell'applicazione. */
        if (raw.hIcon != nullptr) {
            HICON owned = CopyIcon(raw.hIcon);
            if (owned != nullptr) {
                /* v2.4: l'hIcon vivo va in iconBitmap; il servizio lo usa
                 * come fonte primaria quando cambia (batteria!). */
                IconToArgb(owned, item.iconBitmap);
                item.hasIconBitmap = BitmapSane(item.iconBitmap);
                if (!item.hasIconBitmap) {
                    item.iconBitmap = ArgbBitmap{};
                }
                DestroyIcon(owned);
            } else {
                wchar_t line[160];
                wsprintfW(line, L"import icona %u: CopyIcon negata",
                          static_cast<unsigned>(i));
                AppendCoreLog(line);
            }
        }

        /* Pixel autorevoli: il ritaglio del pulsante disegnato da Explorer.
         * Se la cattura non riesce (barra appena ricreata, tema strano...)
         * resta l'HICON della NOTIFYICONDATA, come prima. */
        if (capture.ok && remoteRect.address != nullptr) {
            RECT itemRect = {};
            DWORD_PTR rectResult = 0;
            if (SendMessageTimeoutW(toolbar, kTbGetItemRect,
                                    static_cast<WPARAM>(i),
                                    reinterpret_cast<LPARAM>(remoteRect.address),
                                    SMTO_ABORTIFHUNG | SMTO_NORMAL,
                                    kSendTimeoutMs, &rectResult) != 0
                && rectResult != 0
                && ReadProcessMemory(process.handle.get(), remoteRect.address, &itemRect,
                                     sizeof(itemRect), &read)
                && read == sizeof(itemRect)) {
                ArgbBitmap captured;
                if (CropCapture(capture, itemRect, captured)
                    && !captured.empty() && BitmapSane(captured)) {
                    item.bitmap = std::move(captured);
                    item.capturedPixels = true;
                }
            }
        }

        out.push_back(std::move(item));
    }

    if (incompleteOut != nullptr) {
        *incompleteOut = incomplete;
        if (incomplete) {
            wchar_t line[160];
            wsprintfW(line, L"import toolbar=%016I64X overflow=%d: lettura incompleta",
                      reinterpret_cast<unsigned long long>(toolbar),
                      fromOverflow ? 1 : 0);
            AppendCoreLog(line);
        }
    }
    return true;
}

} /* namespace */

/* v2.37 punto 15: definizione pubblica (fuori dal namespace anonimo). */
void RequestAbortReads() { g_abortReads.store(true); }
bool ReadsAbortRequested() { return g_abortReads.load(); }

ExplorerTrayReadResult ExplorerTrayReader::ReadAllEx(bool capture) {
    ExplorerTrayReadResult result;

    /* Lettura sola-lettura delle due toolbar di Explorer, senza COM.
     *
     * Storia onesta: il toggle ITrayNotify::EnableAutoTray che RetroBar usa
     * per far comparire anche le icone nascoste e' stato provato fino alla
     * 1.9.9 compresa. Sulla build dell'utente (Explorer patchato/a tema)
     * quell'attivazione COM faulta: prova A/B, 1.9.8 senza toggle non
     * crasha, 1.9.9 con toggle crasha di nuovo con 0xc0000005. Qui dentro
     * non entra piu'.
     *
     * Le icone nell'overflow di Explorer NON richiedono alcun toggle: la
     * finestra NotifyIconOverflowWindow (o l'isola XAML su 22H2+) esiste da
     * quando Explorer parte e la SUA toolbar vive anche a riquadro chiuso;
     * TBSTATE_HIDDEN sulla toolbar principale copre il resto. La
     * riconciliazione del servizio le tratta come "nascoste" e le mostra
     * nel proprio riquadro.
     *
     * Nota sul broadcast TaskbarCreated: alle 1.9.18 era mandato anche
     * all'avvio e faceva ri-registrare le applicazioni con uid diversi da
     * quelli che Explorer gia' conosceva: duplicati fra importato e vivo,
     * cioe' "piu' icone della tray reale". Non si manda piu' all'avvio
     * (resta all'uscita, per restituire le registrazioni alla shell) e il
     * riaggancio per GUID nel servizio ricuce comunque le ri-registrazioni
     * che arrivano dal riavvio di Explorer. */
    std::vector<ExplorerTrayItem> visible;
    std::vector<ExplorerTrayItem> overflowed;

    bool visibleIncomplete = false;
    bool overflowIncomplete = false;

    HWND visToolbar = FindTrayToolbar(false);
    result.okVisible  = ReadToolbar(visToolbar, false, visible, capture,
                                    &visibleIncomplete);
    /* Toolbar raggiungibile ma con pulsanti non letti = non valida. */
    if (visibleIncomplete) {
        result.okVisible = false;
    }
    result.emptyVisible = result.okVisible && visible.empty();
    if (result.emptyVisible) {
        /* Zero pulsanti VISIBILI: stato possibile ma ambiguo (tutte le
         * icone nell'overflow, oppure Explorer in ricostruzione). Non e'
         * mai una base per rimuovere icone dal modello. */
        result.okVisible = false;
    }

    /* L'overflow si fotografa solo se serve (prima passata / eventi): i
     * suoi pulsanti cambiano raramente e il costo e' la PrintWindow. */
    HWND ovrToolbar = FindTrayToolbar(true);
    if (ovrToolbar == nullptr) {
        result.okOverflow = true;   /* niente finestra: nessun nascosto */
    } else {
        result.okOverflow = ReadToolbar(ovrToolbar, true, overflowed, capture,
                                        &overflowIncomplete);
        if (overflowIncomplete) {
            result.okOverflow = false;
        }
    }

    using Key = std::pair<uint64_t, uint32_t>;
    const auto keyOf = [](const ExplorerTrayItem& item) -> Key {
        return Key{ item.ownerHwnd, item.uid };
    };

    std::set<Key> emitted;
    for (ExplorerTrayItem& item : visible) {
        emitted.insert(keyOf(item));
        result.items.push_back(std::move(item));
    }
    for (ExplorerTrayItem& item : overflowed) {
        if (emitted.count(keyOf(item)) == 0) {
            emitted.insert(keyOf(item));
            result.items.push_back(std::move(item));
        }
    }

    return result;
}

int32_t ExplorerTrayReader::ReadAll(std::vector<ExplorerTrayItem>& out) {
    out.clear();
    ExplorerTrayReadResult result = ReadAllEx(true);
    out = std::move(result.items);
    return static_cast<int32_t>(out.size());
}

} /* namespace w7t */
