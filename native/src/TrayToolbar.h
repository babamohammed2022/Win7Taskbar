/*
 * Win7Taskbar - Core nativo - Toolbar reale dell'area di notifica
 * Copyright (c) 2026 Win7Taskbar contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * ---------------------------------------------------------------------------
 * PERCHE' UN TOOLBAR WINDOW32 VERA (progetto §3)
 *
 * La shell non "disegna" la tray: la tray E' un controllo. La gerarchia
 * reale e' sempre stata
 *
 *     Shell_TrayWnd
 *       \- TrayNotifyWnd
 *            \- SysPager
 *                 \- ToolbarWindow32          (icone "promosse")
 *
 * e su Windows 10/11 esiste anche il secondo livello
 *
 *     NotifyIconOverflowWindow
 *       \- ToolbarWindow32                   (icone nascoste)
 *
 * dove la distinzione "visibile / nell'overflow" NON e' un flag di disegno:
 * e' il bit TBSTATE_HIDDEN della barra principale (modello storico 9x/2003,
 * il pager mostrava una pagina di pulsanti alla volta) piu' l'appartenenza
 * al secondo toolbar (modello Vista+). Entrambi i meccanismi sono Common
 * Controls veri, pilotati dai messaggi TB_*, con TBBUTTON che referenziano
 * gli ItemID e le immagini indicizzate in un ImageList.
 *
 * Qui ricostruiamo QUEL MODELLO, nativo, nella NOSTRA gerarchia di finestre
 * (la Shell_TrayWnd fantasma che il progetto gia' crea e che riceve i
 * WM_COPYDATA delle applicazioni): ogni icona dell'area di notifica diventa
 * un pulsante reale di un ToolbarWindow32 reale, con:
 *
 *   - ImageList a 32bpp alimentata dai fotogrammi ARGB dell'icona
 *     (ImageList_Add, ricreata in-place quando l'icona cambia pixel);
 *   - TBBUTTON.idCommand = ItemID assegnato dal servizio;
 *   - TBSTATE_HIDDEN per le icone nell'overflow (stessa fonte di verita'
 *     usata dalla shell: la lista WPF del livello gestito DERIVA da qui);
 *   - TB_GETITEMRECT + ClientToScreen per i rettangoli a schermo, cioe'
 *     le risposte a Shell_NotifyIconGetRect e l'ancoraggio dei flyout,
 *     calcolati dal controllo e non stimati;
 *   - TB_INSERTBUTTON / TB_DELETEBUTTON / TB_MOVEBUTTON per l'ordine,
 *     come fa la shell quando un'icona viene aggiunta, rimossa o
 *     trascinata.
 *
 * Il toolbar vive nel nostro processo, figlio della nostra TrayNotifyWnd:
 * nessuna iniezione, nessun messaggio inventato. Il disegno visibile della
 * barra resta al tema WPF (priorita' dell'utente: comportamento prima
 * dell'aspetto), ma l'ORDINE, la VISIBILITA', la GEOMETRIA e lo STATO del
 * tray nascono qui, dal meccanismo nativo, esattamente come nella shell.
 * ---------------------------------------------------------------------------
 */

#ifndef W7T_TRAY_TOOLBAR_H
#define W7T_TRAY_TOOLBAR_H

#include "Common.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <map>
#include <mutex>
#include <vector>

namespace w7t {

/*
 * Modello a toolbar della tray. Non e' thread-safe di per se': vive sul
 * thread del TrayService (creazione e mutazioni), che e' l'unico che lo
 * consulta; la query di geometria e' protetta da mutex perche' puo' essere
 * letta anche dal percorso Shell_NotifyIconGetRect.
 */
class TrayToolbar {
public:
    static TrayToolbar& Instance();

    /* Crea la gerarchia SysPager -> ToolbarWindow32 sotto `notifyParent`
     * e costruisce l'ImageList. Idempotente: richiamo dopo Destroy(). */
    bool Create(HWND notifyParent, HINSTANCE instance);
    void Destroy();

