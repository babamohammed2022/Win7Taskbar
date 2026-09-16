using System;
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
