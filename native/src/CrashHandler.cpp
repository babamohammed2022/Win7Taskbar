// Win7Taskbar - native fatal crash reporter
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.

#include <windows.h>
#ifdef _MSC_VER
#include <crtdbg.h>
#include <stdlib.h>
#endif

#include "BatteryFlyout.h"
#include "Win8NetworkFlyout.h"   /* v3.8: rilascio GDI del riquadro Win8 */

namespace {

volatile LONG g_crashReporting = 0;

void WriteCrashLine(HANDLE file, const wchar_t* text) noexcept
{
    if (file == INVALID_HANDLE_VALUE || text == nullptr) return;
    const int chars = lstrlenW(text);
    if (chars <= 0) return;
    int bytes = WideCharToMultiByte(CP_UTF8, 0, text, chars, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return;
    // The crash path deliberately uses a fixed stack buffer and Win32 APIs only.
    // Do not allocate C++ objects here: the process may already have a corrupted heap.
    char buffer[1024];
    if (bytes >= static_cast<int>(sizeof(buffer))) bytes = sizeof(buffer) - 1;
    if (WideCharToMultiByte(CP_UTF8, 0, text, chars, buffer, bytes, nullptr, nullptr) <= 0) return;
    DWORD written = 0;
    WriteFile(file, buffer, static_cast<DWORD>(bytes), &written, nullptr);
    const char nl[] = "\r\n";
    WriteFile(file, nl, 2, &written, nullptr);
}

/* v1.21.18: the same event, as one line in log-core.txt - the file a tester
 * is already asked for. Win32 only, fixed buffers, because the heap may
 * already be unusable when this runs. The path rule is the one
 * w7t::AppendCoreLog() uses: log-core.txt beside the running module. */
void AppendCrashToCoreLog(const wchar_t* summary) noexcept
{
    if (summary == nullptr) return;

    wchar_t modulePath[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath)) == 0) return;
    int lastSlash = -1;
    for (int i = 0; modulePath[i] != L'\0'; i++) {
        if (modulePath[i] == L'\\') lastSlash = i;
    }
    if (lastSlash < 0) return;
    modulePath[lastSlash + 1] = L'\0';

    wchar_t logPath[MAX_PATH]{};
    lstrcpynW(logPath, modulePath, ARRAYSIZE(logPath));
    lstrcatW(logPath, L"log-core.txt");

    HANDLE file = CreateFileW(logPath, FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;

    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t line[512]{};
    wsprintfW(line, L"%04u-%02u-%02u %02u:%02u:%02u.%03u [CRASH] %s\r\n",
              now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
              now.wSecond, now.wMilliseconds, summary);
    DWORD written = 0;
    WriteFile(file, line, static_cast<DWORD>(lstrlenW(line)) * sizeof(wchar_t),
              &written, nullptr);
    CloseHandle(file);
}

void WriteCrashReport(EXCEPTION_POINTERS* ep) noexcept
{
    if (InterlockedExchange(&g_crashReporting, 1) != 0) return;

    wchar_t appData[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"APPDATA", appData, ARRAYSIZE(appData));
    if (!n || n >= ARRAYSIZE(appData)) return;

    wchar_t directory[MAX_PATH]{};
    lstrcpynW(directory, appData, ARRAYSIZE(directory));
    lstrcatW(directory, L"\\Win7Taskbar");
    CreateDirectoryW(directory, nullptr);

    wchar_t logs[MAX_PATH]{};
    lstrcpynW(logs, directory, ARRAYSIZE(logs));
    lstrcatW(logs, L"\\logs");
    CreateDirectoryW(logs, nullptr);

    SYSTEMTIME st{};
    GetLocalTime(&st);

    wchar_t path[MAX_PATH]{};
    wsprintfW(path,
              L"%s\\native-crash-%04u%02u%02u-%02u%02u%02u-%03u.txt",
              logs, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
              st.wSecond, st.wMilliseconds);

    HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;

    WriteCrashLine(file, L"Win7Taskbar - native fatal crash report");

    wchar_t line[256]{};
    wsprintfW(line, L"processId=%lu", GetCurrentProcessId());
    WriteCrashLine(file, line);
    wsprintfW(line, L"threadId=%lu", GetCurrentThreadId());
    WriteCrashLine(file, line);

    if (ep != nullptr && ep->ExceptionRecord != nullptr) {
        wsprintfW(line, L"exceptionCode=0x%08lX",
                  ep->ExceptionRecord->ExceptionCode);
        WriteCrashLine(file, line);
        wsprintfW(line, L"exceptionAddress=%p", ep->ExceptionRecord->ExceptionAddress);
        WriteCrashLine(file, line);
    } else {
        WriteCrashLine(file, L"exceptionCode=<terminate>");
        WriteCrashLine(file, L"exceptionAddress=<none>");
    }

    // Keep the report useful even when the fault occurs before the managed
    // logging subsystem is available.
    if (ep != nullptr && ep->ContextRecord != nullptr) {
#if defined(_M_X64) || defined(__x86_64__)
        wsprintfW(line, L"instructionPointer=%p", reinterpret_cast<void*>(ep->ContextRecord->Rip));
#elif defined(_M_IX86) || defined(__i386__)
        wsprintfW(line, L"instructionPointer=%p", reinterpret_cast<void*>(ep->ContextRecord->Eip));
#elif defined(_M_ARM64) || defined(__aarch64__)
        wsprintfW(line, L"instructionPointer=%p", reinterpret_cast<void*>(ep->ContextRecord->Pc));
#endif
        WriteCrashLine(file, line);
    }

    wchar_t processPath[MAX_PATH]{};
    DWORD pathLength = GetModuleFileNameW(nullptr, processPath, ARRAYSIZE(processPath));
    if (pathLength > 0) {
        WriteCrashLine(file, processPath);
    }

    /* v1.21.18: WHICH module faulted and at which offset. Without this a
     * report only carries a bare address, which is useless across runs. */
    if (ep != nullptr && ep->ExceptionRecord != nullptr &&
        ep->ExceptionRecord->ExceptionAddress != nullptr) {
        HMODULE faultModule = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(
                                   ep->ExceptionRecord->ExceptionAddress),
                               &faultModule) &&
            faultModule != nullptr) {
            wchar_t modulePath[MAX_PATH]{};
            if (GetModuleFileNameW(faultModule, modulePath, ARRAYSIZE(modulePath)) > 0) {
                const unsigned long long offset =
                    static_cast<unsigned long long>(
                        reinterpret_cast<ULONG_PTR>(
                            ep->ExceptionRecord->ExceptionAddress) -
                        reinterpret_cast<ULONG_PTR>(faultModule));
                /* A path is longer than the 256-char line buffer used
                 * above, so these two lines get a buffer of their own. */
                wchar_t moduleLine[MAX_PATH + 64]{};
                wsprintfW(moduleLine, L"faultingModule=%s", modulePath);
                WriteCrashLine(file, moduleLine);
                wsprintfW(moduleLine, L"faultingOffset=+0x%I64X", offset);
                WriteCrashLine(file, moduleLine);

                wchar_t summary[400]{};
                wsprintfW(summary, L"unhandled exception 0x%08lX in %s +0x%I64X",
                          ep->ExceptionRecord->ExceptionCode, modulePath, offset);
                AppendCrashToCoreLog(summary);
            }
        }
    }

    /* The log the tester is asked for, so one file already has the story. */
    {
        wchar_t coreLog[MAX_PATH]{};
        lstrcpynW(coreLog, processPath, ARRAYSIZE(coreLog));
        int lastSlash = -1;
        for (int i = 0; coreLog[i] != L'\0'; i++) {
            if (coreLog[i] == L'\\') lastSlash = i;
        }
        if (lastSlash >= 0) {
            coreLog[lastSlash + 1] = L'\0';
            lstrcatW(coreLog, L"log-core.txt");
            wchar_t logLine[MAX_PATH + 16]{};
            wsprintfW(logLine, L"coreLog=%s", coreLog);
            WriteCrashLine(file, logLine);
        }
    }

    CloseHandle(file);
}

