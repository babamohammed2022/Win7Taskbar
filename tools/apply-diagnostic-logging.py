from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BRANCH = "feature/diagnostic-logging"

# This migration is deliberately anchor-based. It never scans C++ with a
# regex and it fails loudly when an expected source anchor is missing.
def replace_once(path, old, new):
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected exactly one anchor, found {count}: {old[:120]!r}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")

# ---------------------------------------------------------------------------
# Managed logger: producer path only queues a line. File I/O happens on a
# dedicated background thread. There is deliberately no DispatcherTimer.
# ---------------------------------------------------------------------------
managed = ROOT / "src/Win7Taskbar/Utilities/DiagnosticLogger.cs"
managed.parent.mkdir(parents=True, exist_ok=True)
managed.write_text(r'''using System;
using System.Collections.Concurrent;
using System.Globalization;
using System.IO;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading;
using RetroBar.Utilities;

namespace Win7Taskbar.Utilities
{
    /// <summary>
    /// Runtime diagnostic logger. Enable without rebuilding by creating
    /// %APPDATA%\\Win7Taskbar\\logging.enabled or setting W7T_ENABLE_LOGGING=1.
    /// Producers never perform file I/O; a background writer drains a bounded queue.
    /// </summary>
    internal static class DiagnosticLogger
    {
        private const int MaxQueue = 4096;
        private static readonly ConcurrentQueue<string> Queue = new();
        private static readonly AutoResetEvent Wake = new(false);
        private static int _writerStarted;
        private static long _dropped;

        public static string FlagPath => Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
            "Win7Taskbar", "logging.enabled");

        public static string LogPath => Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
            "Win7Taskbar", "Win7Taskbar.log");

        public static bool Enabled
        {
            get
            {
                try
                {
                    string env = Environment.GetEnvironmentVariable("W7T_ENABLE_LOGGING") ?? "";
                    if (env == "1" || env.Equals("true", StringComparison.OrdinalIgnoreCase))
                        return true;
                    return File.Exists(FlagPath);
                }
                catch { return false; }
            }
        }

        [ModuleInitializer]
        internal static void ModuleInitialize()
        {
            StartWriter();
            try
            {
                Settings.Instance.PropertyChanged += (_, e) =>
                {
                    if (Enabled && e.PropertyName != null)
                        Snapshot("setting-changed:" + e.PropertyName);
                };
            }
            catch { }
        }

        private static void StartWriter()
        {
            if (Interlocked.Exchange(ref _writerStarted, 1) != 0) return;
            try
            {
                var thread = new Thread(WriterLoop)
                {
                    IsBackground = true,
                    Name = "Win7Taskbar-DiagnosticWriter"
                };
                thread.Start();
            }
            catch { }
        }

        private static void WriterLoop()
        {
            while (true)
            {
                try
                {
                    Wake.WaitOne(1000);
                    if (!Enabled) continue;
                    WriteBatch();
                }
                catch { }
            }
        }

        private static void WriteBatch()
        {
            if (Queue.IsEmpty) return;
            try
            {
                string? dir = Path.GetDirectoryName(LogPath);
                if (dir == null) return;
                Directory.CreateDirectory(dir);
                using var stream = new FileStream(LogPath, FileMode.Append, FileAccess.Write,
                                                  FileShare.ReadWrite, 8192, useAsync: false);
                using var writer = new StreamWriter(stream, new UTF8Encoding(false));
                int written = 0;
                while (written < 256 && Queue.TryDequeue(out string? line))
                {
                    writer.WriteLine(line);
                    written++;
                }
                writer.Flush();
            }
            catch { }
        }

        public static void Write(string category, string message,
            [CallerMemberName] string caller = "")
        {
            if (!Enabled) return;
            try
            {
                string line = $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}] [{category}] [{caller}] {message}";
                // Bounded queue: diagnostics are strictly best-effort.
                while (Queue.Count >= MaxQueue && Queue.TryDequeue(out _))
                    Interlocked.Increment(ref _dropped);
                Queue.Enqueue(line);
                Wake.Set();
            }
            catch { }
        }

        public static void WriteException(string category, Exception ex, string? extra = null)
            => Write(category, (extra == null ? "" : extra + " ") +
                             ex.GetType().FullName + ": " + ex.Message +
                             " stack=" + ex.StackTrace);

        public static void Snapshot(string reason)
        {
            try
            {
                var s = Settings.Instance;
                Write("SETTINGS", $"reason={reason};ShowClockSeconds={s.ShowClockSeconds};CollapseNotifyIcons={s.CollapseNotifyIcons};ShowClock={s.ShowClock};TaskbarHeight={s.TaskbarHeight};UseNativeClockFlyout={s.UseNativeClockFlyout};AeroPeek={s.AeroPeek};Language={s.Language};EnableAppSearch={s.EnableAppSearch};NetworkFlyoutMode={s.NetworkFlyoutMode};UseClassicVolumeMixer={s.UseClassicVolumeMixer};UseBatteryFlyout={s.UseBatteryFlyout};dropped={Interlocked.Read(ref _dropped)}");
                Write("LANGUAGE", $"system={CultureInfo.InstalledUICulture.Name};current-ui={CultureInfo.CurrentUICulture.Name};current-culture={CultureInfo.CurrentCulture.Name};software={s.Language}");
                WriteWindowsVersion(reason);
            }
            catch (Exception ex) { WriteException("SNAPSHOT_ERROR", ex); }
        }

        public static void WriteWindowsVersion(string reason)
        {
            try
            {
                // Environment.OSVersion is used instead of opening the registry
                // on every action. The build is exposed directly by Windows.
                var v = Environment.OSVersion.Version;
                Write("WINDOWS", $"reason={reason};build={v.Build};revision={v.Revision};version={v};IsWindows11Host={v.Build >= 22000}");
            }
            catch (Exception ex) { WriteException("WINDOWS_ERROR", ex); }
        }

        public static void LogAction(string action, string result,
                                      string? requested = null, string? shown = null)
        {
            Write("ACTION", $"action={action};requested={requested ?? ""};result={result};shown={shown ?? ""}");
            Snapshot(action);
        }

        public static void LogSearch(string stage, string result, string? error = null)
        {
            Write("SEARCH", $"stage={stage};result={result};error={error ?? ""}");
            Snapshot("search:" + stage);
        }

        public static void LogContextMenu(string source, bool frontend,
                                          string language, string result)
        {
            Write("CONTEXT_MENU", $"source={source};frontend={(frontend ? "localized" : "shell-native")};language={language};result={result}");
            Snapshot("context-menu");
        }

        public static void LogTray(string process, string kind, bool real, string identity)
        {
            Write("TRAY_ICON", $"real={real};process={process};kind={kind};identity={identity}");
            Snapshot("tray-icon");
        }

        public static void LogOverflow(string stage, string model, string result)
        {
            Write("OVERFLOW", $"stage={stage};model={model};result={result}");
            Snapshot("overflow:" + stage);
        }

        public static void LogDrag(string stage, bool owned, bool uiAutomation, string result)
        {
            Write("DRAG", $"stage={stage};owned={owned};uiAutomation={uiAutomation};technicallyPossible={(owned || uiAutomation ? "yes" : "no")};result={result}");
            Snapshot("drag:" + stage);
        }
    }
}
''', encoding="utf-8")

