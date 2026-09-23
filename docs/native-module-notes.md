# Integrazione moduli nativi (suite snippet Appendice A) — note

Data: 2026-09-23 · Branch: `arena/01a0cd8b-win7taskbar` · Reviewer: code-review
questo documento è materiale di analisi/osservazione (regola §7: le
informazioni derivate da analisi vivono in `docs/`, separate dal codice;
nessun nome simbolico/indirizzo/offset deriva da dump entra nel codice).

## 1. Mapping snippet → file (16 moduli tutti integrati)

| # | Snippet | File di destinazione | Scelta |
|---|---------|----------------------|--------|
| 01 | raii_win32 | `native/include/RaiiWrappers.h` (esteso) | estensione: `scope_guard`/`on_scope_exit`, alias `unique_hicon/hfont/hhook/hlocal/hglobal/cotask`, `adopt_*`, `MemoryDc`; riusa `unique_handle`, `DcHandle`, `GdiSelector` |
| —  | com_ptr   | `RaiiWrappers.h` → `w7t::raii::ComPtr` | forma compatibile con `detail::ComPtr` di `ImmersiveFlyouts.h`; unificazione in un solo tipo = refactor collaterale RINVIATO (diff puliti) |
| 02 | exception_safe_loop | `native/src/ExceptionGuards.h` (nuovo) | `RunMessageLoop`/`GuardedWndProc`/`GuardedRun`; niente `/EHa` globale, niente `catch(CException&)` |
| 03 | appbar_raii | `native/src/AppBarPositioner.h/.cpp` (nuovo) | RAII `ABM_NEW/SETPOS/QUERYPOS/REMOVE` per-monitor; coesiste col singleton `AppBarService` del percorso `W7T_AppBarRegister` (convergenza futura: vedi §4) |
| 04 | edge_rotation | `native/src/EdgeRotation.h` (nuovo) | puro (`NextEdgeCw/Ccw`, `EdgeRelationFor`, flag orientamento); unit-testato |
| 05 | taskbar_window | `native/src/TaskbarWindow.h/.cpp` (nuovo) | classe `Win7Taskbar_TryWnd`→ NO: **`Win7Taskbar_TrayWnd`** (mai `Shell_TrayWnd`, regola progetto); callback managed `on_edge_rotated`/`on_taskbar_recreated` |
| 06 | tray_sizer | `native/src/TraySizer.h/.cpp` (nuovo) | **corpi implementati** dal commento: hit-test bordo libero → `HTLEFT/HTRIGHT/HTTOP/HTBOTTOM`, clamp `WM_SIZING` (min/max LOGICAL × DPI 07), consolidamento `WM_EXITSIZEMOVE`; righe/colonne = dichiarazioni (`// TODO: verificare`) |
| 07 | dpi_metrics | `native/src/DpiMetrics.h/.cpp` (nuovo) | `MonitorDpi`/`WindowDpi` riusano la scala `GetDpiForScreenRect`/`GetDpiForWindowSafe` di `Common.h`; aggiunti `ScaleForDpi` + `IsProcessPerMonitorDpiAware` (via `GetThreadDpiAwarenessContext` dinamico) |
| 08 | tray_icon | `native/src/TrayIconHost.h/.cpp` (nuovo) | lato CLIENT `Shell_NotifyIconW` (complementa il SERVER di `TrayService`); re-add su `TaskbarCreated`; balloon soppressi in quiet time (`SHQueryUserNotificationState` dinamico) |
| 09 | shell_hook | `native/src/ShellHookReceiver.h/.cpp` (nuovo) | `RegisterShellHookWindow` (shell32 dinamico) + messaggio registrato `SHELLHOOK` (**correzione**: non numeri privati); dispatch `HSHELL_*` |
| 10 | subclass_guard | `native/src/WindowSubclass.h/.cpp` (nuovo) | RAII `SetWindowSubclass`/`RemoveWindowSubclass` (comctl32 v6) |
| 11 | taskbar_list3 | `native/src/TaskbarListClient.h/.cpp` (nuovo) | `ITaskbarList3` **solo interfaccia pubblica SDK** (`shobjidl_core.h`) + `CoCreateInstance`; `raii::ComPtr`; CoInit STA bilanciato |
| 12 | band_geometry | `native/src/BandLayout.h/.cpp` (nuovo) | split intero tasks/tray/clock lungo il bordo (logica portata); puro; unit-testato |
| 13 | registry_policy | `native/src/RegistryPolicy.h/.cpp` (nuovo) | `WriteWithBackup`/`HasBackup`/`RestoreBackup`/`DeleteBackup` con backup in `HKCU\Software\Win7Taskbar\RegistryBackup\<policy>`; i 4 valori README sono **descrittori**; aggancio chirurgico in `EnsureWin32BatteryFlyoutValue` |
| 14 | localappdata_store | `native/src/LocalAppDataStore.h/.cpp` (nuovo) | `%LOCALAPPDATA%\Win7Taskbar\` formalizzato (`Dir`/`File`/`BackupFile`) + `IniStore` generico; `trayicons.ini` resta di `TrayPrefsStore` |
| 15 | clock_tray_layout | `native/src/ClockTrayLayout.h/.cpp` (nuovo) | puro split icons/clock (clock all'estremita' esterna, `reverse` per le facce interne); dichiarazioni flyout (`// TODO: verificare`) |
| 16 | registry_shadow | `native/src/RegistryShadow.h/.cpp` (nuovo) | `Propose`/`ReadEffective`/`Commit`/`Discard`/`State` + guard virtualizzazione (**correzione API**, vedi §3); `Commit` passa da `WriteWithBackup` |

