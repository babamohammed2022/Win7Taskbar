from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]

# ---------- managed logger ----------
managed = ROOT / 'src/Win7Taskbar/Utilities/DiagnosticLogger.cs'
managed.parent.mkdir(parents=True, exist_ok=True)
managed.write_text(r'''using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text;
using System.Threading;
using System.Windows;
using System.Windows.Input;
using System.Windows.Threading;
using RetroBar.Utilities;

namespace Win7Taskbar.Utilities
{
    /// <summary>Runtime diagnostic logger. Enable by creating %APPDATA%\\Win7Taskbar\\logging.enabled.</summary>
    internal static class DiagnosticLogger
    {
        private static readonly object Gate = new();
        private static DateTime _flagLastWrite = DateTime.MinValue;
        private static bool _flagValue;
        private static DispatcherTimer? _watchTimer;
        private static IntPtr _mouseHook;

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
                    if (env == "1" || env.Equals("true", StringComparison.OrdinalIgnoreCase)) return true;
                    var fi = new FileInfo(FlagPath);
                    DateTime stamp = fi.Exists ? fi.LastWriteTimeUtc : DateTime.MinValue;
                    if (stamp != _flagLastWrite) { _flagLastWrite = stamp; _flagValue = fi.Exists; }
                    return _flagValue;
                }
                catch { return false; }
            }
        }

        public static void Initialize()
        {
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(LogPath)!);
                Write("LOGGER", "initialized flag=" + FlagPath);
                if (Application.Current != null)
                {
                    Application.Current.Dispatcher.Hooks.DispatcherUnhandledException += (_, e) =>
                        WriteException("DispatcherUnhandledException", e.Exception);
                    _watchTimer = new DispatcherTimer(DispatcherPriority.Background)
                    { Interval = TimeSpan.FromMilliseconds(500) };
                    _watchTimer.Tick += (_, _) =>
                    {
                        if (Enabled) Snapshot("periodic");
                    };
                    _watchTimer.Start();
                }
            }
            catch { }
        }

        public static void Write(string category, string message, [System.Runtime.CompilerServices.CallerMemberName] string caller = "")
        {
            if (!Enabled) return;
            try
            {
                string line = $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}] [{category}] [{caller}] {message}";
                lock (Gate)
                {
                    Directory.CreateDirectory(Path.GetDirectoryName(LogPath)!);
                    File.AppendAllText(LogPath, line + Environment.NewLine, Encoding.UTF8);
                }
            }
            catch { }
        }

        public static void WriteException(string category, Exception ex, string? extra = null)
            => Write(category, (extra == null ? "" : extra + " ") + ex.GetType().FullName + ": " + ex.Message + "\n" + ex.StackTrace);

        public static void Snapshot(string reason)
        {
            try
            {
                var s = Settings.Instance;
                Write("SETTINGS", $"reason={reason} ShowClockSeconds={s.ShowClockSeconds};CollapseNotifyIcons={s.CollapseNotifyIcons};ShowClock={s.ShowClock};TaskbarHeight={s.TaskbarHeight};UseNativeClockFlyout={s.UseNativeClockFlyout};AeroPeek={s.AeroPeek};Language={s.Language};EnableAppSearch={s.EnableAppSearch};NetworkFlyoutMode={s.NetworkFlyoutMode};UseClassicVolumeMixer={s.UseClassicVolumeMixer};UseBatteryFlyout={s.UseBatteryFlyout}");
                Write("LANGUAGE", $"system={CultureInfo.InstalledUICulture.Name};current-ui={CultureInfo.CurrentUICulture.Name};current-culture={CultureInfo.CurrentCulture.Name};software={s.Language}");
                WriteWindowsVersion("snapshot");
            }
            catch (Exception ex) { WriteException("SNAPSHOT_ERROR", ex); }
        }

        public static void WriteWindowsVersion(string reason)
        {
            try
            {
                using var key = Microsoft.Win32.Registry.LocalMachine.OpenSubKey(@"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion");
                string build = key?.GetValue("CurrentBuildNumber")?.ToString() ?? "unknown";
                string ubr = key?.GetValue("UBR")?.ToString() ?? "unknown";
                Write("WINDOWS", $"reason={reason};build={build}.{ubr};version={Environment.OSVersion.Version};IsWindows11Host={(int.TryParse(build, out int b) && b >= 22000)}");
            }
            catch (Exception ex) { WriteException("WINDOWS_ERROR", ex); }
        }

        public static void LogAction(string action, string result, string? requested = null, string? shown = null)
        {
            Write("ACTION", $"action={action};requested={requested ?? ""};result={result};shown={shown ?? ""}");
            Snapshot(action);
        }

        public static void LogSearch(string stage, string result, string? error = null)
        {
            Write("SEARCH", $"stage={stage};result={result};error={error ?? ""}");
            Snapshot("search:" + stage);
        }

        public static void LogContextMenu(string source, bool frontend, string language, string result)
            => Write("CONTEXT_MENU", $"source={source};frontend={(frontend ? "localized" : "shell-native")};language={language};result={result}");

        public static void LogTray(string process, string kind, bool real, string identity)
            => Write("TRAY_ICON", $"real={real};process={process};kind={kind};identity={identity}");

        public static void LogOverflow(string stage, string model, string result)
            => Write("OVERFLOW", $"stage={stage};model={model};result={result}");

        public static void LogDrag(string stage, bool owned, bool uiAutomation, string result)
            => Write("DRAG", $"stage={stage};owned={owned};uiAutomation={uiAutomation};technicallyPossible={(owned ? "yes" : "no")};result={result}");
    }
}
''', encoding='utf-8')

