/*
 * Win7Taskbar - Core nativo - Flyout immersivi di sistema
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
 * -------------------------------------------------------------------------
 * PROVENIENZA DEL CODICE
 * -------------------------------------------------------------------------
 * La sequenza di invocazione dei flyout immersivi (rete, orologio,
 * batteria, volume) e i relativi identificatori COM sono adattati da
 *
 *     ExplorerPatcher/ImmersiveFlyouts.h  e  ImmersiveFlyouts.c
 *     Copyright (c) Valentin-Gabriel Radu (valinet) e contributori
 *     https://github.com/valinet/ExplorerPatcher
 *     GNU General Public License v2 (o, a scelta, qualsiasi versione
 *     successiva pubblicata dalla FSF)
 *
 * essendo quell'header privo di indicazione di versione ("v2" nel
 * LICENSE del repository), il codice derivato e' qui ridistribuito sotto
 * GPL-3.0-or-later, compatibile con entrambi i progetti a monte
 * (ExplorerPatcher, GPL; RetroBar, Apache-2.0). Vedi THIRD-PARTY-NOTICES.md.
 *
 * Differenze rispetto all'originale:
 *   - gestione delle risorse COM via RAII (ComPtr, HStringRef) invece del
 *     rilascio manuale annidato nei blocchi if(SUCCEEDED(hr));
 *   - le interfacce sono dichiarate in C++ (metodi virtuali) invece che
 *     come vtbl C espliciti;
 *   - gli oggetti ottenuti dalla fabbrica restano in CACHE fra una chiamata
 *     e l'altra: ExplorerPatcher li crea e li rilascia a ogni invocazione,
 *     qui ricrearli a ogni clic sul tray renderebbe l'apertura visibilmente
 *     piu' lenta e farebbe lampeggiare il riquadro;
 *   - combase.dll e' caricato a runtime con GetProcAddress per non legare
 *     la DLL a un modulo che su Windows 7 non esiste;
 *   - nessuna eccezione attraversa il confine della DLL: l'ABI pubblica e'
 *     C e restituisce HRESULT / codici interi, quindi gli errori viaggiano
 *     come valori di ritorno e non come throw.
 *
 * La sequenza di chiamate COM e' quella richiesta dalle API di sistema e
 * non e' materiale creativo: CoCreateInstance(CLSID_ImmersiveShell) ->
 * IServiceProvider::QueryService(CLSID_ShellExperienceManagerFactory) ->
 * GetExperienceManager(nome runtime) -> QueryInterface(IID del flyout) ->
 * ShowFlyout / HideFlyout.
 *
 * Ambito: solo i quattro riquadri che Windows 7 espone come icone di
 * sistema nella tray (rete, orologio, batteria, volume). Il centro
 * notifiche di Windows 10/11 NON e' incluso: non ha un corrispettivo nel
 * modello di Windows 7 e non serve a questa barra.
 */

#ifndef W7T_IMMERSIVE_FLYOUTS_H
#define W7T_IMMERSIVE_FLYOUTS_H

#include "Common.h"

/* 1.0.0-alpha: Common.h definisce WIN32_LEAN_AND_MEAN, e con quel simbolo
 * windows.h NON include piu' i header COM. MinGW-w64 li tira comunque
 * dentro, l'SDK Microsoft no: senza questo include la compilazione con MSVC
 * si ferma su "'IUnknown': base class undefined" (le interfacce qui sotto
 * derivano da IUnknown) e su STDMETHODCALLTYPE. <objbase.h> e' il header COM
 * canonico ed esiste su entrambe le toolchain. */
#include <objbase.h>

/* HSTRING e' l'handle di stringa del Windows Runtime. MinGW-w64 e l'SDK di
 * Microsoft lo forniscono entrambi in <winstring.h>/<hstring.h>; se una
 * toolchain non li avesse, essendo handle opachi basta dichiararli. */
#if defined(__has_include)
#  if __has_include(<winstring.h>)
#    include <winstring.h>
#    define W7T_HAVE_WINSTRING 1
#  endif
#endif

