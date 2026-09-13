// Win7Taskbar - native fatal crash reporter
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.

#include <windows.h>

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

    CloseHandle(file);
}

LONG WINAPI Win7TaskbarUnhandledExceptionFilter(EXCEPTION_POINTERS* ep) noexcept
{
    WriteCrashReport(ep);
    // Never resume execution after an unhandled native fault. Microsoft documents
    // EXCEPTION_EXECUTE_HANDLER as the terminating path for a top-level filter.
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    UNREFERENCED_PARAMETER(instance);
    UNREFERENCED_PARAMETER(reserved);

    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        SetUnhandledExceptionFilter(Win7TaskbarUnhandledExceptionFilter);
    }
    return TRUE;
}