# Startup hook: logger is deliberately before theme/native initialization.
app = ROOT / 'src/Win7Taskbar/App.xaml.cs'
t = app.read_text(encoding='utf-8')
if 'DiagnosticLogger.Initialize();' not in t:
    t = t.replace('StartupGuard.Install(Dispatcher);', 'DiagnosticLogger.Initialize();\n            DiagnosticLogger.WriteWindowsVersion("startup");\n            DiagnosticLogger.Snapshot("startup");\n\n            StartupGuard.Install(Dispatcher);', 1)
    t = t.replace('StartupGuard.Enter("tema");', 'DiagnosticLogger.Snapshot("before-theme");\n                StartupGuard.Enter("tema");', 1)
    t = t.replace('StartupGuard.Enter("lingua");', 'DiagnosticLogger.Snapshot("before-language");\n                StartupGuard.Enter("lingua");', 1)
    t = t.replace('_taskbar = new TaskbarWindow(_bridge);', 'DiagnosticLogger.Snapshot("before-taskbar");\n                _taskbar = new TaskbarWindow(_bridge);', 1)
    app.write_text(t, encoding='utf-8')

# Localization logging.
loc = ROOT / 'src/Win7Taskbar/Utilities/LocalizationManager.cs'
t = loc.read_text(encoding='utf-8')
t = t.replace('public static void ApplyLanguage(string langCode)\n        {', 'public static void ApplyLanguage(string langCode)\n        {\n            DiagnosticLogger.Write("LANGUAGE", "ApplyLanguage requested=" + (langCode ?? "<null>"));\n            DiagnosticLogger.Snapshot("language-before-apply");', 1)
t = t.replace('Application.Current.Resources.MergedDictionaries.Add(langDict);', 'Application.Current.Resources.MergedDictionaries.Add(langDict);\n                DiagnosticLogger.Write("LANGUAGE", "software-language=" + langCode + ";dictionary=" + LanguageFileFor(langCode));', 1)
t = t.replace('System.Diagnostics.Debug.WriteLine($"LocalizationManager: failed to apply language {langCode}: {ex.Message}");', 'System.Diagnostics.Debug.WriteLine($"LocalizationManager: failed to apply language {langCode}: {ex.Message}");\n                DiagnosticLogger.WriteException("LANGUAGE_ERROR", ex, "requested=" + langCode);', 1)
loc.write_text(t, encoding='utf-8')

