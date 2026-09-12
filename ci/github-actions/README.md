# Pipeline di build e release (GitHub Actions)

GitHub esegue **solo** i file che stanno in `.github/workflows/`. L'automazione che
gestisce questo repository non ha il permesso `workflows`, quindi il workflow va
creato a mano **una volta sola** (30 secondi), copiandolo da questa cartella.

## Attivare il workflow

1. Apri questo file su GitHub:
   `ci/github-actions/release.yml`
   (pulsante **Raw**, poi seleziona tutto e copia: `Ctrl+A`, `Ctrl+C`).
2. Vai su: **Add file → Create new file**
3. Nome del file (importante, esattamente questo):
   `.github/workflows/release.yml`
4. Incolla il contenuto e premi **Commit changes** sul branch `main`.
5. Vai su **Actions → release** per vedere le esecuzioni. Per lanciarlo a mano:
   **Run workflow**.

> Prima volta: se la release non viene creata, apri
> **Settings → Actions → General → Workflow permissions** e seleziona
> **Read and write permissions**, poi salva.

## Cosa fa

| Momento | Risultato |
| --- | --- |
| Push su `main` (o `master`) | compila il nativo con MSVC, pubblica il pacchetto self-contained e lo carica come **artifact** della run |
| Push di un tag `v*` (es. `v1.0.0-alpha`) | come sopra, e allega lo zip alla **GitHub Release** con quel nome |
| Pull request | verifica soltanto che il pacchetto si compili e sia davvero self-contained |
| Manuale (**Run workflow**) | uguale al push su `main` |

La verifica in fondo al workflow è il punto centrale del progetto: controlla che nel
pacchetto ci siano `System.Private.CoreLib.dll` e `includedFrameworks` nel
`Win7Taskbar.runtimeconfig.json`. Se mancano, la build fallisce: significa che
l'utente finale avrebbe dovuto installare .NET, cosa che **non deve succedere**.

## Pubblicare la versione 1.0.0-alpha

Il numero di versione viene letto da `src/Win7Taskbar/Win7Taskbar.csproj`
(`<Version>1.0.0-alpha</Version>`), e il nome dell'archivio è
`Win7Taskbar-1.0.0-alpha-win-x64.zip`.

Per creare la release:

```bash
git tag v1.0.0-alpha
git push origin v1.0.0-alpha
```

Il workflow compila e allega lo zip alla release `v1.0.0-alpha`.

## Alternativa senza Actions

In alternativa il pacchetto si crea sul proprio PC con il doppio clic su
`COMPILA.bat` (vedi `docs/GUIDA-RAPIDA-IT.md`) e si carica a mano nella release
con il pulsante **Attach binaries**.
