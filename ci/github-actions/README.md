# Pipeline GitHub Actions — documentazione storica

> **Avviso di archiviazione.** La manutenzione del progetto è stata interrotta per mancanza di tempo. L'automazione delle release è stata disattivata e `.github/workflows/release.yml` è stato rimosso. L'unica release conservata è `v1.3.26-alpha`, incompleta rispetto agli obiettivi iniziali.

Questo file e il workflow di esempio in questa cartella sono conservati come documentazione storica e per eventuale consultazione in fork indipendenti. Le istruzioni seguenti **non** descrivono un processo attivo in questo repository e non devono essere usate per riattivare le release qui.

## Procedura storica (solo per fork indipendenti)

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

## Comportamento del workflow storico

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

## Esempio storico di pubblicazione (non attivo in questo repository)

Il numero di versione viene letto da `src/Win7Taskbar/Win7Taskbar.csproj`
(`<Version>1.0.0-alpha</Version>`), e il nome dell'archivio è
`Win7Taskbar-1.0.0-alpha-win-x64.zip`.

Per creare la release:

```bash
git tag v1.0.0-alpha
git push origin v1.0.0-alpha
```

Il workflow compila e allega lo zip alla release `v1.0.0-alpha`.

## Packaging locale del codice sorgente archiviato

In alternativa il pacchetto si crea localmente con il doppio clic su
`compilation files/build.bat` (vedi `docs/GUIDA-RAPIDA-IT.md`). In una fork mantenuta
autonomamente, il pacchetto può essere allegato a una release gestita dalla fork; questo
repository non pubblica nuove release.