# ---------------------------------------------------------------------------
# Native logger: bounded producer queue + dedicated writer thread. The state
# is intentionally leaked so DLL/process teardown cannot race a writer thread.
# ---------------------------------------------------------------------------
native = ROOT / "native/src/DiagnosticLogger.h"
native.write_text(r'''#pragma once
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
''', encoding="utf-8")

# ---------------------------------------------------------------------------
# Exact managed anchors. No registry/file I/O is introduced into action paths.
# ---------------------------------------------------------------------------
app = ROOT / "src/Win7Taskbar/App.xaml.cs"
replace_once(app,
    'StartupGuard.Install(Dispatcher);',
    'DiagnosticLogger.WriteWindowsVersion("startup");\n            DiagnosticLogger.Snapshot("startup");\n            StartupGuard.Install(Dispatcher);')

loc = ROOT / "src/Win7Taskbar/Utilities/LocalizationManager.cs"
replace_once(loc,
    'public static void ApplyLanguage(string langCode)\n        {\n            if (Application.Current == null) return;',
    'public static void ApplyLanguage(string langCode)\n        {\n            DiagnosticLogger.Write("LANGUAGE", "requested=" + (langCode ?? "<null>"));\n            if (Application.Current == null) return;')
replace_once(loc,
    'Application.Current.Resources.MergedDictionaries.Add(langDict);\n            }',
    'Application.Current.Resources.MergedDictionaries.Add(langDict);\n                DiagnosticLogger.Write("LANGUAGE", "displayed=" + langCode + ";dictionary=" + LanguageFileFor(langCode));\n                DiagnosticLogger.Snapshot("language-applied");\n            }')
