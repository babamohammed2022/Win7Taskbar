// Win7Taskbar - W7TInject.dll: hook di supporto per congelare il resize
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// v3.0: il flyout orologio e' una finestra DI SISTEMA (explorer/
// ShellExperienceHost): da fuori non esiste un'API pubblica che ne
// disattivi la ridimensionabilita' lasciando i bordi Aero. Il meccanismo
// DOCUMENTATO e' SetWindowsHookEx (hook su thread specifico): il sistema
// carica questa piccola DLL nel processo bersaglio e la nostra callback
// gira DENTRO quel processo, dove puo' usare SetWindowSubclass (altra API
// pubblica) per rispondere a WM_NCHITTEST con HTBORDER sui lati: i bordi
// Aero restano (WS_THICKFRAME) ma il resize non parte.
//
// v3.1: entrambi i callback girano DENTRO processi di sistema: sono
// protetti con __try/__except (SEH) perche' un fault qui dentro
// buttarebbe giu' explorer, non la nostra barra.
//
// La DLL deve restare minuscola e senza stato globale pesante: viene
// iniettata in processi di sistema.

#include <windows.h>
#include <commctrl.h>
#include "SehGuard.h"

namespace {

LRESULT CALLBACK FreezeSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam,
                                    LPARAM lParam, UINT_PTR uIdSubclass,
                                    DWORD_PTR dwRefData) {
    (void)uIdSubclass;
    (void)dwRefData;

    W7T_SEH_TRY
        // Messaggio registrato: il core ce lo manda per rimuoverci PRIMA di
        // togliere l'hook (e quindi prima che questa DLL venga scaricata dal
        // processo bersaglio): mai lasciare un subclass puntato a codice non
        // piu' caricato.
        const UINT uUnfreeze = RegisterWindowMessageW(L"W7T_UnfreezeSize");
        if (uUnfreeze != 0 && uMsg == uUnfreeze) {
            RemoveWindowSubclass(hWnd, FreezeSubclassProc, 0);
            RemovePropW(hWnd, L"W7T_FreezeSize");
            RemovePropW(hWnd, L"W7T_Subclassed");
            return 0;
        }

        if (uMsg == WM_NCHITTEST) {
            LRESULT r = DefSubclassProc(hWnd, uMsg, wParam, lParam);
            switch (r) {
                case HTTOP: case HTTOPLEFT: case HTTOPRIGHT:
                case HTBOTTOM: case HTBOTTOMLEFT: case HTBOTTOMRIGHT:
                case HTLEFT: case HTRIGHT:
                    return HTBORDER;   // bordo si', grip di resize no
                default:
                    return r;
            }
        }

        if (uMsg == WM_NCDESTROY) {
            RemoveWindowSubclass(hWnd, FreezeSubclassProc, 0);
            RemovePropW(hWnd, L"W7T_FreezeSize");
            RemovePropW(hWnd, L"W7T_Subclassed");
        }

        return DefSubclassProc(hWnd, uMsg, wParam, lParam);
    W7T_SEH_CATCH
        // Fault dentro il processo ospite: lasciamo passare il messaggio
        // al comportamento di default, senza toccare altro.
        return DefSubclassProc(hWnd, uMsg, wParam, lParam);
    W7T_SEH_END
}

} /* namespace */

extern "C" __declspec(dllexport)
LRESULT CALLBACK W7TInject_CallWndProc(int nCode, WPARAM wParam, LPARAM lParam) {
    W7T_SEH_TRY
        if (nCode >= 0) {
            const CWPSTRUCT* p = reinterpret_cast<const CWPSTRUCT*>(lParam);
            if (p != nullptr && p->hwnd != nullptr && IsWindow(p->hwnd) &&
                GetPropW(p->hwnd, L"W7T_FreezeSize") != nullptr &&
                GetPropW(p->hwnd, L"W7T_Subclassed") == nullptr) {
                SetPropW(p->hwnd, L"W7T_Subclassed", reinterpret_cast<HANDLE>(1));
                SetWindowSubclass(p->hwnd, FreezeSubclassProc, 0, 0);
            }
        }
    W7T_SEH_CATCH
        /* mai crashare il processo ospite */
    W7T_SEH_END
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) {
    return TRUE;
}
