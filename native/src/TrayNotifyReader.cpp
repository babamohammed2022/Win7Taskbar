/*
 * Win7Taskbar - lettura opzionale dello stato TrayNotify di Explorer
 * Copyright (c) 2026 Win7Taskbar contributors
 *
 * ITrayNotify/ITrayNotifyWin8 non sono API pubbliche Microsoft. Questa e'
 * una tecnica di compatibilita' out-of-process: se il contratto non e'
 * disponibile o cambia, il reader fallisce senza bloccare gli altri backend.
 */

#include "TrayNotifyReader.h"
#include "../include/RaiiWrappers.h"

#include <objbase.h>
#include <new>
#include <utility>

namespace w7t {
namespace {

/* CLSID e IID osservati da Explorer e usati anche da ManagedShell/RetroBar.
 * Sono identificatori di un contratto privato, non una dipendenza binaria
 * dal progetto di riferimento. */
const CLSID kClsidTrayNotify =
    { 0x25DEAD04, 0x1EAC, 0x4911,
      { 0x9E, 0x3A, 0xAD, 0x0A, 0x4A, 0xB5, 0x60, 0xFD } };
const IID kIidTrayNotifyLegacy =
    { 0xFB852B2C, 0x6BAD, 0x4605,
      { 0x95, 0x51, 0xF1, 0x5F, 0x87, 0x83, 0x09, 0x35 } };
const IID kIidTrayNotifyWin8 =
    { 0xD133CE13, 0x3537, 0x48BA,
      { 0x93, 0xA7, 0xAF, 0xCD, 0x5D, 0x20, 0x53, 0xB4 } };
const IID kIidNotificationCallback =
    { 0xD782CCBA, 0xAFB0, 0x43F1,
      { 0x94, 0xDB, 0xFD, 0xA3, 0x77, 0x9E, 0xAC, 0xCB } };

/* Il layout e' privato ma il callback viene marshalled da COM: i puntatori
 * sono validi per la durata di Notify. Non conserviamo mai questi indirizzi. */
struct TrayNotifyItemRaw {
    LPCWSTR pszExeName;
    LPCWSTR pszIconText;
    HICON   hIcon;
    HWND    hWnd;
    DWORD   dwUserPref;
    UINT    uID;
    GUID    guidItem;
};

struct INotificationCallback : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Notify(
        ULONG event, TrayNotifyItemRaw* item) = 0;
};

struct ITrayNotifyLegacy : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE RegisterCallback(
        INotificationCallback* callback) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPreference(
        const TrayNotifyItemRaw* item) = 0;
    virtual HRESULT STDMETHODCALLTYPE EnableAutoTray(BOOL enabled) = 0;
};

struct ITrayNotifyWin8 : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE RegisterCallback(
        INotificationCallback* callback, ULONG* handle) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnregisterCallback(
        ULONG* handle) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPreference(
        const TrayNotifyItemRaw* item) = 0;
    virtual HRESULT STDMETHODCALLTYPE EnableAutoTray(BOOL enabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE DoAction(BOOL enabled) = 0;
};

/* Copia prudente dei testi ricevuti dal proxy COM. Il contratto consegna
 * stringhe terminate, ma un limite evita che un dato corrotto trasformi la
 * lettura in una scansione senza fine. */
std::wstring CopyNotifyText(LPCWSTR text) {
    if (text == nullptr) {
        return std::wstring();
    }
    constexpr size_t kMaxText = 4096;
    size_t length = 0;
    while (length < kMaxText && text[length] != L'\0') {
        ++length;
    }
    if (length == kMaxText) {
        return std::wstring();
    }
    return std::wstring(text, length);
}

class SnapshotCallback final : public INotificationCallback {
public:
    explicit SnapshotCallback(std::vector<TrayNotifySnapshotItem>* output)
        : m_output(output) {}

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&m_refs));
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const LONG left = InterlockedDecrement(&m_refs);
        if (left == 0) {
            delete this;
        }
        return static_cast<ULONG>(left);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,
                                             void** object) override {
        if (object == nullptr) {
            return E_POINTER;
        }
        *object = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) ||
            IsEqualIID(iid, kIidNotificationCallback)) {
            *object = static_cast<INotificationCallback*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE Notify(ULONG event,
                                     TrayNotifyItemRaw* raw) override {
        if (m_output == nullptr || raw == nullptr || raw->hWnd == nullptr) {
            return S_OK;
        }

        /* COM non deve mai vedere un'eccezione C++ del consumer. */
        try {
            TrayNotifySnapshotItem item;
            item.event = static_cast<uint32_t>(event);
            item.ownerHwnd = static_cast<uint64_t>(
                reinterpret_cast<uintptr_t>(raw->hWnd));
            item.uid = raw->uID;
            item.guidItem = raw->guidItem;
            item.preference = static_cast<int32_t>(raw->dwUserPref);
            item.exeName = CopyNotifyText(raw->pszExeName);
            item.tooltip = CopyNotifyText(raw->pszIconText);

            /* L'HICON appartiene alla sessione della shell. CopyIcon crea
             * una risorsa del nostro processo e la guardia la distrugge su
             * ogni ritorno, anche se IconToArgb fallisce. */
            if (raw->hIcon != nullptr) {
                raii::IconHandle icon(CopyIcon(raw->hIcon));
                if (icon) {
                    ArgbBitmap bitmap;
                    if (IconToArgb(icon.get(), bitmap) &&
                        BitmapSane(bitmap)) {
                        item.bitmap = std::move(bitmap);
                    }
                }
            }
            m_output->push_back(std::move(item));
        } catch (...) {
            return E_OUTOFMEMORY;
        }
        return S_OK;
    }

private:
    volatile LONG m_refs = 1;
    std::vector<TrayNotifySnapshotItem>* m_output = nullptr;
};

