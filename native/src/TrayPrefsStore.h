/* Win7Taskbar - native core - notification-area icon preferences store
 * Copyright (c) 2026 Win7Taskbar contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * ============================================================================
 * WHY THIS FILE EXISTS
 *
 * The tray's pin/overflow logic needs to remember, per icon, what the user chose, and
 * the program is portable by design: extracting the zip and deleting it must
 * leave the system exactly as it was. That rules out ANY persistent registry
 * write. Windows itself keeps the same information in a blob under
 * Explorer's TrayNotify key; the old code mirrored the CONCEPT into
 * HKCU\SOFTWARE\Win7Taskbar\TrayIconPrefs2 (a DWORD per icon).
 *
 * This store replaces that registry key completely:
 *
 *   - one file: %LOCALAPPDATA%\Win7Taskbar\trayicons.ini
 *     (same folder as the managed toolbars.ini, same "delete the folder and
 *     everything is gone" contract);
 *   - plain text, one "name=value" per line, two sections;
 *   - the first load MIGRATES the legacy registry key into the file and
 *     then DELETES the key, so even the value an older build wrote does not
 *     survive this upgrade (self-cleaning, also after a crash: the check
 *     runs at every startup, not only when the file is missing);
 *   - saving rewrites the whole file through a temp file +
 *     MoveFileEx(REPLACE_EXISTING), so a kill mid-write can never truncate
 *     the stored choices.
 *
 * Threading: every public method takes the lock; the map is the single
 * source of truth once loaded. Icon names are produced by TrayService's
 * MakePreferenceName ("exe-path-in-lowercase#uid", '/'-only separators),
 * which never contain '=', so the one-split parser below is total.
 * ============================================================================
 */

#ifndef W7T_TRAY_PREFS_STORE_H
#define W7T_TRAY_PREFS_STORE_H

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace w7t {

/* The three states of the Win7 page, plus "the user never chose". Values are
 * the on-disk numbers; they must stay 0/1/2 (kBehaviorNone is memory-only). */
enum TrayIconBehavior : int32_t {
    kBehaviorNone = -1,
    kBehaviorShow = 0,       /* Show icon and notifications    */
    kBehaviorNotifyOnly = 1, /* Only show notifications        */
    kBehaviorHide = 2,       /* Hide icon and notifications    */
};

class TrayPrefsStore {
public:
    static TrayPrefsStore& Instance();

    /* Saved behavior of one icon, or kBehaviorNone when the user has never
     * chosen. `name` is the MakePreferenceName key. */
    int32_t Behavior(const std::wstring& name);
    bool Has(const std::wstring& name);

    /* Persist one choice. behavior outside 0..2 removes the entry (same as
     * "no explicit choice" - the shell rule applies again). */
    void SetBehavior(const std::wstring& name, int32_t behavior);

    /* "Always show all icons and notifications on the taskbar". */
    bool AlwaysShow();
    void SetAlwaysShow(bool on);

    /* System-icon switches of the "Turn system icons on or off" page.
     * `kind` is the TrayFallbackIcons SystemIconKind value (1=network,
     * 2=volume, 3=battery); other kinds are always on. */
    bool SystemIconOn(int32_t kind);
    void SetSystemIconOn(int32_t kind, bool on);

    /* "Restore default icon behaviors": forget every per-icon choice (the
     * two global switches stay, they are separate settings). */
    void ClearIconBehaviors();

    /* Number of per-icon entries (for logging and the dialog header). */
    size_t IconCount();

private:
    TrayPrefsStore() = default;
    void LoadIfNeeded();               /* takes m_mutex          */
    void EnsureLoadedLocked();         /* reads file + migration */
    void SaveLocked();                 /* temp file + replace   */
    static std::wstring FilePath();

    std::mutex m_mutex;
    bool m_loaded = false;
    std::map<std::wstring, int32_t> m_behaviors;
    bool m_alwaysShow = false;
    std::map<int32_t, bool> m_systemOff; /* only OFF kinds are stored */
};

} /* namespace w7t */

#endif /* W7T_TRAY_PREFS_STORE_H */