#ifndef W7T_HAVE_WINSTRING
extern "C" {
#ifndef __HSTRING__
#define __HSTRING__
typedef struct HSTRING__* HSTRING;
#endif
#ifndef __HSTRING_HEADER__
#define __HSTRING_HEADER__
typedef struct HSTRING_HEADER {
    union {
        void* Reserved1;
        char  Reserved2[24]; /* x64; su x86 sono 20 byte */
    } Reserved;
} HSTRING_HEADER;
#endif
} /* extern "C" */
#endif

namespace w7t {

/* ------------------------------------------------------------------------- */
/*  Tipi pubblici del modulo                                                 */
/* ------------------------------------------------------------------------- */

/* Windows.Foundation.Rect del Windows Runtime: quattro float passati per
 * riferimento. Non e' una RECT Win32: confondere le due e' il modo piu'
 * rapido per ottenere un riquadro aperto nell'angolo in alto a sinistra. */
struct WinRtRect {
    float X;
    float Y;
    float Width;
    float Height;
};

/* Costruisce un WinRtRect a partire da una RECT Win32 (pixel fisici). */
inline WinRtRect MakeWinRtRect(const RECT& r) {
    WinRtRect out;
    out.X      = static_cast<float>(r.left);
    out.Y      = static_cast<float>(r.top);
    out.Width  = static_cast<float>(r.right - r.left);
    out.Height = static_cast<float>(r.bottom - r.top);
    return out;
}

/* Quale riquadro di sistema aprire. I valori numerici coincidono con le
 * costanti W7T_FLYOUT_* dell'ABI pubblica e con INVOKE_FLYOUT_* di
 * ExplorerPatcher, cosi' il managed layer passa l'intero senza traduzioni. */
enum class FlyoutKind : int32_t {
    Network = 1,
    Clock   = 2,
    Battery = 3,
    Sound   = 4,
    ActionCenter = 5
};

/* Mostra o nasconde. Stessi valori di INVOKE_FLYOUT_SHOW / _HIDE. */
enum class FlyoutAction : int32_t {
    Show = 1,
    Hide = 2
};

/* ------------------------------------------------------------------------- */
/*  Interfacce COM                                                           */
/* ------------------------------------------------------------------------- */

/* NOTA IMPORTANTE - non spostare queste dichiarazioni dentro un namespace
 * anonimo.
 *
 * Sono interfacce COM: le implementazioni vivono dentro Windows, non in
 * questo file. Se vengono dichiarate in un namespace anonimo, il compilatore
 * vede metodi virtuali puri di un tipo che nessuna classe visibile deriva, ne
 * deduce che una simile chiamata non puo' mai avvenire e sostituisce ogni
 * invocazione con __cxa_pure_virtual (il gestore di errore che termina il
 * processo), eliminando insieme a essa anche gli IID diventati "inutilizzati".
 * Il codice compila senza un solo avviso e il percorso nativo smette
 * silenziosamente di funzionare. Verificato con GCC 14 a -O2.
 *
 * native/tools/check-com-interfaces.py ricontrolla questa proprieta' sul
 * binario prodotto, a ogni build.
 */

/* IExperienceManager (IInspectable + ShowFlyout/HideFlyout).
 * E' l'interfaccia comune a NetworkFlyout, TrayClockFlyout,
 * TrayBatteryFlyout e MtcUvc: cambiano solo il nome runtime con cui la si
 * chiede alla fabbrica e l'IID con cui la si interroga.
 *
 * I tre metodi di IInspectable precedono quelli utili e vanno dichiarati per
 * mantenere allineati gli offset della vtable. TrustLevel e' un enum WinRT:
 * qui e' un int, che ha la stessa rappresentazione binaria. */
struct IExperienceManager : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetIids(ULONG* iidCount, IID** iids) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* className) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTrustLevel(int* trustLevel) = 0;

    /* Firma reale, da ExplorerPatcher: ShowFlyout(Rect*, void*) dove il
     * secondo argomento si passa NULL. Dichiararlo con un solo parametro
     * lascerebbe spazzatura nel registro che il callee legge comunque:
     * su x64 gli argomenti viaggiano in registri, dichiarati o no. */
    virtual HRESULT STDMETHODCALLTYPE ShowFlyout(WinRtRect* rect, void* options) = 0;
    virtual HRESULT STDMETHODCALLTYPE HideFlyout() = 0;
};