#ifdef _MSC_VER
/* v1.7.2: CRT safety nets. The "Exception Processing Message 0xc0000005
 * Unexpected Parameters" box the reporter saw on Windows 10 is NOT an
 * access violation: it is the CRT's abort dialog for an invalid
 * parameter (a bad swprintf/strcpy argument), and it kills the process
 * without ever reaching the SEH guards. A handler that simply RETURNS
 * makes the CRT function fail gracefully instead of aborting; abort's
 * report dialog is disabled alongside it. */
void __cdecl W7tInvalidParameterHandler(const wchar_t* expression,
                                        const wchar_t* function,
                                        const wchar_t* file,
                                        unsigned int line,
                                        uintptr_t /*reserved*/) {
    (void)expression; (void)function; (void)file; (void)line;
    OutputDebugStringW(L"Win7Taskbar: CRT invalid parameter suppressed\n");
    /* returning = the CRT call reports an error, no abort */
}
#endif

LONG WINAPI Win7TaskbarUnhandledExceptionFilter(EXCEPTION_POINTERS* ep) noexcept
{
    WriteCrashReport(ep);
    // Never resume execution after an unhandled native fault. Microsoft documents
    // EXCEPTION_EXECUTE_HANDLER as the terminating path for a top-level filter.
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

// Single DllMain for the whole DLL. Exports.cpp used to define a second one
// (v2.41 battery-flyout teardown) and MSVC link gave LNK2005 "DllMain already
// defined" now that duplicate COMDATs are no longer silently folded; the two
// bodies are merged here.
extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    UNREFERENCED_PARAMETER(reserved);

    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(instance);
            SetUnhandledExceptionFilter(Win7TaskbarUnhandledExceptionFilter);
#ifdef _MSC_VER
            _set_invalid_parameter_handler(W7tInvalidParameterHandler);
            _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
            _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
            break;
        case DLL_PROCESS_DETACH:
            // Moved from Exports.cpp (v2.41): release the battery flyout's
            // GDI+ bitmaps when the host unloads the DLL. v1.7.1: no-ops
            // when the flyout was never shown, so shutdown never constructs
            // the singleton under the loader lock.
            w7t::BatteryFlyout::ShutdownIfCreated();
            /* v3.8: rilascio font GDI del riquadro di rete variante Windows
             * 8 (se non e' mai stato aperto la chiamata e' un no-op). */
            w7t::Win8NetworkFlyout::ShutdownIfCreated();
            break;
        default:
            break;
    }
    return TRUE;
}