## 2. Scelte di integrazione (nessuna duplicazione)

- **Strati esistenti riusati, non rifatti**: `Common.h` (DPI ladder),
  `ScopeGuards.h`/`RaiiWrappers.h` (RAII GDI), `AppBarService` (protocollo
  `ABM_*` completo, percorso a barra singola), `TrayService` (server
  `Shell_TrayWnd`), `TrayPrefsStore` (`trayicons.ini`), `DiagnosticLogger`.
- **WM_USER/WM_APP nostri centralizzati** in `PrivateWindowMessages.h`
  (`kMsgAppBarNotify`, `kMsgTrayIconCallback`). Numeri standard già in uso e
  non toccati: `TB_*` (ExplorerTrayReader `WM_USER+23/24/29`), JumpList
  (`WM_APP+0x177`), ricerca/switch (`WM_APP+1/2`).
- **Namespace/tipi**: gli snippet usano `w7tb`; il codice usa `w7t` e i
  pattern locali (nessun rename di tipi esistenti).
- **Eccezioni**: solo `std::exception` ai confini Win32 (`ExceptionGuards.h`);
  i proc strada fanno `GuardedWndProc`. Nessun `catch(CException&)`, nessun
  SEH-pattern ATL/MSVC; il filtro SEH esistente di `TrayService` resta
  l'unica zona SEH e non è stato toccato.
- **Scritte registry**: ogni scrittura passa da `RegistryPolicy::
  WriteWithBackup` (README). Unica eccezione documentata: i `Reg*` diretti
  dei percorsi di recovery no-alloc (`BestEffortRestoreBatteryKeyNoAlloc`,
  `RecoverBatteryFlyoutKeyFromBackup`) — devono restare allocation-free per
  il filtro crash.

## 3. Deviazioni dichiarate (regola §7 + conflitti risolti)

