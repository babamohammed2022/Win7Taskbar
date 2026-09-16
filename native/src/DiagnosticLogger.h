#pragma once
#include <windows.h>
#include <winternl.h>
#include <string>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <sstream>
#include <iomanip>

namespace w7tlog {
struct State {
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<std::string> queue;
    bool stop = false;
    size_t dropped = 0;
};

inline State& GetState() {
    // Intentionally leaked: the writer may outlive static destruction during
    // DLL detach. The OS reclaims it at process exit.
    static State* state = new State();
    return *state;
}

inline std::wstring AppDataPath(const wchar_t* leaf) {
    wchar_t buf[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    if (!n || n >= MAX_PATH) return std::wstring(leaf);
    std::wstring dir(buf); dir += L"\\Win7Taskbar";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\" + leaf;
}

inline bool Enabled() {
    wchar_t env[16]{};
    DWORD n = GetEnvironmentVariableW(L"W7T_ENABLE_LOGGING", env, 16);
    if (n && (_wcsicmp(env, L"1") == 0 || _wcsicmp(env, L"true") == 0)) return true;
    DWORD attr = GetFileAttributesW(AppDataPath(L"logging.enabled").c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

inline std::string Utf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string r(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), r.data(), n, nullptr, nullptr);
    return r;
}

inline void WriterLoop() {
    State& st = GetState();
    for (;;) {
        std::deque<std::string> batch;
        {
            std::unique_lock<std::mutex> lock(st.mutex);
            st.cv.wait_for(lock, std::chrono::milliseconds(1000), [&] {
                return st.stop || !st.queue.empty();
            });
            if (st.stop && st.queue.empty()) return;
            const size_t count = (std::min)(st.queue.size(), static_cast<size_t>(256));
            for (size_t i = 0; i < count; ++i) {
                batch.emplace_back(std::move(st.queue.front()));
                st.queue.pop_front();
            }
        }
        HANDLE file = CreateFileW(AppDataPath(L"Win7Taskbar.log").c_str(),
                                   FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) continue;
        for (const std::string& line : batch) {
            DWORD bytes = 0;
            WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &bytes, nullptr);
            const char nl = '\n';
            WriteFile(file, &nl, 1, &bytes, nullptr);
        }
        CloseHandle(file);
    }
}

inline void EnsureWriter() {
    static std::once_flag once;
    std::call_once(once, [] {
        std::thread(WriterLoop).detach();
    });
}

inline void Write(const char* category, const std::string& message, const char* function = "") {
    if (!Enabled()) return;
    EnsureWriter();
    SYSTEMTIME st{}; GetLocalTime(&st);
    std::ostringstream line;
    line << '[' << std::setfill('0') << std::setw(4) << st.wYear << '-'
         << std::setw(2) << st.wMonth << '-' << std::setw(2) << st.wDay << ' '
         << std::setw(2) << st.wHour << ':' << std::setw(2) << st.wMinute << ':'
         << std::setw(2) << st.wSecond << '.' << std::setw(3) << st.wMilliseconds
         << "] [" << category << "] [" << function << "] " << message;
    State& stt = GetState();
    {
        std::lock_guard<std::mutex> lock(stt.mutex);
        constexpr size_t maxQueue = 4096;
        while (stt.queue.size() >= maxQueue) {
            stt.queue.pop_front();
            ++stt.dropped;
        }
        stt.queue.push_back(line.str());
    }
    stt.cv.notify_one();
}

inline void WindowsVersion(const char* reason, const char* function) {
    if (!Enabled()) return;
    RTL_OSVERSIONINFOW v{}; v.dwOSVersionInfoSize = sizeof(v);
    auto rtl = reinterpret_cast<LONG(WINAPI*)(PRTL_OSVERSIONINFOW)>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    if (rtl) rtl(&v);
    std::ostringstream s;
    s << "reason=" << reason << ";build=" << v.dwBuildNumber
      << ";IsWindows11Host=" << (v.dwBuildNumber >= 22000 ? "true" : "false");
    Write("WINDOWS", s.str(), function);
}
inline void Gate(const char* gate, bool result, const char* function) {
    std::ostringstream s; s << "gate=" << gate << ";result=" << (result ? "accepted" : "rejected");
    Write("GATE", s.str(), function);
}
inline void Flyout(const char* requested, const char* gate, const char* result,
                   const char* shown, const char* function) {
    std::ostringstream s; s << "requested=" << requested << ";gate=" << gate
                            << ";result=" << result << ";shown=" << shown;
    Write("FLYOUT", s.str(), function);
}
inline void Search(const char* stage, const char* result, const char* error,
                   const char* function) {
    std::ostringstream s; s << "stage=" << stage << ";result=" << result
                            << ";error=" << (error ? error : "");
    Write("SEARCH", s.str(), function);
}
inline void ContextMenu(const char* source, bool frontend, const char* language,
                        const char* result, const char* function) {
    std::ostringstream s; s << "source=" << source << ";frontend="
                            << (frontend ? "localized" : "shell-native")
                            << ";language=" << language << ";result=" << result;
    Write("CONTEXT_MENU", s.str(), function);
}
inline void Tray(const char* process, const char* kind, bool real,
                 const char* identity, const char* function) {
    std::ostringstream s; s << "real=" << (real ? "true" : "false")
                            << ";process=" << process << ";kind=" << kind
                            << ";identity=" << identity;
    Write("TRAY_ICON", s.str(), function);
}
inline void Overflow(const char* stage, const char* model, const char* result,
                     const char* function) {
    std::ostringstream s; s << "stage=" << stage << ";model=" << model
                            << ";result=" << result;
    Write("OVERFLOW", s.str(), function);
}
inline void Drag(const char* stage, bool owned, bool uiAutomation,
                 const char* result, const char* function) {
    std::ostringstream s; s << "stage=" << stage << ";owned=" << (owned ? "true" : "false")
                            << ";uiAutomation=" << (uiAutomation ? "true" : "false")
                            << ";technicallyPossible=" << ((owned || uiAutomation) ? "yes" : "no")
                            << ";result=" << result;
    Write("DRAG", s.str(), function);
}
}
#define W7T_LOG(cat, msg) ::w7tlog::Write(cat, msg, __FUNCTION__)
#define W7T_LOG_WINVER(reason) ::w7tlog::WindowsVersion(reason, __FUNCTION__)
#define W7T_LOG_GATE(gate, result) ::w7tlog::Gate(gate, result, __FUNCTION__)
#define W7T_LOG_FLYOUT(req, gate, result, shown) ::w7tlog::Flyout(req, gate, result, shown, __FUNCTION__)
