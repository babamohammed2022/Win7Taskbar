# Jump Lists: reverse-engineering verification of the Windows 7 behavior

Status: reference note (no code of Microsoft is used or copied).

Primary source: a string/xref dump of `explorer.exe` from **Windows Thin PC
** (a Windows 7-based, 32-bit enterprise variant). The taskbar/jump-list
code is the Windows 7 one, but the binary is not retail Windows 7; where
that distinction matters it is flagged below. Companion to ADR 13
(`docs/architecture-decisions.md`) and to `docs/FEATURE-STATUS.md`
(### Jump Lists).

This file records what the dump **directly shows**, what is **inferred**,
and which public API each Win7Taskbar decision rests on. Rule followed:
never claim "Windows 7 does X" when only a string suggests X.

## 1. Direct evidence (strings and xrefs)

| String | VA | Xrefs | What it directly shows |
|---|---|---|---|
| `Start_JumpListItems` | 0x0103734C | 7, incl. 0x0105EA84, 0x0105EE12, **0x0108E0FC** (inside the taskbar pin-management cluster) | a jump-list policy value read by multiple code paths, including the taskbar side |
| `Software\Microsoft\Windows\CurrentVersion\Explorer\ApplicationDestinations\` | 0x01050030 | 0x01050260, **0x0105EA40** (same cluster) | the full registry prefix of the pinned-items store, read by the same management code |
| `customopen` / `togglepin` | 0x0106978C / 0x010697A4 | adjacent pairs 0x0105FB10/FB17 and 0x0108EA16/E9ED | two item actions dispatched side by side in two code clusters |
| `taskbarpin` / `taskbarunpin` / `startpin` / `startunpin` | 0x01043DEC-0x01043E34 | 6 references within ~60 bytes (0x01043990-0x01043A0C), one function | a single code path emitting all four pin events, Start vs Taskbar tracked separately |
| `DesktopDestinationList` | 0x01018100 | 0x010180F0, 0x01018948 (same function) | the desktop's jump list is identified by this id |
| `NoJumpListPathTooltip` | 0x0108D274 | 0x0108D132 | a tooltip for jump-list items without a path |
| `TaskbarContextMenu` | 0x0108A378 | 0x0108A21F | the taskbar context menu module/function |
| `.(ShellLinkDestination)` | 0x01069744 | type table | an internal type: jump-list items are modeled as shell links |

Named imports from the dump that matter here:

- `SHGetPropertyStoreForWindow` (SHELL32) - window property store reads
  (AppUserModelID of a running group);
- `SHAddToRecentDocs` (SHELL32) - recent-documents store updates;
- `SHGetLocalizedName` (SHELL32) - localized section names (explains the
  negative evidence below: "Recent/Frequent/Tasks" are localized
  resources, not plain strings);
- `Shell_GetCachedImageIndexW`, `SHGetStockIconInfo`, `SHGetFileInfoW`,
  `ExtractIconExW` - item icon resolution;
- `SHParseDisplayName`, `SHCreateItemWithParent`, `SHCreateShellItem`,
  `SHBindToObject`, `SHGetPathFromIDListW`, `ShellExecuteExW` - ShellItem
  construction and item launch;
- `CoCreateInstance`, `CreateBindCtx`, `CoGetInterfaceAndReleaseStream`,
  `CoMarshalInterThreadInterfaceInStream`, `PropVariantClear` (ole32) and
  the PROPSYS converters - the COM/property-store machinery.

Constant tables without xrefs (`ApplicationDestinations` 0x0004F696 and the
duplicated `taskbarpin`/`taskbarunpin`/`startpin`/`startunpin` tables at
0x0008D6B6, 0x00091DC6, 0x000982F6): the strings are used as **identifiers**
(event/command names), not display text.

## 2. Negative evidence (useful absences)

The dump contains no `ICustomDestinationList` / `IApplicationDocumentLists`
/ `IApplicationDestinations` / `IDestinationList` strings, no CLSIDs/IIDs, no
`AppUserModelID`, no `Recent` / `Frequent` / `Tasks` / `Custom`, no
`CTaskListWnd`. The COM model is therefore **not directly demonstrated by
the dump**; it is inferred as the only public API surface that exposes the
evidenced store, and is consistent with the `ApplicationDestinations` key
plus the ole32/PROPSYS imports. Section names being localized explains
their absence (`SHGetLocalizedName` import).

Important consequence, verified against the public documentation (section
7): the Windows 7 taskbar does NOT consume the pinned store through the
public destination-list COM interfaces - it reads the store directly, and
those interfaces (as defined for Windows 7 and later) do not expose a read
of the pinned set at all. A third party therefore cannot reproduce the
pinned section through documented APIs.

## 3. Evidence vs inference

| Element | Evidence in the dump | Inference | Confidence |
|---|---|---|---|
| Pinned items stored under `HKCU\...\Explorer\ApplicationDestinations\<AppID>` | full key prefix, xref'd from the management cluster (0x0105EA40) | the key is the backing store the taskbar reads directly; items are `.lnk`-style shell links (via `ShellLinkDestination`). The format and the identity mapping are private (not documented); **not adopted** | high (key) / medium (format) / high (private, do not parse) |
| Item actions: open and pin/unpin | `customopen` + `togglepin`, adjacent xrefs in two clusters | the two operations of a Windows 7 jump-list item; `customopen` is reproducible with the public `ShellExecuteW` (adopted), `togglepin` on the pinned set is not reproducible through documented APIs (not adopted, see section 7) | high (existence) |
| `Start_JumpListItems` gates the jump lists | 7 xrefs, two inside the taskbar management cluster (0x0108E0FC) | documented DWORD of `HKCU\...\Explorer\StartMenu`; taskbar applicability from the xref position (medium) | high (Start) / medium (taskbar) |
| Desktop jump list = `DesktopDestinationList` | string + 2 xrefs in one function | the desktop's AppUserModelID (documented) | high |
| App identification via window property store | `SHGetPropertyStoreForWindow` named import | used for the group's AUMID (shared import: attribution inferred) | medium-high |
| Recent docs via `SHAddToRecentDocs` | named import | explorer maintains the recent store; the Recent section source is `IApplicationDocumentLists` (documented API, not in the dump) | direct (import) / medium-high (source) |
| `startpin`/`startunpin`/`taskbarpin`/`taskbarunpin` are pin events | 6 refs in one function; duplicated no-xref tables | telemetry/event names, not display text | medium-high |
| Item = shell link | type name `.(ShellLinkDestination)` | internal modeling of items | medium |
| Tooltip for items without a path | `NoJumpListPathTooltip` (1 xref) | name-only tooltip fallback | medium (existence) / low (content) |
| Localized section names | `SHGetLocalizedName` import | "Recent/Frequent/Tasks" are localized resources | high |
| The COM interfaces used by the taskbar | none in the dump | the taskbar reads the stores directly (no destination-list COM import); the public interfaces are the only documented surface for a third party | medium-high (absence) / high (API facts, section 7) |
| Recent/Frequent/Tasks/Custom sections | none in the dump | general Windows 7 knowledge | low (as file evidence) / high (as behavior) |

## 4. Reconstructed flow (Windows 7)

```text
Taskbar button (window group or pinned app)
    |
    +-> identity: window property store -> AppUserModelID
    |        (evidence: SHGetPropertyStoreForWindow import)
    |        / pinned shortcut metadata / implicit default id (MSDN)
    +-> policy: Start_JumpListItems (0 disables)
    |        (evidence: xrefs in the management clusters)
    +-> sections
    |        [Pinned]  HKCU\...\Explorer\ApplicationDestinations\<AppID>
    |                  (key: direct evidence; read DIRECTLY by the
    |                   taskbar - no destination-list COM import in the
    |                   binary; no public read API exists for a third
    |                   party, section 7 - NOT shown by Win7Taskbar)
    |        [Recent]/[Frequent]  automatic destinations
    |                   (IApplicationDocumentLists, documented API;
    |                    no strings in the dump)
    |        [Tasks]  window commands (general knowledge, not in the dump)
    +-> items as ShellItems (ShellLinkDestination), real icons
    |        (imports: SHGetFileInfoW/ExtractIconExW/...),
    |        tooltip = path, NoJumpListPathTooltip when unresolvable
    +-> jump view popup next to the button (left-aligned above it,
    |        small gap, static - the placement Win7Taskbar v2.62 uses;
    |        CTaskListWnd's name is not in the dump)
    +-> actions: customopen (ShellExecuteExW), togglepin (direct store
                 write, internal), window tasks
    +-> side effects: taskbarpin/taskbarunpin events (inference:
           telemetry), recent-store updates (SHAddToRecentDocs import)
```

## 5. What Win7Taskbar adopted from this verification

Implemented in `native/src/JumpListWindow.cpp` (v2.62):

- **Recent/Frequent sections**: read with `IApplicationDocumentLists`
  (SetAppID / GetList, ADLT_RECENT / ADLT_FREQUENT, SIGDN_FILESYSPATH via
  the IShellLink/IShellItem items) - the documented read API for the
  automatic destination sections. Shown with their section headers and
  real file icons, capped at ten entries per section.
- **`customopen`**: clicking a row opens it with `ShellExecuteW` on the
  resolved path (registered application), failures logged, never fatal.
- **`Start_JumpListItems` policy**: `0` disables the jump lists (open
  fails with code -4, logged, the managed side cancels the gesture) - the
  value name is evidenced in the dump, the semantics are documented.
- **Tasks section** (general knowledge, flagged as such): for groups with
  a live window, Minimize/Maximize/Restore/Move/Size reusing the existing
  `WindowManager::ExecuteCommand` path (the same one
  `W7T_ExecuteWindowCommand` exposes), plus the pre-existing graphical
  close row.
- **Tooltips**: the full path under the hovered row after ~600 ms (the
  Windows 7 delay), name-only when the item has no resolvable path (the
  `NoJumpListPathTooltip` case).

**Not adopted - the pinned (custom) section.** The dump shows the Windows
7 taskbar reading the `ApplicationDestinations` store directly (section
2: the binary imports no destination-list COM interface). The public
interfaces, as defined for Windows 7 and later, do not expose the pinned
set: `IApplicationDestinations` only removes Recent/Frequent
destinations and MSDN is explicit that the pinned items "cannot be
removed programmatically; only the user can remove them". Section 7
records the verification that killed the early v2.62 attempt (a pinned
read + unpin built on the Vista revision of those interfaces). The store
format and the AppID-to-entry mapping are private and are deliberately
not parsed.

**Not adopted - the `togglepin` write.** Same reason: there is no
documented API that removes a single pinned item (see section 7); the
taskbar pin of the application itself (the taskbar pin folder `.lnk`,
the "Pin this program to the taskbar" row) is a different store and is
handled by the pre-existing `PerformPinOrUnpin` path, unchanged.

Not adopted: telemetry (`taskbarpin`/`startpin`/...) - not a feature of
this project; `DesktopDestinationList` - this taskbar has no desktop
group button; raw parsing of the `ApplicationDestinations` store -
private format, out of scope by project rules.

## 6. Test matrix to verify on hardware (Windows 7 / 10 / 11)

1. plain app with a taskbar button - list opens, sections per data;
2. app with recent files - Recent/Frequent rows open the right files;
3. app with pinned (custom) items - no pinned rows appear (no public
   read API, by design), the list still shows the real document
   sections, the app row, the pin row and the tasks; nothing is
   invented;
4. app with Tasks - window group shows the Tasks section, commands
   behave exactly like the system menu's;
5. right-click on the taskbar button - unchanged Windows 7 context menu,
   never opens a jump list;
6. multiple instances of the same app - one group, one list, correct
   identity;
7. explorer restart (`TaskbarCreated`) - bar and jump lists come back;
8. ExplorerPatcher present - no conflicts (passive public-API reads
   only; no writes to any jump list store);
9. DWM previews present - hover preview, thumbnails and jump lists
   never fight (the gesture arbitration is unchanged);
10. empty jump list (no data at all) - only the standard rows, no
    crash, no invented entries.

## 7. API surface verification (2026-09-22)

The early v2.62 iteration built the pinned section on `IApplicationDestinations`
(`SetAppID` / `GetObjectCount` / `GetObjectList` / `GetItem`) and the unpin on
`ICustomDestinationList` (`BeginList` -> `GetObjectCollection` -> `RemoveAt`
-> `SetItemObjectList`). The release build failed with the SDK compiler
(`'IDestinationList': undeclared identifier`, `'GetObjectCount': is not a
member of 'IApplicationDestinations'`), and the following verified the
real surface. The iteration was withdrawn; this section keeps the
evidence so the API is not re-derived wrongly.

Verified against:

- the current Microsoft Learn documentation for
  `IApplicationDestinations` and `ICustomDestinationList`
  (requirements: Windows 7 and later, header `shobjidl_core.h`):
  `IApplicationDestinations` has exactly `SetAppID`, `RemoveDestination`,
  `RemoveAllDestinations`; `ICustomDestinationList` has exactly
  `SetAppID`, `BeginList`, `AppendCategory`, `AppendKnownCategory`,
  `AddUserTasks`, `CommitList`, `GetRemovedDestinations`, `DeleteList`,
  `AbortList`. The MSDN remarks for `IApplicationDestinations` state
  verbatim that the methods "do not remove items that the user has pinned
  to the Jump List. Those items cannot be removed programmatically; only
  the user can remove them."
- the Windows SDK build used by CI (the error list above proves the
  installed `shobjidl.h` declares `IApplicationDestinations` without any
  `GetObject*` method and does not declare `IDestinationList` for this
  configuration);
- independent implementations that must match the public contract (Qt
  WinExtras' `winshobjidl_p.h`, LGPL; Wine's `IApplicationDestinations`
  in `dlls/shell32/shellpath.c`, LGPL): both carry the same three-method
  `IApplicationDestinations` and the same nine-method
  `ICustomDestinationList` - no read of the pinned set, no
  `GetObjectCollection`/`SetItemObjectList`.

Conclusion: the method sets the early iteration used (`GetObjectCount` /
`GetObjectList` / `GetItem` on `IApplicationDestinations`;
`GetObjectCollection` / `SetItemObjectList` on `ICustomDestinationList`)
belong to the **Windows Vista revision** of these interfaces. The
interfaces were changed in Windows 7 (same names, smaller method sets),
which is also consistent with the dump's negative evidence: the Windows 7
taskbar does not import them for reading - it reads the store directly,
and the `togglepin` action writes the store directly as well.

Consequences for Win7Taskbar (all already in place):

- no pinned section (nothing to read through a documented API);
- no per-row unpin (nothing to write through a documented API);
- Recent/Frequent stay on `IApplicationDocumentLists` (unchanged by the
  Vista->7 revision; that is the interface CI compiled before);
- no new build definitions were needed (the temporary `NTDDI_VERSION`
  definition was reverted once the real cause was identified);
- reading the store files/registry directly was considered and rejected:
  the AppID-to-entry mapping and the value format are private, and the
  project rules require documented public APIs and explicit
  evidence-vs-inference separation.
