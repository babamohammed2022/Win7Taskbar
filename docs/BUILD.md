# Compilare Win7Taskbar

Obiettivo del progetto: **l'utente finale non deve installare nessuna versione di .NET**.
Le build pubblicate sono *self-contained*: contengono già il runtime .NET e girano
su Windows 10/11 x64 appena scompattate.

---

## 1. Build "un clic" sul tuo PC (Windows)

Requisiti:

* Windows 10/11 x64
* PowerShell 5.1 (di serie) e, per la parte nativa, CMake + Visual Studio Build Tools
* Il **.NET SDK**: se manca, lo script prova a installarlo da solo con `winget`
  oppure con `dotnet-install.ps1`

```powershell
git clone https://github.com/babamohammed2022/Win7Taskbar.git
cd Win7Taskbar
git checkout arena/01a0966f-win7taskbar
powershell -ExecutionPolicy Bypass -File build\Build-Release.ps1
```

Al termine trovi in `.\release\`:

| File | Descrizione |
| --- | --- |
| `Win7Taskbar-1.0.0-alpha-win-x64-portable.zip` | cartella portatile: scompatta e avvia l'eseguibile |
| `Win7Taskbar-1.0.0-alpha-win-x64-selfcontained.exe` | eseguibile singolo, nessuna installazione |

Opzioni utili:

```powershell
# versione diversa
...\Build-Release.ps1 -Version 1.0.0-beta

# senza eseguibile singolo (build piu' rapida e leggera)
...\Build-Release.ps1 -NoSingleFile

# salta la parte nativa C++
...\Build-Release.ps1 -NoNative
```

---

## 2. Build automatica con GitHub Actions

Il workflow [`ci/github-actions/win7taskbar-ci.yml`](../ci/github-actions/win7taskbar-ci.yml)
fa tutto su un runner Windows: import dei sorgenti, build self-contained,
creazione degli ZIP e (opzionale) pubblicazione della release **1.0.0-alpha**.

Per attivarlo basta copiarlo in `.github/workflows/win7taskbar-ci.yml` su `main`
e lanciarlo da **Actions -> Win7Taskbar CI -> Run workflow**.

Input disponibili:

| Input | Significato |
| --- | --- |
| `target_branch` | branch su cui importare i sorgenti (default: `arena/01a0966f-win7taskbar`) |
| `skip_fetch` | salta il download da MediaFire (sorgenti già importati) |
| `build_release` | compila gli asset self-contained |
| `publish_release` | crea/aggiorna la release GitHub `v1.0.0-alpha` |

---

## 3. Note tecniche

* **Self-contained**: `dotnet publish -r win-x64 --self-contained true`.
  Non serve il runtime .NET installato sul PC di destinazione.
* **Eseguibile singolo**: aggiunge `-p:PublishSingleFile=true`,
  `-p:IncludeNativeLibrariesForSelfExtract=true` e
  `-p:EnableCompressionInSingleFile=true`. È la modalità più comoda per
  l'utente medio (un solo file da scaricare), ma all'avvio deve scompattare
  il contenuto in una cartella temporanea: il primo avvio è più lento.
* **WPF non supporta il trimming**: le dimensioni restano intorno ai 100-180 MB.
  Per ridurle si può usare la modalità *framework-dependent* (richiede però il
  .NET Desktop Runtime installato), oppure la variante `-NoSingleFile`.
* **WPF compila solo su Windows**: per questo la build automatica usa
  `windows-latest`. Non è possibile generare la release da Linux/macOS.
* **Parte nativa C++**: se nel repository è presente un `CMakeLists.txt`, lo
  script di build lo compila in `build-native\` e copia le DLL prodotte accanto
  all'eseguibile. Se il progetto usa vcpkg, viene passato automaticamente il
  toolchain file di vcpkg presente sul runner.