# ---------- native header ----------
header = ROOT / 'native/src/DiagnosticLogger.h'
header.write_text(r'''#pragma once
#include <windows.h>
#include <winternl.h>
#include <shlobj.h>
#include <string>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <cstdlib>

namespace w7tlog {
inline std::mutex g_mutex;
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
    if (n && (_wcsicmp(env,L"1")==0 || _wcsicmp(env,L"true")==0)) return true;
    DWORD attr = GetFileAttributesW(AppDataPath(L"logging.enabled").c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}
inline std::string Narrow(const std::wstring& s) {
    if (s.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0,nullptr,nullptr);
    std::string r(n,'\0'); WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),r.data(),n,nullptr,nullptr); return r;
}
inline void Write(const char* category, const std::string& message, const char* function = "") {
    if (!Enabled()) return;
    SYSTEMTIME st{}; GetLocalTime(&st);
    std::lock_guard<std::mutex> lock(g_mutex);
    std::ofstream f(Narrow(AppDataPath(L"Win7Taskbar.log")), std::ios::app);
    if (!f) return;
    f << '[' << std::setfill('0') << std::setw(4) << st.wYear << '-'
      << std::setw(2) << st.wMonth << '-' << std::setw(2) << st.wDay << ' '
      << std::setw(2) << st.wHour << ':' << std::setw(2) << st.wMinute << ':'
      << std::setw(2) << st.wSecond << '.' << std::setw(3) << st.wMilliseconds
      << "] [" << category << "] [" << function << "] " << message << '\n';
}
inline void WindowsVersion(const char* reason, const char* function) {
    if (!Enabled()) return;
    RTL_OSVERSIONINFOW v{}; v.dwOSVersionInfoSize = sizeof(v);
    auto rtl = reinterpret_cast<LONG (WINAPI*)(PRTL_OSVERSIONINFOW)>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    if (rtl) rtl(&v);
    std::ostringstream s; s << "reason=" << reason << ";build=" << v.dwBuildNumber
        << ";IsWindows11Host=" << (v.dwBuildNumber >= 22000 ? "true" : "false");
    Write("WINDOWS", s.str(), function);
}
inline void Gate(const char* gate, bool result, const char* function) {
    std::ostringstream s; s << "gate=" << gate << ";result=" << (result?"accepted":"rejected");
    Write("GATE", s.str(), function);
}
inline void Flyout(const char* requested, const char* gate, const char* result, const char* shown, const char* function) {
    std::ostringstream s; s << "requested=" << requested << ";gate=" << gate << ";result=" << result << ";shown=" << shown;
    Write("FLYOUT", s.str(), function);
}
inline void Search(const char* stage, const char* result, const char* error, const char* function) {
    std::ostringstream s; s << "stage=" << stage << ";result=" << result << ";error=" << (error?error:"");
    Write("SEARCH", s.str(), function);
}
inline void ContextMenu(const char* source, bool frontend, const char* language, const char* function) {
    std::ostringstream s; s << "source=" << source << ";frontend=" << (frontend?"localized":"shell-native") << ";language=" << language;
    Write("CONTEXT_MENU", s.str(), function);
}
inline void Snapshot(const char* reason, const char* function) { Write("SNAPSHOT", std::string("reason=")+reason, function); WindowsVersion(reason,function); }
inline void Error(const char* category, const std::string& error, const char* function) { Write(category, "error="+error, function); }
}
#define W7T_LOG(cat,msg) ::w7tlog::Write(cat,msg,__FUNCTION__)
#define W7T_LOG_WINVER(reason) ::w7tlog::WindowsVersion(reason,__FUNCTION__)
#define W7T_LOG_GATE(gate,result) ::w7tlog::Gate(gate,result,__FUNCTION__)
#define W7T_LOG_FLYOUT(req,gate,result,shown) ::w7tlog::Flyout(req,gate,result,shown,__FUNCTION__)
''', encoding='utf-8')

# Include logger and add low-risk function-entry tracing to relevant native code.
selected = re.compile(r'(Flyout|Search|Tray|Overflow|Drag|ContextMenu|Properties|Windows11|Clock|Network|Volume|Battery|Start)', re.I)
for p in (ROOT / 'native/src').glob('*.cpp'):
    if p.name == 'DiagnosticLogger.cpp': continue
    text = p.read_text(encoding='utf-8', errors='ignore')
    if not selected.search(text):
        continue
    if '#include "DiagnosticLogger.h"' not in text:
        text = '#include "DiagnosticLogger.h"\n' + text
    # Log entry for selected functions, but avoid constructors/lambdas and avoid modifying declarations.
    pat = re.compile(r'^(?P<indent>\s*)(?P<sig>(?:(?:static|inline|extern|virtual|bool|void|int|HRESULT|DWORD|LRESULT|HWND|LONG|std::\w+[<>,\s:*]*|[A-Za-z_][\w:<>]*\s*\*)\s+)+(?P<name>[A-Za-z_]\w*(?:Flyout|Search|Tray|Overflow|Drag|ContextMenu|Properties|Windows11|Clock|Network|Volume|Battery|Start)[A-Za-z_0-9]*)\s*\([^;{}]*\)\s*(?:const\s*)?\{)', re.M | re.I)
    def repl(m):
        indent=m.group('indent'); name=m.group('name')
        return m.group(0) + '\n' + indent + '    W7T_LOG_WINVER("function-entry");\n' + indent + '    W7T_LOG("CALL", std::string("function=") + "' + name + '");'
    text = pat.sub(repl, text)
    p.write_text(text, encoding='utf-8')

# Keep the migration script only for this one CI pass.
''', "branch":"feature/diagnostic-logging