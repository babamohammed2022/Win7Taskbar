# Win7Taskbar — guida rapida archiviata (italiano)

> **Avviso di archiviazione.** La manutenzione è stata interrotta per mancanza di tempo. L'unica release conservata è [Win7Taskbar v1.3.26-alpha](https://github.com/babamohammed2022/Win7Taskbar/releases/tag/v1.3.26-alpha), ancora incompleta rispetto agli obiettivi iniziali del progetto. Questa guida, originariamente redatta per la versione v1.0.0-alpha, potrebbe non descrivere integralmente la release conservata. Non sono previsti ulteriori aggiornamenti né supporto da parte dei manutentori. Si ringraziano tutti coloro che hanno partecipato al progetto con contributi, verifiche, traduzioni, segnalazioni o materiali.

Questa guida è per chi **vuole usare** Win7Taskbar e, se serve, per chi vuole
**compilarlo da sé**. Non serve sapere nulla di programmazione.

---

## 1. Che cosa fa

Sostituisce la barra delle applicazioni di Windows 10/11 con quella di **Windows 7**:
superbar con raggruppamento delle finestre, jump list, area di notifica, flyout di
sistema (orologio, rete, volume, batteria), pannello overflow e finestra Proprietà.

Viene mostrata solo la barra nuova: la barra di Windows resta nascosta e viene
liberata correttamente quando chiudi il programma.

---

## 2. Requisiti

| Cosa | Valore |
| --- | --- |
| Sistema | Windows 10 (21H2 o successivo) oppure Windows 11 |
| Architettura | **x64** (64 bit) |
| .NET installato | **no, non serve**: il runtime è incluso nel pacchetto |

---

## 3. Installazione (utente finale)

1. Apri la pagina dell'unica release conservata:
   **https://github.com/babamohammed2022/Win7Taskbar/releases/tag/v1.3.26-alpha**
2. Scarica `Win7Taskbar-1.3.26-alpha-win-x64.zip` (il runtime .NET è incluso).
3. **Scompatta tutto lo zip** in una cartella qualsiasi (es. `C:\Win7Taskbar`).
   Mantieni le sottocartelle `Themes`, `Resources` e `Languages` accanto a
   `Win7Taskbar.exe`: il tema viene letto da disco all'avvio.
4. Avvia **`Win7Taskbar.exe`**.

### Verifica del file scaricato (facoltativa)

Se nella pagina della release è pubblicata l'impronta **SHA-256** del pacchetto,
la si può verificare aprendo PowerShell nella cartella del download e digitando:

```powershell
Get-FileHash .\Win7Taskbar-1.3.26-alpha-win-x64.zip -Algorithm SHA256
```

Il valore `Hash` che compare deve essere identico a quello pubblicato: se
coincide, il file è integro e non è stato manomesso.

> **Prima di tutto**, consiglio di creare un punto di ripristino di Windows, così
> puoi tornare indietro in un istante se qualcosa non ti piace.

### Come si chiude

Tasto destro sull'**orologio** (in basso a destra) → **Properties** →
**"Close Win7Taskbar"**. Usa sempre questo comando: la barra di Windows viene
ripristinata in modo pulito.

### Impostazioni

Sempre dal tasto destro sull'orologio → **Properties**. Sono disponibili dieci
lingue (italiano incluso): orologio, stile dei flyout, ricerca app, lingua, area di
notifica, barre aggiuntive e chiusura.

---

## 4. Se qualcosa non funziona

* **La barra non compare.** Apri la cartella del programma, tieni premuto `Shift`,
  fai clic destro su uno spazio vuoto → *Apri finestra PowerShell qui* e digita:

  ```powershell
  .\Win7Taskbar.exe
  ```

  Le righe che scorrono dicono se la parte nativa (`Win7TaskbarCore.dll`) è stata
  caricata. Il progetto è archiviato: non sono previsti gestione delle segnalazioni o supporto da parte dei manutentori.

* **Le icone o il tema non si vedono.** Hai scompattato lo zip dentro un'altra
  cartella senza copiare `Themes\`, `Resources\` e `Languages\`: riscompatta tutto
  insieme con "Estrai tutto".

* **Antivirus / SmartScreen.** Il programma non è firmato digitalmente: se Windows
  avvisa, scegli *Ulteriori informazioni* → *Esegui comunque*. L'accesso al codice
  sorgente è subordinato alle impostazioni di accesso del repository e alla licenza.

* **Funzionalità incomplete.** La release conservata è incompleta. `FEATURE-STATUS.md`
  documenta storicamente lo stato delle funzionalità; non costituisce un impegno a
  correggerle o fornire supporto.

---

## 5. Compilare da soli (facoltativo)

### Modo più semplice — doppio clic

1. Scarica il repository (**Code → Download ZIP**) e scompattalo, oppure
   `git clone https://github.com/babamohammed2022/Win7Taskbar.git`
2. Doppio clic su **`build.bat`** (dentro la cartella `compilation files/`).

Lo script:
* installa da solo il **.NET 8 SDK** se manca (senza diritti di amministratore);
* compila anche la parte nativa C++ se hai CMake, altrimenti usa le DLL native
  **già incluse** nel repository (`dist\Win7TaskbarCore.dll`, `dist\W7TInject.dll`);
* crea il pacchetto **self-contained** in `dist-package\` e uno zip il cui nome
  versionato è determinato dal codice sorgente archiviato;
* apre la cartella con lo zip finito.

### Script di packaging conservato nel codice sorgente

```powershell
pwsh -File "compilation files/publish.ps1" -Zip              # pacchetto + archivio
pwsh -File "compilation files/publish.ps1" -Zip -SkipNative  # riusa le DLL native presenti
```

### Dove si trova la parte .NET

Tutta la parte .NET/WPF sta in **`src/`**:

```
src/Win7Taskbar/Win7Taskbar.csproj      applicazione WPF (il file da compilare)
src/RetroBar.Shim/RetroBar.Shim.csproj  shim di compatibilità del tema
Win7Taskbar.sln                         soluzione Visual Studio con entrambi
```

Con Visual Studio 2022 o Rider puoi aprire direttamente `Win7Taskbar.sln` e premere
*Compila*: non serve altro (le DLL native sono già nel repository).

---

## 6. Domande frequenti

**Serve installare .NET?**
No. L'unica release conservata, `v1.3.26-alpha`, è *self-contained*: il runtime .NET
è incluso nel pacchetto.

**Posso usare la barra di Windows insieme a questa?**
No, Win7Taskbar nasconde la barra originale: è la sostituzione della barra.

**Ci sono rischi?**
Il programma non modifica file di sistema: crea una finestra AppBar propria e nasconde
la barra di Windows. Chiudendolo con il comando *Close Win7Taskbar* la barra originale
torna disponibile. Come per ogni software di questo tipo, tieni comunque un punto di
ripristino.

**Come segnalo un problema?**
Il progetto è archiviato e non prevede gestione delle segnalazioni o supporto da parte
dei manutentori. Il codice resta consultabile e sviluppabile autonomamente da chi dispone
dell'accesso al repository, nel rispetto della licenza applicabile.