1. **Riferimenti al dump**: nel codice restano solo i tag di tracciabilità
   (`// CERTO`, `// IPOTESI`, `// TODO: verificare`) e affermazioni su
   comportamento PUBBLICO (es. `ABM_SETPOS` con `rc` in/out). Numeri di
   riga/simboli interni dei dump vivono solo come prosa in questo file:
   l'analisi di snippet riguarda la struttura del taskbar di shell in
   `taskbar.cpp` (registrazione appbar, orientamento ai bordi, bande) e
   `trayui.cpp` (banda notifica/orologio), risolta qui nei moduli sopra.
   **La mappa di offset di vtable NON è inclusa da nessuna parte**: non
   serve a compilare o eseguire (regola §7: "none are needed to build or
   run it"). Questa deviazione rispetto a "mantieni i riferimenti al dump"
   è esplicita e motivata dal conflitto con §7.
2. **API inesistenti negate**: lo snippet 16 cita `RegQueryKeyExW` +
   `KEY_IS_VIRTUALIZED` — non esistono in alcun SDK (verificato: le query
   documentate sono `RegQueryValueEx`/`RegGetValue`/`RegEnumKeyEx`).
   Sostituzione: `NtQueryKey(KeyVirtualizationInformation)` caricato da
   `ntdll` dinamicamente, con fallback graceful "non virtualizzato" e
   `// TODO: verificare` su 8.1/10/11.
3. **Interfacce private COM escluse** (`ITaskGroup`, `ITaskItem`,
   `IRunnableTaskScheduler2`, `TOID_ResolveWindow`): non documentate,
   instabili, incompatibili con l'ABI COM MinGW. Il modulo 11 usa solo
   `ITaskbarList3` pubblico.
4. **Shell hook**: il pattern corretto è `RegisterWindowMessageW(L"SHELLHOOK")`
   + `RegisterShellHookWindow`; non numeri `WM_APP` privati.
5. **com_ptr**: sostituito da `w7t::raii::ComPtr`; l'unificazione con
   `detail::ComPtr` esistente è un refactor collaterale volontariamente
   evitato (vincolo "diff puliti").

## 4. Convocazioni/convivenze da sciogliere in futuro (non bloccanti)

- `AppBarPositioner` (per-monitor) vs `AppBarService` (singleton): il
  secondo serve il percorso esportato `W7T_AppBarRegister`. Convergere in
  una sola implementazione quando il percorso multi-barra atterra.
- `FreeEdgeSizer` multi-riga: `SetRows`/`SetColumns` dichiarati,
  `UpdateRowColumnSizes` stub (milestone multi-barra).
- `ClockFlyoutRect`: stub (milestone polish tray).
- Unificare `raii::ComPtr` e `detail::ComPtr` in un refactor dedicato.

## 5. Matrice di smoke test (manuale, su macchine reali)

| Cenare | 8.1 | 10 | 11 |
|--------|-----|----|----|
| DPI per-monitor (spessore barra corretto su secondario 150%) | ✔ atteso via `GetDpiForMonitor` | ✔ `GetDpiForWindow` | ✔ `GetDpiForWindow` |
| Rotazione bordi + clamp resize (drag bordo libero, min/max) | manuale | manuale | manuale |
| Appbar: work area rispettata da altre finestre massimizzate | manuale | manuale | manuale |
| Ico­ne di notifica dopo riavvio Explorer (TaskbarCreated) | manuale | manuale | manuale |
| Balloon: soppressi in quiet time/presentazione | manuale | manuale | manuale |
| ITaskbarList3 progress/overlay dal client | manuale | manuale | manuale |
| Policy: backup `RegistryBackup\<policy>` + restore pulito | manuale | manuale | manuale |
| Shadow: virtualizzazione rilevata (o graceful) | verificare | verificare | verificare (assente) |

## 6. Elenco `// TODO: verificare` aperti

1. `RegistryShadow.cpp` — status `NtQueryKey` su chiavi non virtualizzabili
   (8.1/10/11 reali).
2. `TraySizer.cpp` — `UpdateRowColumnSizes` (corpi multi-riga/colonne).
3. `ClockTrayLayout.cpp` — `ClockFlyoutRect` (ancoraggio flyout calendario).
4. `EdgeRotation.h` — soglia `3/4` in `EdgeRelationFor` (barre non standard).
5. `DpiMetrics.cpp` — comportamento con awareness context unknown (-1).

## 7. Build & test

- `cmake -S native -B native/build -A x64 -DW7T_BUILD_TESTS=ON`
- `cmake --build native/build --config Release`
- `ctest --test-dir native/build -C Release --output-on-failure`
  (`layout_tests` = 04/12/15 + puri 06; `registry_tests` = 13/16 su chiavi
  HKCU temporanee con cleanup).
- CI: `build-validation.yml` esegue configure con `-DW7T_BUILD_TESTS=ON`,
  build MSVC e `ctest`.
