# Control Panel cascade: Open-Shell analysis and Win7Taskbar mapping

This document records what was read in the Open-Shell source before the
Control Panel submenu of the Start Menu right column was written, and how
each observed behaviour maps to Win7Taskbar. Open-Shell (Open-Shell-Menu,
MIT License) was used as a **behavioural reference only**: no code, bitmaps
or resources were copied, and Win7Taskbar does not depend on Open-Shell at
run time. Attribution is kept in `THIRD-PARTY-NOTICES.md`.

Source read (Open-Shell-Menu `master`, `Src/StartMenu/StartMenuDLL/` unless
stated otherwise):

| File | What was inspected |
|---|---|
| `CustomMenu.cpp` | `g_StdCommands7` entry `control_panel` (folder `FOLDERID_ControlPanelFolder`, `MENU_TRACK`, icon `shell32.dll,137`); `ControlPanelCategories` switch; `MENU_CONTROLPANEL` id |
| `MenuContainer.cpp` | `AddFirstFolder` (enumeration, exclusions, `CONTAINER_CONTROLPANEL`), `LoadItemOrder` (sorting), `InitWindowInternal` (column packing, limits), `OnMouseMove` / `OnTimer` (hover timer), `OnLButtonUp` (click on a folder item), `MAX_MENU_ITEMS`, the Administrative Tools special case, the "(Empty)" placeholder |
| `MenuCommands.cpp` | `ActivateItem` (`ACTIVATE_OPEN` placement of the submenu, `ACTIVATE_EXECUTE` launch through `IContextMenu`), `CloseSubMenus`, Ctrl+Shift "runas" |
| `SkinManager.cpp` | Default skin metrics (`Submenu_padding`, text/icon/arrow paddings, separator width, arrow bitmap sizes `g_ArrowSizes96`) |
| `Src/Skins/Win7Aero7/SkinDescription.txt` | Windows 7 Aero skin submenu section (white submenus, 2px padding, thin frame, Segoe UI 9pt, selection/separator bitmaps) |
| `MenuContainer.h` | `CONTAINER_CONTROLPANEL` ("this is the control panel, don't go into subfolders"), `s_MenuLimits` |

## Behaviour mapping