    /* Ritaglia il pager e il toolbar nell'area della TrayNotifyWnd.
     * `clientRect` e' in coordinate del padre. */
    void SetArea(const RECT& clientRect);

    /* Allinea il contenuto della toolbar all'elenco `order` di ItemID.
     * Aggiunge cio' che manca, toglie cio' che non c'e' piu', sposta i
     * pulsanti con TB_MOVEBUTTON per rispettare l'ordine. Ritorna true se
     * la geometria e' cambiata. */
    bool ApplyOrder(const std::vector<uint32_t>& order);

    /* La finestra "NotifyIconOverflowWindow" con il SUO ToolbarWindow32 che
     * specchia le icone TBSTATE_HIDDEN: la gerarchia reale di Explorer,
     * ricostruita qui perche' il riquadro nascosto sia un controllo vero e
     * non un disegno (e perche' chi cerca quella classe la trovi). */
    bool ApplyOverflowOrder(const std::vector<uint32_t>& order);

    /* Immagine di un pulsante: 32bpp dall'ARGB, sostituita in-place. */
    void SetButtonImage(uint32_t id, const ArgbBitmap& pixels);

    /* Visibilita' come la shell: TBSTATE_HIDDEN per l'overflow. */
    void SetButtonHidden(uint32_t id, bool hidden);
    bool IsButtonHidden(uint32_t id) const;

    /* Testo del pulsante (TB_ADDSTRINGW + iString), per i tooltip nativi. */
    void SetButtonText(uint32_t id, const std::wstring& text);

    void RemoveButton(uint32_t id);

    int  ButtonCount() const;
    int  IndexOf(uint32_t id) const;

    /* Rettangolo a schermo del pulsante via TB_GETITEMRECT + ClientToScreen.
     * True se il pulsante esiste e non e' nascosto. */
    bool GetItemScreenRect(uint32_t id, RECT& out) const;

    /* Hit test a schermo (TB_HITTEST) -> ItemID, 0 se vuoto. */
    uint32_t HitTest(const POINT& screenPoint) const;

private:
    TrayToolbar() = default;
    ~TrayToolbar();
    TrayToolbar(const TrayToolbar&) = delete;
    TrayToolbar& operator=(const TrayToolbar&) = delete;

    int EnsureSlot(uint32_t id);   /* indice ImageList del pulsante */

    /* Corpo comune dei due "apply": lavora su UNA toolbar (principale o
     * overflow) con la SUA mappa id->indice. Chiamato con m_mutex trattenuto
     * dai due wrapper pubblici. */
    static bool ApplyToToolbar(HWND wnd, std::map<uint32_t, int>& idMap,
                               const std::vector<uint32_t>& order,
                               TrayToolbar& self);

    HWND                 m_pager   = nullptr;   /* SysPager             */
    HWND               m_toolbar = nullptr;   /* ToolbarWindow32      */
    HWND               m_overflowWnd = nullptr;  /* NotifyIconOverflowWindow */
    HWND               m_overflowBar = nullptr;  /* toolbar del riquadro   */
    bool               m_areaValid = false;    /* SetArea chiamato con rect valido */
    HIMAGELIST           m_images  = nullptr;
    std::map<uint32_t, int>   m_idToIndex;   /* ItemID -> indice TB    */
    std::map<uint32_t, int>   m_ovfToIndex;   /* id -> indice overflow  */
    std::map<uint32_t, int>   m_idToImage;   /* ItemID -> indice ImageList */
    std::map<uint32_t, int>   m_idToString;  /* ItemID -> offset TB_ADDSTRING */
    mutable std::mutex   m_mutex;
    int                  m_iconSize = 16;
};

} /* namespace w7t */

#endif /* W7T_TRAY_TOOLBAR_H */