bool ReadWithWin8Interface(std::vector<TrayNotifySnapshotItem>& out) {
    raii::ComPtr<ITrayNotifyWin8> tray;
    const HRESULT created = CoCreateInstance(
        kClsidTrayNotify, nullptr, CLSCTX_LOCAL_SERVER,
        kIidTrayNotifyWin8, reinterpret_cast<void**>(tray.Put()));
    if (FAILED(created) || !tray) {
        return false;
    }

    SnapshotCallback* callback = new (std::nothrow) SnapshotCallback(&out);
    if (callback == nullptr) {
        return false;
    }

    /* Il secondo parametro è ULONG* (non ULONGLONG*): su x64 il valore
     * resta a 32 bit perché la firma COM privata usa unsigned long. Passare
     * un valore invece dell'indirizzo a UnregisterCallback corromperebbe il
     * contratto e può spiegare un fault dentro Explorer. */
    ULONG handle = 0;
    const HRESULT registered = tray->RegisterCallback(callback, &handle);
    if (FAILED(registered)) {
        callback->Release();
        return false;
    }

    /* Manteniamo il riferimento locale fino alla deregistrazione: il
     * contratto privato dovrebbe fare AddRef, ma il cleanup non deve
     * dipendere da un'implementazione difettosa di Explorer. */
    const HRESULT unregistered = tray->UnregisterCallback(&handle);
    callback->Release();
    if (FAILED(unregistered)) {
        AppendCoreLog(L"tray notify: UnregisterCallback Win8 non riuscito");
    }
    return true;
}

bool ReadWithLegacyInterface(std::vector<TrayNotifySnapshotItem>& out) {
    raii::ComPtr<ITrayNotifyLegacy> tray;
    const HRESULT created = CoCreateInstance(
        kClsidTrayNotify, nullptr, CLSCTX_LOCAL_SERVER,
        kIidTrayNotifyLegacy, reinterpret_cast<void**>(tray.Put()));
    if (FAILED(created) || !tray) {
        return false;
    }

    SnapshotCallback* callback = new (std::nothrow) SnapshotCallback(&out);
    if (callback == nullptr) {
        return false;
    }

    const HRESULT registered = tray->RegisterCallback(callback);
    if (FAILED(registered)) {
        callback->Release();
        return false;
    }

    /* Nell'interfaccia precedente il secondo RegisterCallback(NULL) è il
     * percorso documentato dalle implementazioni compatibili per rimuovere
     * il callback unico. */
    const HRESULT unregistered = tray->RegisterCallback(nullptr);
    callback->Release();
    if (FAILED(unregistered)) {
        AppendCoreLog(L"tray notify: deregistrazione callback legacy non riuscita");
    }
    return true;
}

} /* namespace */

bool TrayNotifyReader::ReadSnapshot(
    std::vector<TrayNotifySnapshotItem>& out) {
    out.clear();

    /* Il reader può essere invocato da un worker senza COM inizializzato.
     * MTA è sufficiente perché RegisterCallback consegna la fotografia in
     * modo sincrono; l'oggetto COM non attraversa il ritorno della funzione. */
    raii::ComInitializer com(COINIT_MULTITHREADED);
    if (!com.succeeded()) {
        AppendCoreLog(L"tray notify: CoInitializeEx non riuscito");
        return false;
    }

    /* Windows 8+ prima, Windows 7 come fallback. Nessuna delle due interfacce
     * è necessaria per il funzionamento: WM_COPYDATA e toolbar/UIA restano
     * indipendenti. */
    bool ok = ReadWithWin8Interface(out);
    if (!ok) {
        out.clear();
        ok = ReadWithLegacyInterface(out);
    }

    if (ok) {
        wchar_t line[160] = {};
        wsprintfW(line, L"tray notify: fotografia callback=%u voci",
                  static_cast<unsigned>(out.size()));
        AppendCoreLog(line);
    } else {
        AppendCoreLog(L"tray notify: COM non disponibile, fallback invariati");
    }
    return ok;
}

} /* namespace w7t */
