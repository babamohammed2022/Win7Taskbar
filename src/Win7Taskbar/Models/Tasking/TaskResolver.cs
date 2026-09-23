// Win7Taskbar - shared task model: asynchronous identity resolution
// Copyright (c) 2026 Win7Taskbar contributors
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

using System;
using System.Collections.Concurrent;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Threading;
using Win7Taskbar.Interop;

namespace Win7Taskbar.Models.Tasking
{
    /// <summary>Why a resolution job was queued.</summary>
    internal enum ResolveReason
    {
        /// <summary>New window: fill identity, title and icon.</summary>
        Discovered,

        /// <summary>The icon is not materialized (or its revision moved).</summary>
        IconStale,

        /// <summary>Re-read the caption (it can change while we watch).</summary>
        TitleRefresh,

        /// <summary>Retry identity after a transient failure.</summary>
        IdentityRetry,
    }

    /// <summary>Outcome of one resolution job.</summary>
    internal enum TaskResolveOutcome
    {
        /// <summary>The job read the window and produced results.</summary>
        Resolved,

        /// <summary>The window disappeared while the job ran: the result is
        /// informational at best and the catalog drops it.</summary>
        Stale,

        /// <summary>The job could not read the window (hung or failed): the
        /// entry keeps its last-known data.</summary>
        Failed,
    }

    /// <summary>
    /// Result record produced by the worker and applied by the catalog after
    /// re-validation. Strings are values: the record owns nothing else.
    /// </summary>
    internal sealed class ResolveResult
    {
        public ulong Hwnd { get; init; }
        public uint Generation { get; init; }
        public TaskResolveOutcome Outcome { get; init; }
        public string Title { get; init; } = string.Empty;
        public string AppId { get; init; } = string.Empty;
        public string ExePath { get; init; } = string.Empty;
        public uint IconRevision { get; init; }
    }

    /// <summary>
    /// Replaces the "runnable task scheduler" idea with the stack's own
    /// mechanism: one worker thread draining a queue (the same worker +
    /// posted-completion shape AppSearchWindow already uses), completions
    /// marshalled onto the dispatcher where the catalog applies them after
    /// re-checking the current model state. Discovery scheduling is not
    /// completion: a queued job never means a visible button.
    /// </summary>
    internal sealed class TaskResolver : IDisposable
    {
        private readonly struct ResolveJob
        {
            public ResolveJob(ulong hwnd, uint generation, ResolveReason reason,
                              string titleHint, string appIdHint, string exePathHint,
                              uint iconRevisionHint, bool wantWindowIcon, bool wantExeIcon)
            {
                Hwnd = hwnd;
                Generation = generation;
                Reason = reason;
                TitleHint = titleHint;
                AppIdHint = appIdHint;
                ExePathHint = exePathHint;
                IconRevisionHint = iconRevisionHint;
                WantWindowIcon = wantWindowIcon;
                WantExeIcon = wantExeIcon;
            }

            public ulong Hwnd { get; }
            public uint Generation { get; }
            public ResolveReason Reason { get; }
            public string TitleHint { get; }
            public string AppIdHint { get; }
            public string ExePathHint { get; }
            public uint IconRevisionHint { get; }
            public bool WantWindowIcon { get; }
            public bool WantExeIcon { get; }
        }

        private const int MaxQueuedJobs = 256;

        private readonly NativeBridge _bridge;
        private readonly AppIconCache _icons;
        private readonly Dispatcher _dispatcher;
        private readonly BlockingCollection<ResolveJob> _queue =
            new BlockingCollection<ResolveJob>(new ConcurrentQueue<ResolveJob>(), MaxQueuedJobs);
        private readonly ConcurrentDictionary<ulong, bool> _inFlight =
            new ConcurrentDictionary<ulong, bool>();
        private readonly CancellationTokenSource _shutdown = new CancellationTokenSource();
        private readonly Task _worker;
        private bool _disposed;

        /// <summary>
        /// Completion sink, wired to the catalog. Always invoked on the
        /// dispatcher thread.
        /// </summary>
        public Action<ResolveResult>? Completed { get; set; }

        public TaskResolver(NativeBridge bridge, AppIconCache icons, Dispatcher dispatcher)
        {
            _bridge = bridge ?? throw new ArgumentNullException(nameof(bridge));
            _icons = icons ?? throw new ArgumentNullException(nameof(icons));
            _dispatcher = dispatcher ?? throw new ArgumentNullException(nameof(dispatcher));

            // Same shape as AppSearchWindow's scan thread: background work,
            // posted completion. The thread is a plain worker; nothing here
            // touches presentation.
            _worker = Task.Factory.StartNew(
                Run,
                _shutdown.Token,
                TaskCreationOptions.LongRunning,
                TaskScheduler.Default);
        }

