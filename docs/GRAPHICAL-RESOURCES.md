# Risorse grafiche centralizzate

Il bundle `GraphicalResourceBundle` (`src/Win7Taskbar/Utilities/GraphicalResourceBundle.cs`) conserva byte-per-byte i 52 PNG già usati dal tema WPF, codificati in Base64. I file PNG WPF non sono più tenuti come binari separati nel repository: il tema runtime (`src/Win7Taskbar/Themes/Windows7.xaml`, allineato anche in `Themes/Windows7.xaml`) li carica tramite `x:Static` sul bundle.

Il mapping sotto mantiene nome originale, percorso/provenienza originale e chiave del bundle. I byte Base64 → PNG sono reversibili e non ricompressi.

## Conteggio

| Categoria | Quantità | Note |
|---|---:|---|
| Asset Base64 nel bundle (PNG) | **55** | Tutti i PNG runtime del tema WPF (incl. bandierina Start 8.1) |
| PNG nativi su disco (Aero 9-slice) | **8** | Caricati da filesystem dal backend C++/WIC |
| ICO di build (`app.ico`) | **2** | Input di build (ApplicationIcon + RC); restano file ICO |
| Immagini solo documentazione | **1** | `docs/icon-256.png` (non runtime) |
| SVG / JPG / altri formati runtime | **0** | Non presenti come asset runtime |

## Eccezioni intenzionali sul filesystem

### 1. Otto slice native Aero (`top_*.png`, `mid_*.png`, `bottom_*.png`)

Restano file PNG fisici in:

- `src/Win7Taskbar/Resources/` (copiati nell'output dal `.csproj`)
- `Resources/` alla root del repository (stessi byte; cartella del pacchetto pubblicato)

Il renderer nativo in `native/src/AeroThumbnailFrame.cpp` le carica **dal filesystem** accanto all'eseguibile tramite WIC (`CreateDecoderFromFilename`). Non è stata introdotta una modifica architetturale per spingere questi byte dal bundle gestito al core nativo: restano su disco per design.

Le stesse otto slice sono anche presenti nel bundle (chiavi `top_left`, …, `bottom_right`) come copia di comodo per eventuale uso WPF; i byte coincidono.

### 2. `app.ico` (build input, non tema runtime)

- `src/Win7Taskbar/app.ico` — `ApplicationIcon` MSBuild (incorporato nell'EXE)
- `native/resources/app.ico` — risorsa Win32 `IDI_APPICON` in `app.rc` (incorporata nella DLL)

Restano file `.ico` fisici perché gli toolchain di build (MSBuild / windres) li richiedono a compile-time. Non sono convertiti in PNG e non passano da `GraphicalResourceBundle`. I due file sono byte-identici.

### 3. Documentazione

- `docs/icon-256.png` — solo documentazione/presentazione
- screenshot del README (hostati su GitHub user-attachments, non nel tree runtime)

Non fanno parte del runtime e non vanno nel bundle.

## Mapping bundle

| ID | Nome originale | Percorso originale | Formato | Utilizzo | Chiave bundle |
|---|---|---|---|---|---|
| `dwmborder` | `DWMBorder.png` | `DWMBorder.png` | PNG | Theme resource dictionary | `dwmborder` |
| `win7_activenormal` | `ActiveNormal.png` | `Win7/ActiveNormal.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_activenormal` |
| `win7_aeropeek` | `AeroPeek.png` | `Win7/AeroPeek.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_aeropeek` |
| `win7_aeropeekblue` | `AeroPeekBlue.png` | `Win7/AeroPeekBlue.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_aeropeekblue` |
| `win7_inactivenormal` | `InactiveNormal.png` | `Win7/InactiveNormal.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_inactivenormal` |
| `win7_inactivepointerover` | `InactivePointerOver.png` | `Win7/InactivePointerOver.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_inactivepointerover` |
| `win7_clockpointerover` | `clockPointerOver.png` | `Win7/clockPointerOver.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_clockpointerover` |
| `win7_clockpressed` | `clockPressed.png` | `Win7/clockPressed.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_clockpressed` |
| `win7_desktopnormal` | `desktopNormal.png` | `Win7/desktopNormal.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_desktopnormal` |
| `win7_desktoppointerover` | `desktopPointerOver.png` | `Win7/desktopPointerOver.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_desktoppointerover` |
| `win7_desktoppressed` | `desktopPressed.png` | `Win7/desktopPressed.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_desktoppressed` |
| `win7_notrunningpointerover` | `notRunningPointerOver.png` | `Win7/notRunningPointerOver.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_notrunningpointerover` |
| `win7_notrunningpressed` | `notRunningPressed.png` | `Win7/notRunningPressed.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_notrunningpressed` |
| `win7_orbhover` | `orbHover.png` | `Win7/orbHover.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_orbhover` |
| `win7_orbnormal` | `orbNormal.png` | `Win7/orbNormal.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_orbnormal` |
| `win7_orbpressed` | `orbPressed.png` | `Win7/orbPressed.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_orbpressed` |
| `win7_overflownormal` | `overflowNormal.png` | `Win7/overflowNormal.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_overflownormal` |
| `win7_overflowpointerover` | `overflowPointerOver.png` | `Win7/overflowPointerOver.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_overflowpointerover` |
| `win7_overflowpressed` | `overflowPressed.png` | `Win7/overflowPressed.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_overflowpressed` |
| `win7_requestingattention` | `requestingAttention.png` | `Win7/requestingAttention.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_requestingattention` |
| `win7_search` | `search.png` | `Win7/search.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_search` |
| `win7_taskbarbackground` | `taskbarBackground.png` | `Win7/taskbarBackground.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_taskbarbackground` |
| `win7_taskview` | `taskview.png` | `Win7/taskview.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_taskview` |
| `win7_traypointerover` | `trayPointerOver.png` | `Win7/trayPointerOver.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_traypointerover` |
| `win7_traypressed` | `trayPressed.png` | `Win7/trayPressed.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_traypressed` |
| `win7_widgetspointerover` | `widgetsPointerOver.png` | `Win7/widgetsPointerOver.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_widgetspointerover` |
| `win7_widgetspressed` | `widgetsPressed.png` | `Win7/widgetsPressed.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_widgetspressed` |
| `win7_win7showdesktop` | `win7showdesktop.png` | `Win7/win7showdesktop.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_win7showdesktop` |
| `win7_win7taskbar` | `win7taskbar.png` | `Win7/win7taskbar.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_win7taskbar` |
| `win7_clock_tilehover` | `Win7_Clock_TileHover.png` | `Win7_Clock_TileHover.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_clock_tilehover` |
| `win7_clock_tilepressed` | `Win7_Clock_TilePressed.png` | `Win7_Clock_TilePressed.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_clock_tilepressed` |
| `win7_overflow_arrow` | `Win7_Overflow_Arrow.png` | `Win7_Overflow_Arrow.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_overflow_arrow` |
| `win7_overflow_tilehover` | `Win7_Overflow_TileHover.png` | `Win7_Overflow_TileHover.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_overflow_tilehover` |
| `win7_overflow_tilepressed` | `Win7_Overflow_TilePressed.png` | `Win7_Overflow_TilePressed.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_overflow_tilepressed` |
| `win7_tray_tilehover` | `Win7_Tray_TileHover.png` | `Win7_Tray_TileHover.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_tray_tilehover` |
| `win7_tray_tilepressed` | `Win7_Tray_TilePressed.png` | `Win7_Tray_TilePressed.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7_tray_tilepressed` |
| `bottom_center` | `bottom_center.png` | `bottom_center.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `bottom_center` |
| `bottom_left` | `bottom_left.png` | `bottom_left.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `bottom_left` |
| `bottom_right` | `bottom_right.png` | `bottom_right.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `bottom_right` |
| `mid_left` | `mid_left.png` | `mid_left.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `mid_left` |
| `mid_right` | `mid_right.png` | `mid_right.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `mid_right` |
| `multiplewindowborder` | `multiplewindowborder.png` | `multiplewindowborder.png` | PNG | Theme resource dictionary | `multiplewindowborder` |
| `startwin7orb` | `startwin7orb.png` | `startwin7orb.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `startwin7orb` |
| `startwin7orbscaled` | `startwin7orbscaled.png` | `startwin7orbscaled.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `startwin7orbscaled` |
| `startwin81flag` | `startwin81flag.png` | `startwin81flag.png` | PNG | Bandierina Start skin Windows 8.1 (3 stati) | `startwin81flag` |
| `startwin81flagscaled` | `startwin81flagscaled.png` | `startwin81flagscaled.png` | PNG | Bandierina Start skin Windows 8.1 (3 stati, alta risoluzione) | `startwin81flagscaled` |
| `startwin8beta8148orb` | `Win8102StartOrb.png` | `Resources/Win8102StartOrb.png` (rimosso dal repository dopo l'embedding) | PNG | Pulsante Start skin Windows 8 Beta 8148 (3 stati 54x54, byte-identico all'originale) | `startwin8beta8148orb` |
| `startwin8beta8148orbscaled` | generato in build | generato in build (Lanczos 2x su alpha premoltiplicato, mai committato) | PNG | Gemello 108x324 per il trigger IsScaled, come startwin7orbscaled | `startwin8beta8148orbscaled` |
| `taskbarbackground` | `taskbarBackground.png` | `taskbarBackground.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `taskbarbackground` |
| `aerobasictaskbar` | `aeroBasicTaskbar.png` | `aeroBasicTaskbar.png` | PNG | Texture della barra Aero Basic (128x40, dither ordinato contro il banding); nessun riferimento diretto trovato nel tema | `aerobasictaskbar` |
| `top_center` | `top_center.png` | `top_center.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `top_center` |
| `top_left` | `top_left.png` | `top_left.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `top_left` |
| `top_right` | `top_right.png` | `top_right.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `top_right` |
| `win7search` | `win7search.png` | `win7search.png` | PNG | Theme resource dictionary | `win7search` |
| `win7search_master` | `win7search_master.png` | `win7search_master.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7search_master` |
| `win7showdesktop` | `win7showdesktop.png` | `win7showdesktop.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7showdesktop` |
| `win7taskbar` | `win7taskbar.png` | `win7taskbar.png` | PNG | Asset disponibile nel bundle; nessun riferimento diretto trovato | `win7taskbar` |
