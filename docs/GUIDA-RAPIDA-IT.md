# Win7Taskbar 1.0.0-alpha — guida rapida (italiano)

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

1. Vai su **https://github.com/babamohammed2022/Win7Taskbar/releases**
2. Scarica `Win7Taskbar-1.0.0-alpha-win-x64.zip`.
3. **Scompatta tutto lo zip** in una cartella qualsiasi (es. `C:\Win7Taskbar`).
   Mantieni le sottocartelle `Themes`, `Resources` e `Languages` accanto a
   `Win7Taskbar.exe`: il tema viene letto da disco all'avvio.
4. Avvia **`Win7Taskbar.exe`**.

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
  caricata. Incolla quel testo in una issue su GitHub.

* **Le icone o il tema non si vedono.** Hai scompattato lo zip dentro un'altra
  cartella senza copiare `Themes\`, `Resources\` e `Languages\`: riscompatta tutto
  insieme con "Estrai tutto".

* **Antivirus / SmartScreen.** Il programma non è firmato digitalmente: se Windows
  avvisa, scegli *Ulteriori informazioni* → *Esegui comunque*. Il codice sorgente è
  pubblico in questo repository.

* **L'anteprima delle finestre non appare.** È voluto: in questa alpha le anteprime
  sono disattivate (vedi lo stato nel `README.md`); il tooltip con il nome
  dell'applicazione funziona.

---

## 5. Compilare da soli (facoltativo)

### Modo più semplice — doppio clic

1. Scarica il repository (**Code → Download ZIP**) e scompattalo, oppure
   `git clone https://github.com/babamohammed2022/Win7Taskbar.git`
2. Doppio clic su **`COMPILA.bat`**.

Lo script:
* installa da solo il **.NET 8 SDK** se manca (senza diritti di amministratore);
* compila anche la parte nativa C++ se hai CMake, altrimenti usa le DLL native
  **già incluse** nel repository (`dist\Win7TaskbarCore.dll`, `dist\W7TInject.dll`);
* crea il pacchetto **self-contained** in `dist-package\` e l'archivio
  `Win7Taskbar-1.0.0-alpha-win-x64.zip` nella cartella principale;
* apre la cartella con lo zip finito.

### Modo "progetto" — script ufficiale

```powershell
pwsh -File build/publish.ps1 -Zip            # pacchetto + archivio
pwsh -File build/publish.ps1 -Zip -SkipNative   # riusa le DLL native presenti
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
No. Le release sono *self-contained*: il runtime .NET è dentro il pacchetto. Funziona
anche su un Windows senza .NET installato.

**Posso usare la barra di Windows insieme a questa?**
No, Win7Taskbar nasconde la barra originale: è la sostituzione della barra.

**Ci sono rischi?**
Il programma non modifica file di sistema: crea una finestra AppBar propria e nasconde
la barra di Windows. Chiudendolo con il comando *Close Win7Taskbar* la barra originale
torna disponibile. Come per ogni software di questo tipo, tieni comunque un punto di
ripristino.

**Come segnalo un problema?**
Apri una issue su GitHub con: versione di Windows, cosa hai fatto, cosa ti aspettavi e
il testo del log (sezione 4).