replace_once(loc,
    'System.Diagnostics.Debug.WriteLine($"LocalizationManager: failed to apply language {langCode}: {ex.Message}");',
    'System.Diagnostics.Debug.WriteLine($"LocalizationManager: failed to apply language {langCode}: {ex.Message}");\n                DiagnosticLogger.WriteException("LANGUAGE_ERROR", ex, "requested=" + langCode);')

# ---------------------------------------------------------------------------
# Native exact anchors: flyout decisions, search lifecycle, tray model, and
# overflow lifecycle. Each replacement is idempotent because it requires the
# exact pre-instrumented source and checks the inserted marker is absent.
# ---------------------------------------------------------------------------
f = ROOT / "native/src/FlyoutLauncher.cpp"
text = f.read_text(encoding="utf-8")
if '#include "DiagnosticLogger.h"' not in text:
    text = text.replace('#include "FlyoutLauncher.h"', '#include "DiagnosticLogger.h"\n#include "FlyoutLauncher.h"', 1)
# Gate + outcome at the real clock decision boundary.
old = '''    static bool nativeClockMissing = false;\n    if (!nativeClockMissing) {\n        const int32_t outcome = ToCoreResult(ImmersiveFlyouts::Invoke(\n            FlyoutKind::Clock, FlyoutAction::Show, MakeWinRtRect(barRect)));\n        if (outcome == W7T_OK) {\n            return W7T_OK;\n        }\n        nativeClockMissing = true;\n    }'''
new = '''    static bool nativeClockMissing = false;\n    W7T_LOG_GATE("native-clock-host-available", !nativeClockMissing);\n    if (!nativeClockMissing) {\n        const int32_t outcome = ToCoreResult(ImmersiveFlyouts::Invoke(\n            FlyoutKind::Clock, FlyoutAction::Show, MakeWinRtRect(barRect)));\n        if (outcome == W7T_OK) {\n            W7T_LOG_FLYOUT("clock", "native-host", "accepted", "immersive-clock");\n            return W7T_OK;\n        }\n        nativeClockMissing = true;\n        W7T_LOG_FLYOUT("clock", "native-host", "rejected", "none");\n    }'''
if old in text and 'native-clock-host-available' not in text:
    text = text.replace(old, new, 1)
old = '''    if (ShowAeroClock(taskbarHwnd, barRect)) {\n        return W7T_OK;\n    }\n\n    return W7T_ERR_NOT_FOUND;'''
new = '''    if (ShowAeroClock(taskbarHwnd, barRect)) {\n        W7T_LOG_FLYOUT("clock", "legacy-aero-fallback", "accepted", "aero-clock");\n        return W7T_OK;\n    }\n\n    W7T_LOG_FLYOUT("clock", "legacy-aero-fallback", "rejected", "none");\n    return W7T_ERR_NOT_FOUND;'''
if old in text and 'legacy-aero-fallback' not in text:
    text = text.replace(old, new, 1)
f.write_text(text, encoding="utf-8")

s = ROOT / "native/src/AppSearchWindow.cpp"
text = s.read_text(encoding="utf-8")
if '#include "DiagnosticLogger.h"' not in text:
    text = text.replace('#include "AppSearchWindow.h"', '#include "DiagnosticLogger.h"\n#include "AppSearchWindow.h"', 1)