        /// <summary>
        /// Queues a resolution job for the entry. At most one job per window
        /// runs at a time (a later refresh re-queues when needed). The
        /// generation travels with the job so stale results are recognizable.
        /// </summary>
        public void Schedule(TaskEntry entry, ResolveReason reason, bool wantExeIcon)
        {
            if (_disposed || entry == null || entry.Hwnd == 0)
            {
                return;
            }

            if (!_inFlight.TryAdd(entry.Hwnd, true))
            {
                return;
            }

            var job = new ResolveJob(
                entry.Hwnd,
                entry.Generation,
                reason,
                entry.Title ?? string.Empty,
                entry.AppId ?? string.Empty,
                entry.ExePath ?? string.Empty,
                entry.IconRevision,
                wantWindowIcon: true,
                wantExeIcon: wantExeIcon);

            if (!_queue.TryAdd(job))
            {
                _inFlight.TryRemove(entry.Hwnd, out _);
            }
        }

        /// <summary>Stops accepting work and drains the queue (shutdown).</summary>
        public void CancelAll()
        {
            if (!_queue.IsAddingCompleted)
            {
                _queue.CompleteAdding();
            }
        }

        private void Run()
        {
            try
            {
                foreach (ResolveJob job in _queue.GetConsumingEnumerable(_shutdown.Token))
                {
                    ResolveResult result = Process(job);
                    Post(result, job.Hwnd);
                }
            }
            catch (OperationCanceledException)
            {
                // Shutdown: pending jobs are dropped on purpose.
            }
            catch
            {
                // A broken job must not kill the worker: the next refresh
                // re-queues what is still missing.
            }
        }

        private ResolveResult Process(in ResolveJob job)
        {
            var outcome = TaskResolveOutcome.Failed;
            string title = job.TitleHint;
            string appId = job.AppIdHint;
            string exePath = job.ExePathHint;
            uint iconRevision = job.IconRevisionHint;

            try
            {
                // Never send messages into a hung window: skip the query half
                // and keep the hints (the entry survives on last-known data).
                bool hung = GhostHandle.IsHung(job.Hwnd);
                if (!hung && _bridge.TryGetWindow(job.Hwnd, out W7TWindowInfo info))
                {
                    title = info.Title ?? string.Empty;
                    appId = info.AppId ?? string.Empty;
                    exePath = info.ExePath ?? string.Empty;
                    iconRevision = info.IconRevision;
                    outcome = TaskResolveOutcome.Resolved;
                }
                else if (!hung)
                {
                    // Not hung yet not found: the window went away mid-job.
                    outcome = TaskResolveOutcome.Stale;
                }

                if (job.WantWindowIcon && !hung)
                {
                    var image = _bridge.GetWindowIcon(job.Hwnd);
                    if (image != null)
                    {
                        _icons.Put(AppIconCache.WindowKey(job.Hwnd), image);
                    }
                }

                if (job.WantExeIcon && !string.IsNullOrEmpty(exePath))
                {
                    var image = _bridge.GetExeIcon(exePath);
                    if (image != null)
                    {
                        _icons.Put(AppIconCache.ExeKey(exePath), image);
                    }
                }

                if (!string.IsNullOrEmpty(exePath))
                {
                    // Warm the friendly-name cache off the UI thread: the
                    // tooltip reads it synchronously from the model.
                    TaskGroup.ResolveFriendlyApplicationName(exePath, title, appId);
                }
            }
            catch
            {
                if (outcome != TaskResolveOutcome.Resolved)
                {
                    outcome = TaskResolveOutcome.Failed;
                }
            }

            return new ResolveResult
            {
                Hwnd = job.Hwnd,
                Generation = job.Generation,
                Outcome = outcome,
                Title = title,
                AppId = appId,
                ExePath = exePath,
                IconRevision = iconRevision,
            };
        }

        private void Post(ResolveResult result, ulong hwnd)
        {
            try
            {
                _dispatcher.BeginInvoke(new Action(() =>
                {
                    _inFlight.TryRemove(hwnd, out _);
                    if (!_disposed)
                    {
                        try
                        {
                            Completed?.Invoke(result);
                        }
                        catch
                        {
                            // The completion is applied best-effort: a failed
                            // apply must not take down the dispatcher (the
                            // next refresh re-syncs what was lost).
                        }
                    }
                }));
            }
            catch
            {
                // Dispatcher shut down first: drop the result.
                _inFlight.TryRemove(hwnd, out _);
            }
        }

        public void Dispose()
        {
            if (_disposed)
            {
                return;
            }

            _disposed = true;
            Completed = null;
            CancelAll();
            try
            {
                // Bounded join: a stuck query must not block process exit.
                if (!_worker.Wait(TimeSpan.FromSeconds(2)))
                {
                    _shutdown.Cancel();
                }
            }
            catch
            {
                // AggregateException from cancellation: acceptable at shutdown.
            }

            _shutdown.Cancel();
            _queue.Dispose();
            _shutdown.Dispose();
        }
    }
}