/* IShellExperienceManagerFactory: da un nome runtime ("Windows.Internal.
 * ShellExperience.XYZ") restituisce l'oggetto che gestisce quel riquadro.
 *
 * Nell'header di ExplorerPatcher il primo parametro e' dichiarato HSTRING*
 * ma viene passata una HSTRING: la forma corretta, e quella usata qui, e'
 * il passaggio per valore dell'handle. */
struct IShellExperienceManagerFactory : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetIids(ULONG* iidCount, IID** iids) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* className) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTrustLevel(int* trustLevel) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetExperienceManager(HSTRING experience,
                                                           IUnknown** manager) = 0;
};

/* ------------------------------------------------------------------------- */
/*  Helper RAII                                                              */
/* ------------------------------------------------------------------------- */

namespace detail {

/* Wrapper minimale per puntatori COM: equivalente di Microsoft::WRL::ComPtr
 * senza tirare dentro WRL, che il resto del core non usa. Non copiabile,
 * spostabile, rilascia nell'ordine inverso a quello di costruzione. */
template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    explicit ComPtr(T* p) : m_ptr(p) {}
    ~ComPtr() { Reset(); }

    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    ComPtr(ComPtr&& other) noexcept : m_ptr(other.m_ptr) { other.m_ptr = nullptr; }
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            Reset();
            m_ptr = other.m_ptr;
            other.m_ptr = nullptr;
        }
        return *this;
    }

    /* Punto di uscita per le API che scrivono il puntatore: azzera prima,
     * cosi' non si puo' perdere un riferimento per strada. */
    T** Put() { Reset(); return &m_ptr; }
    T** PutAndRead() { return &m_ptr; }

    T* Get() const { return m_ptr; }
    T* operator->() const { return m_ptr; }
    explicit operator bool() const { return m_ptr != nullptr; }

    /* Cede la proprieta' al chiamante (che dovra' fare Release). */
    T* Detach() { T* p = m_ptr; m_ptr = nullptr; return p; }

    void Reset() {
        if (m_ptr != nullptr) {
            m_ptr->Release();
            m_ptr = nullptr;
        }
    }

private:
    T* m_ptr = nullptr;
};

/* HSTRING creata con WindowsCreateStringReference: non possiede la memoria
 * della stringa sorgente (resta un riferimento al buffer wide del chiamante,
 * che deve vivere almeno quanto l'oggetto), ma va comunque chiusa con
 * WindowsDeleteString. E' la forma usata da ExplorerPatcher e costa una
 * copia in meno rispetto a WindowsCreateString. */
class HStringRef {
public:
    HStringRef() = default;

    bool Create(const wchar_t* text);
    void Reset();

    HSTRING Get() const { return m_value; }
    explicit operator bool() const { return m_value != nullptr; }

    ~HStringRef() { Reset(); }

    HStringRef(const HStringRef&) = delete;
    HStringRef& operator=(const HStringRef&) = delete;

private:
    HSTRING_HEADER m_header = {};
    HSTRING        m_value  = nullptr;
};

} /* namespace detail */

/* ------------------------------------------------------------------------- */
/*  API del modulo                                                           */
/* ------------------------------------------------------------------------- */

class ImmersiveFlyouts {
public:
    /* Mostra o nasconde un flyout immersivo.
     *
     * @param kind    quale riquadro (rete, orologio, batteria, volume).
     * @param action  Show oppure Hide.
     * @param anchor  rettangolo della NOSTRA barra, in pixel fisici: la
     *                shell lo usa per collocare il riquadro.
     * @return S_OK, oppure l'HRESULT della prima chiamata fallita.
     */
    static HRESULT Invoke(FlyoutKind kind, FlyoutAction action, const WinRtRect& anchor);

    /* true se questo Windows ha l'infrastruttura dei flyout immersivi
     * (Windows 10 e successivi). Su Windows 7/8 restituisce false e il
     * chiamante deve usare le vie classiche. */
    static bool IsSupported();


    /* Rilascia le istanze COM tenute in cache. */
    static void Shutdown();

private:
    ImmersiveFlyouts() = delete;
};

} /* namespace w7t */

#endif /* W7T_IMMERSIVE_FLYOUTS_H */