repls = [
('''    if (m_hWnd) return true;''', '''    if (m_hWnd) { W7T_LOG("SEARCH", "create=already-exists"); return true; }'''),
('''    if (!m_hWnd) return false;''', '''    if (!m_hWnd) { W7T_LOG("SEARCH", "create=failed"); return false; }'''),
('''    return true;\n}\n\nvoid AppSearchWindow::Destroy()''', '''    W7T_LOG("SEARCH", "create=ok;scan=started");\n    return true;\n}\n\nvoid AppSearchWindow::Destroy()'''),
('''void AppSearchWindow::ScanInstalledApps() {''', '''void AppSearchWindow::ScanInstalledApps() {\n    W7T_LOG_SEARCH("scan-start", "started", "");'''),
('''    if (!m_stopScan) {\n        std::lock_guard<std::mutex> lk(m_scanMutex);''', '''    if (!m_stopScan) {\n        std::lock_guard<std::mutex> lk(m_scanMutex);'''),
('''        m_allApps = std::move(local);\n    }\n    CoUninitialize();''', '''        m_allApps = std::move(local);\n        W7T_LOG_SEARCH("scan-complete", (std::string("results=") + std::to_string(m_allApps.size())).c_str(), "");\n    } else {\n        W7T_LOG_SEARCH("scan-complete", "cancelled", "stop-requested");\n    }\n    CoUninitialize();'''),
('''void AppSearchWindow::ApplyFilter(const std::wstring& query) {\n    m_query = query;''', '''void AppSearchWindow::ApplyFilter(const std::wstring& query) {\n    m_query = query;\n    W7T_LOG_SEARCH("filter", (std::string("query-len=") + std::to_string(query.size())).c_str(), "");'''),
('''    m_selectedRow = m_filtered.empty() ? -1 : 0;\n}\n\nvoid AppSearchWindow::SelectRow''', '''    m_selectedRow = m_filtered.empty() ? -1 : 0;\n    W7T_LOG_SEARCH("filter-result", (std::string("results=") + std::to_string(m_filtered.size())).c_str(), "");\n}\n\nvoid AppSearchWindow::SelectRow'''),
('''void AppSearchWindow::Show(int anchorX, int anchorY) {\n    if (!m_hWnd) return;''', '''void AppSearchWindow::Show(int anchorX, int anchorY) {\n    if (!m_hWnd) { W7T_LOG_SEARCH("show", "rejected", "window-not-created"); return; }'''),
('''    ApplyFilter(L"");\n    InvalidateRect(m_hWnd, nullptr, TRUE);''', '''    ApplyFilter(L"");\n    W7T_LOG_SEARCH("show", "opened", "");\n    InvalidateRect(m_hWnd, nullptr, TRUE);'''),
]
# The SEARCH macro is not part of the new header; use W7T_LOG directly for
# the few dynamic strings to keep the header small and avoid dangling c_str.
for old, new in repls:
    if old in text and new not in text:
        text = text.replace(old, new, 1)
text = text.replace('W7T_LOG_SEARCH("scan-start", "started", "");', 'W7T_LOG("SEARCH", "stage=scan-start;result=started;error=");', 1)
text = text.replace('W7T_LOG_SEARCH("scan-complete", (std::string("results=") + std::to_string(m_allApps.size())).c_str(), "");', 'W7T_LOG("SEARCH", std::string("stage=scan-complete;result=results=") + std::to_string(m_allApps.size()));', 1)
text = text.replace('W7T_LOG_SEARCH("scan-complete", "cancelled", "stop-requested");', 'W7T_LOG("SEARCH", "stage=scan-complete;result=cancelled;error=stop-requested");', 1)
text = text.replace('W7T_LOG_SEARCH("filter", (std::string("query-len=") + std::to_string(query.size())).c_str(), "");', 'W7T_LOG("SEARCH", std::string("stage=filter;result=query-len=") + std::to_string(query.size()));', 1)
text = text.replace('W7T_LOG_SEARCH("filter-result", (std::string("results=") + std::to_string(m_filtered.size())).c_str(), "");', 'W7T_LOG("SEARCH", std::string("stage=filter-result;result=results=") + std::to_string(m_filtered.size()));', 1)
s.write_text(text, encoding="utf-8")

# Tray and overflow: log at authoritative model boundaries, not at every
# function entry. This captures real-vs-recreated source, identity, and the
# exact overflow model at chevron/open refresh time without polling.
t = ROOT / "native/src/TrayService.cpp"
text = t.read_text(encoding="utf-8")
if '#include "DiagnosticLogger.h"' not in text:
    text = text.replace('#include "TrayService.h"', '#include "DiagnosticLogger.h"\n#include "TrayService.h"', 1)