| Open-Shell component | Observed behaviour | Win7Taskbar equivalent |
|---|---|---|
| `CustomMenu.cpp` `control_panel` | Right-column item bound to `FOLDERID_ControlPanelFolder` (`::{26EE0668-A00A-44D7-9371-BEB064C98683}\0`, the flat "All Control Panel Items" view). In the Windows 7 style the item is forced to `MENU_EXPANDED`, so it is a folder item and cascades. | `StartMenuViewModel.BuildRightLinks`: the existing "control" link gets `HasCascade = true`. The row shows the Windows 7 "display as a menu" arrow (`StartMenuWindow.xaml` item template). |
| `MenuContainer.cpp` `AddFirstFolder` + `CONTAINER_CONTROLPANEL` | `IShellFolder::EnumObjects(FOLDERS|NONFOLDERS)`; each child via `SHCreateItemFromIDList`; skip `SFGAO_HIDDEN`; skip items with an empty display name or a display name starting with the Control Panel GUID; nothing cascades further (`bFolder=false`); cap `MAX_MENU_ITEMS = 2000`; `(Empty)` row when nothing is left. | `ControlPanelItems.Enumerate`: `SHGetKnownFolderItem(FOLDERID_ControlPanelFolder)` (parsing-name fallback) -> `BHID_EnumItems` -> `IEnumShellItems`; same exclusions; same cap; `ControlPanelCascade` shows the localized `(Empty)` row. This is a live shell enumeration, so `.cpl` files, legacy applets restored by third-party tools and vendor applets appear exactly as in Explorer's Control Panel. |
| `MenuContainer.cpp` (after sorting) | Administrative Tools (`::{D20EA4E1-3957-11D2-A40B-0C5020524153}`) is re-flagged as a folder so it cascades. | `ControlPanelItem.IsFolder` is true for that entry; it is drawn with an arrow and opens as a folder through its default verb (no nested cascade: kept partial on purpose to stay small). |
| `MenuContainer.cpp` `LoadItemOrder` | Folders first; names compared with `StrCmpLogicalW` when `NumericSort` (default on), otherwise `CompareString(LINGUISTIC_IGNORECASE)`. | `ControlPanelItems.Sort`: folders first, `StrCmpLogicalW` (shlwapi), `CurrentCultureIgnoreCase` fallback. |
| `MenuContainer.cpp` `OnMouseMove` / `OnTimer` (`TIMER_HOVER`) | Hovering a folder item starts a timer with `MenuDelay` (default `SPI_GETMENUSHOWDELAY`); changing item restarts it; `WM_MOUSELEAVE` kills it; when it fires the submenu opens. Hovering a non-folder item while a submenu is open closes the submenu after the same delay. Opening a new submenu closes the previous one. | `StartMenuWindow.ControlPanelCascade.cs`: `DispatcherTimer` with `SystemParameters.MenuShowDelay`; armed in `OnRightItemMouseEnter`, cancelled in `OnRightItemMouseLeave`; a non-cascading row arms a delayed close; moving into the cascade cancels the pending close. |
| `MenuContainer.cpp` `OnLButtonUp` (`SingleClickFolders` default 0) | A click on a folder item calls `ActivateItem(ACTIVATE_OPEN)`: the submenu opens at once; nothing is launched. | `OnRightItemClick`: `HasCascade` rows open the cascade immediately instead of `OpenRightLink`. The folder itself is still reachable from the row's context menu ("Open"). |
| `MenuContainer.cpp` drawing (`m_Submenu == index`) | The parent item is drawn selected while its submenu is open. | The row's `IsExpanded` is set while the cascade is open; `RightRowStyle` has a `DataTrigger` that applies the hover highlight. |
| `MenuCommands.cpp` `ActivateItem` (`ACTIVATE_OPEN`) | `m_MaxWidth = limits.right - itemRect.right`; the submenu is placed to the right of the item (`x = itemRect.right - (padding.left + frame)`), to the left when it does not fit, clamped to the limits otherwise; vertically top-aligned with the item minus `(padding.top + frame)`, bottom-aligned when it would cross the bottom limit, clamped otherwise; `AW_BLEND` when the system menu fade is enabled. | `ControlPanelCascade.Open`: same order of decisions, in screen pixels, with `inset = frame + padding`; a short opacity fade when `SystemParameters.IsMenuFadeEnabled`. |
| `MenuContainer.cpp` `InitWindowInternal` (`ScrollType` default `1 = NoScroll`) | `s_MenuLimits` = monitor work area extended to the monitor edge on the taskbar side; `maxHeight = limits height - submenu padding`; items are packed top to bottom and **a new column starts when `y > 0 && y + itemHeight > maxHeight`**; the widest item decides the column width and `SameSizeColumns` (default) gives every column that width; `separatorWidth` (4) between columns; the total width is the sum. The number of columns is an outcome of the packing, not a threshold. | `ControlPanelCascade.Open`: identical greedy packing over the same limits (`StartMenuWindow.CascadeLimits`: `rcWork` extended to `rcMonitor` on the side of the bar); one column on a tall screen, two or more when the list is longer than the available height; `ColumnGap = 4`. |
| `SkinManager.cpp` defaults + `Win7Aero7` skin | Submenu: white background, 2px padding inside a 1px frame, Segoe UI 9pt; icon 16px with 3px padding, text padding `{1,2,8,2}`, arrow area `5 + 7 + 4` px, so rows are 22px high; light-blue rounded selection; text `#000000`, disabled `#7F7F7F`. | `ControlPanelCascade` constants (`FrameDip`, `PadDip`, `ItemHeight = 22`, `IconSize = 16`, `IconPad = 3`, `TextPadLeft/Right`, `ArrowArea`), Segoe UI 12 DIP, a light-blue gradient with rounded corners for the hot row. The skin bitmaps are not used. |
| `MenuCommands.cpp` `ActivateItem` (`ACTIVATE_EXECUTE`) | Launch = `IShellItem` -> `BHID_SFUIObject` `IContextMenu` -> `QueryContextMenu(CMF_DEFAULTONLY)` -> `GetMenuDefaultItem` -> `InvokeCommand(CMIC_MASK_UNICODE|CMIC_MASK_FLAG_LOG_USAGE, SW_SHOWNORMAL)`; Ctrl+Shift asks for the `runas` verb; never `ShellExecute`/`control.exe`. | `ShellContextMenu.TryInvokeDefault` (new, next to the existing `TryShow`): same sequence on `SHParseDisplayName` + `SHBindToParent` + `GetUIObjectOf`. `StartMenuViewModel.LaunchControlPanelItem` calls it and falls back to `explorer.exe shell:::{...}` or `control.exe <file.cpl>` only if the shell refuses. |
| `MenuContainer.cpp` context menu on items | Right click shows the item's real `IContextMenu` with `CMF_NORMAL`. | `ControlPanelCascade.ItemContextRequested` -> `ShellContextMenu.TryShow(parsingName, ...)` under the same `_suppressDeactivate` guard the Start Menu already uses for `RunItemMenu`. |
| `MenuCommands.cpp` `CloseSubMenus`, `MenuContainer.cpp` `OnKeyDown` | Escape closes the deepest submenu first. | `OnPreviewKeyDown`: Escape closes the cascade when open, otherwise the menu. `Dismiss()` always closes the cascade. |
| Icon loading (`CItemManager`, small icons) | 16px shell icons per item, loaded asynchronously. | `StartMenuIcons.FromParsingName(parsingName, 16 * dpi)` on a thread-pool task, applied to the row when ready. |

## Integration notes

* The cascade is a WPF `Window` owned by `StartMenuWindow`, `WS_EX_NOACTIVATE |
  WS_EX_TOOLWINDOW`, never activated. The Start Menu's `Deactivated` ->
  `Dismiss` logic is therefore untouched.
* `StartMenuClickAway` (the low-level mouse hook) now treats a window whose
  `GA_ROOTOWNER` is the Start Menu as part of the menu, so clicks inside the
  cascade are not clicks away.
* The enumeration is started when the menu is presented (`PresentAbove`) so
  the list is usually ready by the time the hover delay fires; if not, the
  cascade opens when the enumeration completes, provided the row is still
  hovered.
* Everything fails soft: an enumeration failure shows `(Empty)`, a launch
  failure falls back to the classic process launch, and any exception in the
  hover/open/close path is logged with `Debug.WriteLine` and leaves the menu
  usable.

## Left out on purpose

* Nested cascade for Administrative Tools (opens as a folder instead).
* Category view (`ControlPanelCategories`) and `REST_NOCONTROLPANEL` policy.
* Keyboard navigation inside the cascade beyond Escape.
* Skin bitmaps (selection, separator, arrows): approximated with vector
  brushes of the same geometry.
