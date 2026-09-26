# Mappa del backend Notification Area

Questo documento registra l'audit del percorso reale prima della distribuzione di
una nuova trial. Il backend resta stratificato: nessuna fonte singola viene
considerata obbligatoria.

## Flusso effettivo

```text
applicazione
  Shell_NotifyIcon(NIM_ADD/MODIFY/DELETE/SETVERSION)
    -> Shell_TrayWnd dello shim
    -> WM_COPYDATA/SHELLTRAYDATA
    -> TrayService::HandleCopyDataLocal
    -> TrayService::ApplyMessage
    -> cache m_icons/m_order/m_byGuid
    -> TrayToolbar + TrayOverflowWindow + WPF UI esistente

Explorer già avviato
  -> TrayNotify callback (snapshot iniziale, se disponibile)
  -> ToolbarWindow32 legacy (fallback Win32, se leggibile)
  -> UIA tray XAML (Windows 11; visible e overflow solo dopo apertura reale)
  -> Merge nella stessa cache

TaskbarCreated
  -> ri-risoluzione Explorer / AppBar
  -> nuovo snapshot fallibile
  -> riapertura reale del chevron solo per la raccolta overflow
```

All'avvio l'AppBar/work area viene registrata **prima** di `Shell_TrayWnd`. Il
ricevitore tray si mette topmost, spinge la `Shell_TrayWnd` di Explorer in
fondo, poi invia `TaskbarCreated` una volta così le applicazioni già avviate
rieseguono `Shell_NotifyIcon` verso di noi. Alla chiusura il broadcast viene
ripetuto per restituire le registrazioni a Explorer.

`WM_COPYDATA` è la fonte live per le applicazioni. `ITrayNotify`/`ITrayNotifyWin8`
è una tecnica privata di compatibilità e serve alla fotografia/callback di
Explorer; non è presentata come API Microsoft pubblica. La toolbar e UIA sono
letture di fallback e non autorizzano da sole la cancellazione della cache se
la passata è incompleta.

## RetroBar / ManagedShell -> Win7Taskbar

| Meccanismo RetroBar/ManagedShell | Equivalente Win7Taskbar | Funzionalità mancante o rischio rilevato | Punto di integrazione |
|---|---|---|---|
| `ExplorerTrayService.Run()` individua `Shell_TrayWnd -> TrayNotifyWnd -> SysPager -> ToolbarWindow32` | `ExplorerTrayReader::FindTrayToolbar()` e `ReadAllEx()` | La catena può essere vuota o assente su Windows 11; `FindWindow` non deve scegliere lo shim | `TrayService::ReconcileWithExplorer`, solo se la toolbar è realmente leggibile |
| `TB_BUTTONCOUNT` / `TB_GETBUTTON` con `VirtualAllocEx` e `ReadProcessMemory` | `ExplorerTrayReader::ReadToolbar()` | `TBBUTTON::dwData` e la struttura privata sono reverse engineering; timeout o lettura parziale non significano DELETE | `ExplorerTrayReadResult.okVisible/okOverflow`, RAII `ProcessHandle`/`RemoteBuffer` |
| `TrayNotify` + callback sincrono per ottenere le voci correnti | `TrayNotifyReader::ReadSnapshot()` | CLSID/IID/layout sono non documentati; Explorer o integrità/UAC possono negare COM | Snapshot opzionale all'avvio e dopo `TaskbarCreated`; fallisce senza bloccare gli altri backend |
| `INotificationCB.Notify` consegna preferenza, HWND, UID, GUID, tooltip, HICON | `TrayNotifySnapshotItem` e `TrayService::MergeTrayNotifySnapshot()` | La preferenza non è una fotografia assoluta della posizione visuale | `shellPreference` è metadato; toolbar/overflow/UIA e preferenza locale determinano la vista |
| `TrayService` riceve i `WM_COPYDATA` delle nuove registrazioni | `TrayService::HandleCopyDataLocal()` + `ApplyMessage()` | Payload privato, versioni 32/64 bit e lifetime dell'HICON | `NIM_ADD`, `NIM_MODIFY`, `NIM_DELETE`, `NIM_SETVERSION`; `CopyIcon`/RAII |
| Identità `GUID` quando presente, altrimenti finestra + UID | `TrayIconEntry.guidKey`/`m_byGuid`, `TrayIconKey` come fallback | Un ri-registro può cambiare HWND/UID; chiavi basate solo su exe duplicano le voci | `RebindByGuid()` prima di ADD/MODIFY e DELETE; `exePath` resta metadato |
| `NotifyIcon`/`NotifyIconList` aggiorna una collezione WPF osservabile | `TrayService::CopyTo()` alimenta la UI tray esistente | Un secondo modello UI causerebbe divergenza visibile/overflow | `W7T_TrayIconInfo`, `TrayOverflowWindow`, `TaskbarWindow` esistenti |
| Comportamento tooltip/click/versione ManagedShell | `lastUpdateSource`, `callbackMessage`, `version`, `SendClick()` | NIF_GUID, NIF_STATE, HICON nullo e `NOTIFYICON_VERSION_4` devono essere mantenuti | Forward del click con packing dipendente dalla versione; nessun callback inventato |
| Overflow classico `NotifyIconOverflowWindow` | `ExplorerTrayReader` quando esiste; UIA overflow XAML quando aperto | Su Windows 11 un host può non esistere finché il flyout è chiuso | `RequestOverflowFlyout()` invoca il chevron reale; nessun `SWP_SHOWWINDOW` per materializzare un falso pannello |
| UIA per elementi moderni | `Win11TrayReader` con `SystemTray.NotifyIconView/IconView` | UIA non è un enumeratore completo e non espone sempre owner/HICON | Snapshot best-effort, retention/hysteresis; alias UIA verso la cache TrayNotify quando tooltip coincide |
| Riavvio Explorer / reregistrazione | `TaskbarCreated`, `NoteExplorerRestart()`, reset snapshot COM | Il reset della cache o il broadcast all'avvio crea duplicati | Solo riascolto e riconciliazione; broadcast manuale resta confinato alla pagina legacy |