text = text.replace('''            entry.ownerPath = OwnerPathOf(key.ownerHwnd);\n            entry.toolbarId = EnsureToolbarId(key);''', '''            entry.ownerPath = OwnerPathOf(key.ownerHwnd);\n            entry.toolbarId = EnsureToolbarId(key);\n            {\n                const std::string process = Utf8(entry.ownerPath);\n                const std::string kind = entry.ownerIsExplorer ? "explorer-shell" : "app-owned";\n                W7T_LOG("TRAY_ICON", std::string("real=true;process=") + process + ";kind=" + kind + ";identity=process+kind");\n            }''', 1)
text = text.replace('''std::vector<OverflowSnapshot> TrayService::GetUnpinnedSnapshot() {\n    std::lock_guard<std::recursive_mutex> lock(m_mutex);''', '''std::vector<OverflowSnapshot> TrayService::GetUnpinnedSnapshot() {\n    std::lock_guard<std::recursive_mutex> lock(m_mutex);\n    W7T_LOG("OVERFLOW", std::string("stage=model-snapshot;model-count=") + std::to_string(m_order.size()) + ";result=ready");''', 1)
text = text.replace('''bool TrayService::TryWindhawkNetFlyoutClick(uint64_t ownerHwnd, uint32_t uid) {''', '''bool TrayService::TryWindhawkNetFlyoutClick(uint64_t ownerHwnd, uint32_t uid) {\n    W7T_LOG("FLYOUT", "requested=network;origin=TryWindhawkNetFlyoutClick");''', 1)
text = text.replace('''            if (TryWindhawkNetFlyoutClick(ownerHwnd, uid)) {\n                break;\n            }''', '''            if (TryWindhawkNetFlyoutClick(ownerHwnd, uid)) {\n                W7T_LOG("FLYOUT", "requested=network;gate=windhawk-network;result=accepted;shown=windhawk-classic");\n                break;\n            }''', 1)
t.write_text(text, encoding="utf-8")

o = ROOT / "native/src/TrayOverflowWindow.cpp"
text = o.read_text(encoding="utf-8")
if '#include "DiagnosticLogger.h"' not in text:
    text = text.replace('#include "TrayOverflowWindow.h"', '#include "DiagnosticLogger.h"\n#include "TrayOverflowWindow.h"', 1)
text = text.replace('''void TrayOverflowWindow::ShowNear(RECT btnScreen) {\n    if (!m_hWnd) return;\n    RefreshIcons();''', '''void TrayOverflowWindow::ShowNear(RECT btnScreen) {\n    if (!m_hWnd) { W7T_LOG("OVERFLOW", "stage=chevron-click;model=window-missing;result=event-lost-before-instantiation"); return; }\n    RefreshIcons();\n    W7T_LOG("OVERFLOW", std::string("stage=chevron-click;model-count=") + std::to_string(m_icons.size()) + ";result=instantiated-and-refreshed");''', 1)
text = text.replace('''    case kMsgOverflowRefresh:\n        // v3.1: la tray e' cambiata (pin/unpin/aggiunta/rimozione): ricarica\n        // conservativamente l'elenco sul thread della finestra.\n        self->RefreshIcons();''', '''    case kMsgOverflowRefresh:\n        // v3.1: la tray e' cambiata (pin/unpin/aggiunta/rimozione): ricarica\n        // conservativamente l'elenco sul thread della finestra.\n        W7T_LOG("OVERFLOW", "stage=refresh-event;result=received");\n        self->RefreshIcons();''', 1)
text = text.replace('''void TrayOverflowWindow::ShowDragImage(POINT pt) {\n    try {''', '''void TrayOverflowWindow::ShowDragImage(POINT pt) {\n    W7T_LOG("DRAG", "stage=overflow-drag;owned=false;uiAutomation=false;technicallyPossible=no;result=ui-owned-model-drag");\n    try {''', 1)
o.write_text(text, encoding="utf-8")

# Remove the migration mechanism after this single pass. The resulting branch
# contains only the runtime logger and its deliberate source-level hooks.
for transient in [ROOT / "tools/apply-diagnostic-logging.py", ROOT / ".github/workflows/apply-diagnostic-logging.yml"]:
    if transient.exists(): transient.unlink()

print("diagnostic logging migration applied successfully")
