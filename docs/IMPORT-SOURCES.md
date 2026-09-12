# Come importare i sorgenti originali in questo repository

Il repository nasce da due bundle pubblicati su MediaFire dal maintainer:

| Bundle | Contenuto | Link |
| --- | --- | --- |
| `win7taskbarforgithub.zip` | codice sorgente del progetto (C#/WPF + parti native C++) | <https://www.mediafire.com/file/erka1phr202p1nz/win7taskbarforgithub.zip/file> |
| `Win7Taskbar-v2.58-win-x64.zip` | build distribuita di riferimento (x64) | <https://www.mediafire.com/file/wyfsjmgwvg0bohc/Win7Taskbar-v2.58-win-x64.zip/file> |

Puoi importarli in **tre modi**, dal piu' semplice al piu' automatico.

---

## Opzione 1 - GitHub Actions (nessun software da installare)

1. Su GitHub: **Add file -> Create new file**
2. Nome del file: `.github/workflows/win7taskbar-ci.yml`
3. Incolla il contenuto di [`ci/github-actions/win7taskbar-ci.yml`](../ci/github-actions/win7taskbar-ci.yml)
   e committa su `main`.
4. Vai in **Actions -> Win7Taskbar CI -> Run workflow**, lascia i default e conferma.

Il runner GitHub scarica i due bundle, li estrae e li committa nel branch
indicato (default: `arena/01a0966f-win7taskbar`). Con `build_release: true`
compila anche la release self-contained; con `publish_release: true` la
pubblica come **1.0.0-alpha** con gli asset pronti all'uso.

> Nota: il workflow va creato a mano perche' l'automazione di Arena non ha il
> permesso `workflows` su questo repository. Gli altri file li gestisce da sola.

---

## Opzione 2 - Windows, senza Python (PowerShell)

Da una copia locale del repository:

```powershell
powershell -ExecutionPolicy Bypass -File tools\Get-Sources.ps1 -Extract -CommitPush `
  -Branch arena/01a0966f-win7taskbar
```

Lo script scarica i bundle, verifica e estrae gli archivi e pusha tutto sul
branch indicato. Non richiede il .NET SDK ne' Python: usa solo PowerShell 5.1.

---

## Opzione 3 - Qualsiasi sistema, con Python 3.8+

```bash
python3 tools/fetch_sources.py --out _incoming --extract
python3 tools/fetch_sources.py --out _incoming --extract --commit-push \
        --branch arena/01a0966f-win7taskbar
```

---

## Se MediaFire non e' raggiungibile

MediaFire blocca alcuni ambienti (sandbox, runner aziendali, reti con filtro).
In quel caso:

* esegui lo script da un PC normale (casa/ufficio) e pusha,
* oppure apri la pagina MediaFire nel browser, scarica i due ZIP e caricali in
  una issue/commento su GitHub: verranno importati da li'.

I nomi dei file attesi sono **esattamente** quelli della tabella in alto:
gli script li usano per riconoscere cio' che hanno scaricato.