Il codice e l'integrazione sono originali C++ e non copiano l'interfaccia o la UI
di RetroBar. Le sequenze di sistema e i contratti osservati derivano da
ManagedShell/RetroBar, progetto Apache License 2.0; l'attribuzione completa è in
`docs/CREDITS.txt`.

## Identità e fusione

La precedenza è:

1. `guidItem` se `NIF_GUID` è presente e non nullo;
2. `hWnd + uID`;
3. `exePath`/`pszExeName` solo metadato diagnostico e risoluzione dell'icona; non deduplica.

Le fonti non sovrascrivono ciecamente la cache:

- `WM_COPYDATA` aggiorna il live state dell'applicazione;
- `TrayNotify` completa identità, preferenza e fotografia iniziale;
- toolbar aggiorna stato, callback, versione e pixel della shell;
- UIA aggiorna visibilità moderna e click; se una voce TrayNotify è già
  presente, il suo `uiaUid` è un alias e non una seconda voce;
- `NotifyIconSettings\...\IsPromoted` resta un indizio/preferenza, mai
  l'unica prova di visibilità;
- `NIM_DELETE` e owner/process termination sono i percorsi forti di rimozione.

## Hardening del crash osservato

Il percorso `ReportShellRects -> W7T_SetShellRects` arrivava dal dispatcher WPF,
ma `TrayToolbar` appartiene al thread del servizio. Prima `SetShellRects()`
invocava direttamente `MoveWindow`, `TB_AUTOSIZE` e altre operazioni comctl32
da un thread estraneo: questo è compatibile con lo stack osservato
`COMCTL32.dll -> Win7TaskbarCore.dll -> MSCTF.dll` dopo `appbar: dopo SetPos`.

Ora il layout viene accodato con un solo `WM_APP+110` e applicato sul thread che
ha creato `SysPager`/`ToolbarWindow32`; `TrayToolbar::SetArea()` ha anche una
guardia sul thread proprietario. Gli handle del toolbar, image list, bitmap,
processi remoti, COM e callback sono rilasciati tramite RAII o cleanup esplicito.
Questa è una mitigazione locale verificabile, non un'attribuzione del fault a
`msvcrt.dll` senza dump.

## Vincoli di distribuzione

Prima di una nuova trial servono almeno:

- build nativa con la modifica cross-thread e `TrayNotifyReader`;
- verifica statica che nessuna lettura toolbar incompleta rimuova voci;
- log che distingua `WM_COPYDATA`, `TrayNotify callback`, `Explorer toolbar`,
  `UIA tray` e fallback;
- test su Windows 10/8.1 del percorso legacy invariato;
- test su Windows 11 con Explorer restart, ADD/MODIFY/DELETE, HICON cambiato,
  GUID/HWND rinnovati e overflow aperto dal chevron reale;
- nessuna cancellazione automatica di `TrayNotify` e nessun broadcast/reset
  all'avvio della pagina legacy.
