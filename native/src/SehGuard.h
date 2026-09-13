// Win7Taskbar - guardia SEH PORTABILE (MSVC E MinGW-w64)
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// v2: le vecchie macro su MinGW degradavano a blocchi vuoti (__try e'
// solo MSVC). Ora la protezione e' VERA anche con GCC: un gestore
// vettoriale (AddVectoredExceptionHandler) intercetta le eccezioni
// hardware (access violation, divide-by-zero, istruzione illegale...)
// e fa longjmp fuori dal blocco protetto tramite setjmp. Funziona su
// entrambi i toolchain perche' usa solo API Win32 + setjmp/longjmp.
//
// Uso:
//   W7T_SEH_TRY { ...codice a rischio... }
//   W7T_SEH_CATCH { ...fallback silenzioso... }
//   W7T_SEH_END

#pragma once
#include <windows.h>
#include <setjmp.h>

namespace w7t {

struct SehFrame {
    jmp_buf jump;
    SehFrame* previous;
};

inline thread_local SehFrame* g_sehTop = nullptr;
inline thread_local bool g_sehVehInstalled = false;

inline LONG WINAPI SehVectoredHandler(PEXCEPTION_POINTERS ep) {
    switch (ep->ExceptionRecord->ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_DATATYPE_MISALIGNMENT:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_STACK_OVERFLOW:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_PRIV_INSTRUCTION:
        if (g_sehTop != nullptr) {
            // v1.7: NON stacca piu' qui. Lo stacco lo fa il ramo CATCH
            // (o il pop RAII se un `return` anticipato esce dal blocco):
            // due stacchi si annullano a vicenda perche' entrambi
            // scrivono frame->previous.
            longjmp(g_sehTop->jump, 1);   // non ritorna
        }
        return EXCEPTION_CONTINUE_SEARCH;
    default:
        return EXCEPTION_CONTINUE_SEARCH;
    }
}

inline void SehInstallOnce() {
    if (!g_sehVehInstalled) {
        g_sehVehInstalled = true;
        AddVectoredExceptionHandler(1, SehVectoredHandler);
    }
}

} /* namespace w7t */

/* v1.7: pop di sicurezza. Se il blocco esce con un `return` anticipato
 * (saldo permesso da sempre, ma prima lasciava il frame APPESO su
 * g_sehTop: l'eccezione dopo l'uscita saltava in un frame morto), la
 * distruzione locale stacca il frame ancora in cima. Se invece il blocco
 * e' uscito da CATCH/END (gia' staccato) o via longjmp (staccato dal
 * ramo CATCH), qui il top e' diverso e non tocca nulla. */
struct W7tSehAutoPop {
    ::w7t::SehFrame** top;
    ::w7t::SehFrame* frame;
    ~SehAutoPop() {
        if (*top == frame) {
            *top = frame->previous;
        }
    }
};

#define W7T_SEH_TRY                                                     \
    ::w7t::SehInstallOnce();                                            \
    {                                                                   \
        ::w7t::SehFrame w7tSehFrame{};                                  \
        w7tSehFrame.previous = ::w7t::g_sehTop;                         \
        ::w7t::g_sehTop = &w7tSehFrame;                                 \
        W7tSehAutoPop w7tSehAutoPop{ &::w7t::g_sehTop,                 \
                                        &w7tSehFrame };                \
        if (setjmp(w7tSehFrame.jump) == 0) {

#define W7T_SEH_CATCH                                                   \
        ::w7t::g_sehTop = w7tSehFrame.previous;                         \
        } else {                                                        \
        ::w7t::g_sehTop = w7tSehFrame.previous;

#define W7T_SEH_END                                                     \
        }                                                               \
    }

/* Variante con blocco catch eseguito anche in caso di eccezione:
 * W7T_SEH_TRY { ... } W7T_SEH_CATCH { ...fallback... } W7T_SEH_END */
