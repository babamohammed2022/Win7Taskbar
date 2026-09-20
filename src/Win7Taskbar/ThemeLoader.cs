// Win7Taskbar - composizione dei dizionari del tema
// Copyright (c) 2026 Win7Taskbar contributors
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Windows;
using System.Windows.Markup;
using System.Xml.Linq;
using Win7Taskbar.Utilities;

namespace Win7Taskbar
{
    /// <summary>
    /// Costruisce l'albero di ResourceDictionary del tema.
    ///
    /// IL PROBLEMA
    /// -----------
    /// Themes/Windows7.xaml contiene 16 stili nella forma
    ///     &lt;Style x:Key="TaskbarWindow" BasedOn="{StaticResource TaskbarWindow}"&gt;
    /// cioe' estendono una versione preesistente di se stessi, che deve arrivare
    /// da un dizionario base (il tema nasce come skin innestata su RetroBar).
    ///
    /// Quando WPF risolve quello StaticResource, ESCLUDE dalla ricerca la chiave
    /// che sta costruendo - e la esclude in tutto l'albero raggiungibile dal
    /// dizionario, non solo nel file corrente. Verificato sperimentalmente: con
    /// Base.xaml messo come fratello, come genitore, o annidato nei
    /// MergedDictionaries del tema, il risultato e' sempre
    ///     "Cannot find resource named 'TaskbarWindow'".
    ///
    /// LA SOLUZIONE
    /// ------------
    /// L'unica disposizione che funziona e' avere il riferimento a Base.xaml
    /// DICHIARATO DENTRO il markup del tema, cosi' che il parser lo veda come
    /// parte del dizionario stesso mentre costruisce le chiavi differite.
    ///
    /// Per ottenerlo SENZA modificare il file del repository, il XAML viene
    /// letto come testo, gli si inietta in memoria un
    ///     &lt;ResourceDictionary.MergedDictionaries&gt;
    /// che punta a Base.xaml, e il risultato viene passato a XamlReader.
    /// Il file Themes/Windows7.xaml su disco resta byte-identico all'originale.
    /// 
    /// IL TERZO PROBLEMA (1.21.5: mandava in crash l'avvio con
    /// "Provide value on 'System.Windows.Markup.StaticExtension' threw an
    /// exception. Line number '10' and line position '6'.")
    /// ------------------------------------------------------------------
    /// Il tema dichiara xmlns:bundle="clr-namespace:Win7Taskbar.Utilities"
    /// SENZA assembly e lo usa in 45 <x:Static Member="bundle:..."/> per le
    /// immagini del bundle base64. Nello XAML COMPILATO (Overrides.xaml)
    /// l'omissione e' lecita (vale lo stesso assembly); in quello caricato a
    /// RUNTIME con XamlReader.Load il tipo non si risolve mai, e il primo
    /// x:Static muore dentro StaticExtension.ProvideValue. La "riga 10" e'
    /// la prima x:Static del documento SERIALIZZATO in memoria (il salvataggio
    /// ricompatta il tag radice su una riga sola e aggiunge la dichiarazione
    /// XML, quindi i numeri non corrispondono al file su disco).
    ///
    /// Il tentativo 1.21.4 (XamlTypeMapper custom) era inefficace: il
    /// costruttore XamlTypeMapper(string[]) vuole NOMI DI ASSEMBLY, non una
    /// mappa di namespace, quindi non mappava nulla.
    ///
    /// La correzione e' QualifyClrNamespaces: prima del parse, ogni xmlns
    /// clr-namespace senza assembly viene qualificato IN MEMORIA con
    /// ";assembly=Win7Taskbar" (il file su disco resta byte-identico). In piu',
    /// OGNI x:Static viene pre-validato via reflection (ValidateStaticMembers)
    /// e riverificato dopo il parse (VerifyLoadedImages), cosi' un futuro
    /// refuso nomina chiave e membro invece di un numero di riga.
    /// </summary>
    public static class ThemeLoader
    {
        private const string BaseUri =
            "pack://application:,,,/RetroBar;component/Themes/Base.xaml";

        private const string OverridesUri =
            "pack://application:,,,/Win7Taskbar;component/Themes/Overrides.xaml";

        /* v1.21.42: sfondo opaco grigio-azzurro della skin "Windows 7 Aero
         * Basic". E' un dizionario di soli pennelli, compilato nell'assembly
         * come Overrides.xaml (non un file di tema su disco): il tema resta
         * Themes/Windows7.xaml, intatto. */
        private const string AeroBasicUri =
            "pack://application:,,,/Win7Taskbar;component/Themes/AeroBasic.xaml";

        /* Skin "Windows 8 Beta 8148" (id 3): dizionario compilato con le
         * sole chiavi della beta (pulsante Start, vetro piu' marcato e
         * cornice delle anteprime rettangolare), mergiato per ultimo
         * sopra il tema Windows 7 intatto, come AeroBasic.xaml. */
        private const string Win8Beta8148Uri =
            "pack://application:,,,/Win7Taskbar;component/Themes/Win8Beta8148.xaml";

        private static readonly XNamespace Presentation =
            "http://schemas.microsoft.com/winfx/2006/xaml/presentation";

        private static readonly XNamespace XamlNamespace =
            "http://schemas.microsoft.com/winfx/2006/xaml";

        /// <summary>
        /// Restituisce il dizionario radice pronto per Application.Resources.
        /// </summary>
        public static ResourceDictionary Build()
        {
            // Controllo preliminare: il tema e la cartella Resources (slice
            // native) stanno su disco accanto all'eseguibile. L'errore piu'
            // frequente e' uno ZIP estratto senza le sottocartelle.
            VerifyLayoutOnDisk();

            var root = new ResourceDictionary();
            root.MergedDictionaries.Add(LoadThemeWithBase());
            root.MergedDictionaries.Add(new ResourceDictionary
            {
                Source = new Uri(OverridesUri, UriKind.Absolute)
            });

            /* v1.21.42 - skin "Windows 7 Aero Basic" (id 2): il file del
             * tema e' lo stesso Windows7.xaml (nessuna modifica al tema
             * Aero esistente); qui viene mergiato PER ULTIMO il piccolo
             * dizionario AeroBasic.xaml, che sostituisce soltanto i
             * pennelli dello sfondo vetroso con la superficie opaca
             * grigio-azzurra della reference. Fra dizionari mergiati vince
             * l'ultimo aggiunto (la stessa precedenza di cui vive
             * Overrides.xaml), quindi le sue chiavi hanno la meglio su
             * tema e overrides; con le altre skin questo blocco non viene
             * eseguito e nulla cambia. */
            if (IsAeroBasicSelected())
            {
                root.MergedDictionaries.Add(new ResourceDictionary
                {
                    Source = new Uri(AeroBasicUri, UriKind.Absolute)
                });

                try
                {
                    DiagnosticLogger.Write("THEME",
                        "skin Aero Basic: sfondo opaco applicato sopra il " +
                        "tema Windows 7 (AeroBasic.xaml mergiato per ultimo)");
                }
                catch
                {
                    /* la diagnostica non e' mai un requisito */
                }
            }

            /* Skin "Windows 8 Beta 8148" (id 3): stesso schema dell'Aero
             * Basic (tema Windows7.xaml intatto + piccolo dizionario
             * mergiato PER ULTIMO, con il pulsante Start della beta, il
             * vetro piu' marcato a texture identiche e la cornice delle
             * anteprime rettangolare); con le altre skin
             * questo blocco non viene eseguito e nulla cambia. Il merge e'
             * blindato dentro MergeWin8Beta8148Overrides (pre-validazione
             * degli sprite + try/catch): se l'asset manca o il parse
             * fallisce, la skin ricade sul puro Windows 7 invece di
             * rompere l'avvio. */
            if (IsWin8Beta8148Selected())
            {
                MergeWin8Beta8148Overrides(root);
            }

            // v2.21: SuperbarButton eredita da TaskButton, ma il BasedOn
            // non puo' stare nel BAML (StaticResource irrisolvibile al
            // parse del dizionario isolato): lo colleghiamo qui, prima
            // che qualunque finestra usi lo stile.
            try
            {
                // l'indicizzatore cerca anche nei dizionari mergiati
                object? sbObj = root["SuperbarButton"];
                object? sfObj = root["SuperbarButtonFlashing"];
                object? tbObj = root["TaskButton"];
                if (sbObj is System.Windows.Style superbar &&
                    tbObj is System.Windows.Style taskButton)
                {
                    superbar.BasedOn = taskButton;
                    if (sfObj is System.Windows.Style flashing)
                    {
                        // Inherit the base button geometry directly. Basing
                        // this on SuperbarButton would let its IsRunning
                        // trigger override the orange notification template.
                        flashing.BasedOn = taskButton;
                    }
                }
            }
            catch
            {
                // senza eredita' lo stile resta valido (template proprio)
            }

            // v1.21.22 - con la skin Windows 8.1 la barra e le anteprime sono
            // squadrate: le chiavi della variante 8.1 dichiarate nel tema
            // prendono il posto di quelle di Overrides.xaml. Con la skin
            // Windows 7 questa chiamata non avviene e nulla cambia.
            if (IsWindows81Selected())
            {
                ShadowWindows81Keys(root);
            }

            return root;
        }

        /// <summary>
        /// v1.21.22 - true quando la skin scelta in Proprieta' e' la 8.1, con
        /// la stessa prudenza del resto del caricamento: una configurazione non
        /// leggibile lascia il valore di ripiego, cioe' la skin Windows 7.
        /// </summary>
        private static bool IsWindows81Selected()
        {
            try
            {
                return RetroBar.Utilities.TaskbarThemeIds.Normalize(
                           RetroBar.Utilities.Settings.Instance.ThemeSelection)
                       == RetroBar.Utilities.TaskbarThemeIds.Windows81;
            }
            catch
            {
                return false;
            }
        }

        /// <summary>
        /// v1.21.42 - true quando la skin scelta in Proprieta' e' la
        /// "Windows 7 Aero Basic": stessa prudenza di IsWindows81Selected,
        /// una configurazione non leggibile lascia il ripiego (skin
        /// Windows 7 Aero, senza il dizionario degli sfondi opachi).
        /// </summary>
        private static bool IsAeroBasicSelected()
        {
            try
            {
                return RetroBar.Utilities.TaskbarThemeIds.Normalize(
                           RetroBar.Utilities.Settings.Instance.ThemeSelection)
                       == RetroBar.Utilities.TaskbarThemeIds.Windows7AeroBasic;
            }
            catch
            {
                return false;
            }
        }

        /// <summary>
        /// True quando la skin scelta in Proprieta' e' la "Windows 8 Beta
        /// 8148": stessa prudenza di IsWindows81Selected, una
        /// configurazione non leggibile lascia il ripiego (skin Windows 7,
        /// senza il dizionario della beta).
        /// </summary>
        private static bool IsWin8Beta8148Selected()
        {
            try
            {
                return RetroBar.Utilities.TaskbarThemeIds.Normalize(
                           RetroBar.Utilities.Settings.Instance.ThemeSelection)
                       == RetroBar.Utilities.TaskbarThemeIds.Windows8Beta8148;
            }
            catch
            {
                return false;
            }
        }

        /// <summary>
        /// Skin "Windows 8 Beta 8148" (id 3): mergia per ultimo il
        /// dizionario Win8Beta8148.xaml (pulsante Start della beta, vetro
        /// piu' marcato a texture identiche e cornice delle anteprime
        /// rettangolare). Blindato in due strati:
        /// prima si pre-validano gli sprite nel bundle (chiavi presenti E
        /// decodificabili, come fa ValidateStaticMembers per il tema), poi
        /// il merge avviene dentro try/catch. Su qualunque problema si
        /// salta il dizionario e la skin resta il puro Windows 7 (orb e
        /// vetro originali): mai una barra rotta per un asset cosmetico,
        /// e il chiamante (Build/ReapplyNow) non deve gestire nulla.
        /// </summary>
        private static void MergeWin8Beta8148Overrides(ResourceDictionary root)
        {
            if (!Win8Beta8148SpritesAvailable())
            {
                try
                {
                    DiagnosticLogger.Write("THEME",
                        "skin 8 Beta 8148: sprite non disponibili nel bundle, " +
                        "ricado sul puro Windows 7 (Win8Beta8148.xaml saltato)");
                }
                catch
                {
                    /* la diagnostica non e' mai un requisito */
                }
                return;
            }

            try
            {
                root.MergedDictionaries.Add(new ResourceDictionary
                {
                    Source = new Uri(Win8Beta8148Uri, UriKind.Absolute)
                });

                try
                {
                    DiagnosticLogger.Write("THEME",
                        "skin 8 Beta 8148: pulsante Start della beta, vetro " +
                        "rinforzato e anteprime rettangolari sopra il tema " +
                        "Windows 7 (Win8Beta8148.xaml mergiato per ultimo)");
                }
                catch
                {
                    /* la diagnostica non e' mai un requisito */
                }
            }
            catch (Exception ex)
            {
                /* Il dizionario non si e' mergiato (BAML mancante o parse
                 * fallito): la skin resta il puro Windows 7. Si annota il
                 * motivo in diagnostica e si prosegue: Build() non deve
                 * mai lanciare per questo. */
                try
                {
                    DiagnosticLogger.Write("THEME",
                        "skin 8 Beta 8148: merge Win8Beta8148.xaml fallito " +
                        "(" + ex.GetType().Name + ": " + ex.Message + "), " +
                        "ricado sul puro Windows 7");
                }
                catch
                {
                    /* la diagnostica non e' mai un requisito */
                }
            }
        }

        /// <summary>
        /// True se entrambi gli sprite della beta sono nel bundle grafico
        /// e si decodificano (GraphicalResourceBundle.Get ritorna null in
        /// entrambi i casi di errore, senza lanciare). Qualunque eccezione
        /// imprevista vale false: meglio il ripiego Windows 7.
        /// </summary>
        private static bool Win8Beta8148SpritesAvailable()
        {
            try
            {
                return GraphicalResourceBundle.Keys.Contains("startwin8beta8148orb") &&
                       GraphicalResourceBundle.Get("startwin8beta8148orb") != null &&
                       GraphicalResourceBundle.Keys.Contains("startwin8beta8148orbscaled") &&
                       GraphicalResourceBundle.Get("startwin8beta8148orbscaled") != null;
            }
            catch
            {
                return false;
            }
        }

        /// <summary>
        /// v1.21.22 - cornici e pulsanti a spigoli vivi della skin Windows 8.1.
        ///
        /// Il tema 8.1 dichiara le proprie varianti (chiavi "Win81..."), ma il
        /// dizionario Overrides.xaml e' mergiato DOPO il tema e, come documenta
        /// Microsoft ("Merged resource dictionaries"), fra due dizionari
        /// mergiati vince quello aggiunto per ultimo: le chiavi del tema non
        /// verrebbero mai raggiunte. Qui la variante 8.1 viene copiata nel
        /// dizionario PRINCIPALE, che ha la precedenza su tutti i mergiati (la
        /// precedenza e' documentata e vale sia per StaticResource sia per
        /// DynamicResource).
        ///
        /// Solo skin 8.1: con Windows 7 la funzione non viene chiamata e ogni
        /// chiave resta esattamente quella di Themes/Overrides.xaml. Best
        /// effort: una variante mancante lascia in piedi la versione Windows 7,
        /// e la diagnostica non puo' far fallire l'avvio.
        /// </summary>
        private static void ShadowWindows81Keys(ResourceDictionary root)
        {
            var pairs = new (string Target, string Source)[]
            {
                ("TaskPreviewFrameVista", "Win81TaskPreviewFrameVista"),
                ("TaskPreviewCloseButton", "Win81TaskPreviewCloseButton"),
                ("TaskButtonFrameHover", "Win81TaskButtonFrameHover"),
                ("TaskButtonFrameActive", "Win81TaskButtonFrameActive"),
                ("TaskButtonFrameNotification", "Win81TaskButtonFrameNotification"),
                ("SuperbarButtonOuterCornerRadius", "SuperbarButtonOuterCornerRadius"),
                ("SuperbarButtonInnerCornerRadius", "SuperbarButtonInnerCornerRadius"),
                ("SuperbarHoverTileCornerRadius", "SuperbarHoverTileCornerRadius"),
                ("SuperbarGlowCornerRadius", "SuperbarGlowCornerRadius"),
            };

            var applied = new List<string>();
            var missing = new List<string>();

            foreach ((string target, string source) in pairs)
            {
                try
                {
                    object? variant = root[source];
                    if (variant == null)
                    {
                        missing.Add(source);
                        continue;
                    }

                    root[target] = variant;
                    applied.Add(target);
                }
                catch
                {
                    missing.Add(source);
                }
            }

            try
            {
                DiagnosticLogger.Write("THEME",
                    "skin 8.1: chrome a spigoli vivi applicato a " +
                    string.Join(", ", applied) +
                    (missing.Count > 0
                        ? "; varianti mancanti, resta la versione Windows 7: " +
                          string.Join(", ", missing)
                        : ""));
            }
            catch
            {
                /* la diagnostica non e' mai un requisito */
            }
        }

        /// <summary>
        /// v1.21.19 - applica subito la skin appena scelta nelle Impostazioni
        /// extra, senza aspettare il riavvio del programma.
        ///
        /// Build() costruisce un dizionario nuovo di zecca esattamente come
        /// all'avvio (tema + Overrides + le stesse riparazioni degli stili), e
        /// Application.Resources e' esattamente la proprieta' che App.xaml.cs
        /// riempie all'avvio: sostituirla a runtime e' la stessa cosa che
        /// partire con l'altra skin. Tutti gli elementi del tema sono
        /// riferimenti DynamicResource (lo stile TaskbarWindow, StartButton,
        /// SuperbarButton, ...), quindi WPF li ri-risolve e la barra si veste
        /// con il tema nuovo senza riavvio.
        ///
        /// Se la costruzione o la sostituzione falliscono, il tema precedente
        /// resta in piedi - l'assegnazione avviene solo dopo un Build()
        /// riuscito - e il chiamante scrive nel log che serve un riavvio.
        /// </summary>
        public static string ReapplyNow()
        {
            try
            {
                var application = System.Windows.Application.Current;
                if (application == null)
                {
                    return "riavvio richiesto (nessuna Application attiva)";
                }

                ResourceDictionary theme = Build();
                application.Resources = theme;

                // v1.21.27 - la sostituzione di Application.Resources butta via
                // anche il dizionario della lingua che App.xaml.cs mergia DOPO il
                // tema all'avvio: senza questa riga i menu contestuali (che
                // leggono le chiavi da li') ricadono sul ripiego inglese scritto
                // nel codice finche' l'utente non cambia di nuovo lingua.
                // Riproduciamo esattamente la sequenza di avvio.
                try
                {
                    Win7Taskbar.Utilities.LocalizationManager.ApplyLanguage(
                        RetroBar.Utilities.Settings.Instance.Language);
                }
                catch
                {
                    // Un dizionario lingua non caricabile non deve impedire il
                    // cambio di skin: i menu useranno il ripiego inglese.
                }

                return "tema applicato subito";
            }
            catch (Exception exception)
            {
                return "riavvio richiesto (" + exception.GetType().Name + ": " +
                       exception.Message + ")";
            }
        }

        /// <summary>
        /// v1.21.7 - theme file for the selected skin ("Tema" in the extra
        /// settings).
        ///
        /// The map exists because the skin is an ordinary configuration entry:
        /// here it is known which file it corresponds to, so adding the
        /// Windows 8.1 skin in the future means putting its name here and
        /// declaring it available in TaskbarThemeIds - without touching the
        /// settings system or the rest of the loading. Today the only usable
        /// id is Windows 7, so this method always returns "Windows7.xaml": no
        /// fake skin and no missing file looked up at runtime.
        /// </summary>
        public static string ThemeFileNameFor(int themeId) => themeId switch
        {
            RetroBar.Utilities.TaskbarThemeIds.Windows81 => "Windows8.1.xaml",
            /* v1.21.42: la skin Aero Basic NON ha un file tema proprio:
             * riusa Windows7.xaml tale e quale (stesso layout, stessi
             * pulsanti, stessa tray) e ci mergia sopra il dizionario
             * AeroBasic.xaml con i soli sfondi opachi (vedi Build). */
            RetroBar.Utilities.TaskbarThemeIds.Windows7AeroBasic => "Windows7.xaml",
            /* Skin "Windows 8 Beta 8148" (id 3): come l'Aero Basic, NON ha
             * un file tema proprio - riusa Windows7.xaml e ci mergia sopra
             * Win8Beta8148.xaml (pulsante Start della beta + vetro piu'
             * marcato, vedi Build). */
            RetroBar.Utilities.TaskbarThemeIds.Windows8Beta8148 => "Windows7.xaml",
            _ => "Windows7.xaml",
        };

        /// <summary>
        /// Path of the selected theme next to the executable.
        /// The theme stays a file on disk (Content, not BAML); the WPF images
        /// come from GraphicalResourceBundle and the eight native slices from
        /// Resources/.
        /// </summary>
        public static string ThemeFilePathFor(int themeId) => Path.Combine(
            AppDomain.CurrentDomain.BaseDirectory, "Themes", ThemeFileNameFor(themeId));

        /// <summary>
        /// Path of the theme in use (the skin chosen in Properties).
        ///
        /// Theme loading is critical for startup: if the configuration were
        /// unreadable the code stays on the default skin - the one that really
        /// exists - instead of failing the startup because of one
        /// configuration entry.
        /// </summary>
        public static string ThemeFilePath
        {
            get
            {
                try
                {
                    return ThemeFilePathFor(
                        RetroBar.Utilities.Settings.Instance.ThemeSelection);
                }
                catch (Exception)
                {
                    return ThemeFilePathFor(RetroBar.Utilities.TaskbarThemeIds.Windows7);
                }
            }
        }

        /// <summary>
        /// Verifica che accanto all'eseguibile ci siano Themes\ e Resources\.
        /// Resources\ deve esistere per le otto slice native Aero; le immagini
        /// WPF del tema arrivano dal bundle Base64, non da quella cartella.
        /// </summary>
        private static void VerifyLayoutOnDisk()
        {
            string baseDir = AppDomain.CurrentDomain.BaseDirectory;
            string themes = Path.Combine(baseDir, "Themes");
            string resources = Path.Combine(baseDir, "Resources");

            var missing = new System.Collections.Generic.List<string>();

            if (!Directory.Exists(themes))
            {
                missing.Add("Themes\\");
            }

            if (!Directory.Exists(resources))
            {
                missing.Add("Resources\\");
            }

            if (missing.Count > 0)
            {
                throw new FileNotFoundException(
                    "Estrazione incompleta: mancano " + string.Join(", ", missing) +
                    " accanto a Win7Taskbar.exe (cartella " + baseDir + "). " +
                    "Estrai di nuovo lo ZIP mantenendo la struttura delle cartelle: " +
                    "il tema e le slice native Aero vengono letti da disco a runtime.",
                    ThemeFilePath);
            }
        }

        private static ResourceDictionary LoadThemeWithBase()
        {
            string path = ThemeFilePath;
            if (!File.Exists(path))
            {
                throw new FileNotFoundException(
                    "Themes/Windows7.xaml non trovato accanto all'eseguibile. " +
                    "Il tema deve essere copiato nell'output; le risorse WPF sono nel bundle e le otto slice native restano in Resources/.",
                    path);
            }

            XDocument document = XDocument.Load(path, LoadOptions.PreserveWhitespace);
            if (document.Root == null)
            {
                throw new InvalidOperationException("Themes/Windows7.xaml non e' un XML valido.");
            }

            // 1.21.5 (IL FIX dell'avvio): gli xmlns clr-namespace SENZA
            // assembly non si risolvono nello XAML caricato a runtime.
            // Li qualifichiamo in memoria PRIMA del parse; il file su disco
            // resta byte-identico all'originale.
            QualifyClrNamespaces(document);

            // Inietta <ResourceDictionary.MergedDictionaries> come PRIMO figlio:
            // il parser deve incontrarlo prima delle chiavi che ne dipendono.
            var mergedElement = new XElement(
                Presentation + "ResourceDictionary.MergedDictionaries",
                new XElement(Presentation + "ResourceDictionary",
                    new XAttribute("Source", BaseUri)));

            document.Root.AddFirst(mergedElement);

            // 1.21.5: valida OGNI x:Static via reflection PRIMA del parse, cosi'
            // un refuso nomina chiave e membro invece di "Line number '10'".
            List<StaticMemberRef> staticRefs = CollectStaticMembers(document);
            ValidateStaticMembers(document, staticRefs);

            // BaseUri resta impostato sul file tema (compatibilità parser);
            // le immagini WPF non usano più UriSource relativi alle PNG.
            //
            // 1.21.5: NIENTE XamlTypeMapper custom. Il costruttore
            // XamlTypeMapper(string[]) vuole NOMI DI ASSEMBLY, non mappe di
            // namespace: passargli "clr-namespace:...;assembly=..." non mappa
            // nulla (era il tentativo 1.21.4, inefficace). Con gli xmlns
            // qualificati qui sopra, la risoluzione standard basta, come per
            // gli altri namespace del tema (controls:, retroconv:, ...).
            var context = new ParserContext
            {
                BaseUri = new Uri(path, UriKind.Absolute)
            };

            // XamlReader.Load accetta ParserContext solo con uno Stream:
            // serializziamo il documento modificato in memoria.
            using var buffer = new MemoryStream();
            document.Save(buffer, SaveOptions.DisableFormatting);
            buffer.Position = 0;

            object loaded;
            try
            {
                loaded = XamlReader.Load(buffer, context);
            }
            catch (Exception ex)
            {
                throw new InvalidOperationException(
                    $"Impossibile analizzare '{path}' ({staticRefs.Count} riferimenti " +
                    $"x:Static gia' verificati via reflection): " +
                    $"{ex.GetType().Name}: {ex.Message}", ex);
            }

            if (loaded is not ResourceDictionary dictionary)
            {
                throw new InvalidOperationException(
                    "Themes/Windows7.xaml non contiene un ResourceDictionary.");
            }

            // 1.21.5: nessuna immagine puo' restare null (darebbe una barra
            // invisibile/rotta senza alcun errore). Meglio un messaggio chiaro.
            VerifyLoadedImages(dictionary, staticRefs);

            return dictionary;
        }

        /// <summary>
        /// Rende assembly-qualified ogni xmlns clr-namespace senza assembly.
        ///
        /// Lo XAML caricato a runtime con XamlReader.Load NON risolve
        /// xmlns="clr-namespace:Foo" senza ";assembly=...": il resolver cerca
        /// il tipo solo negli assembly nominati, e x:Static fallisce dentro
        /// StaticExtension.ProvideValue. Solo in memoria: il disco resta intatto.
        /// </summary>
        private static void QualifyClrNamespaces(XDocument document)
        {
            if (document.Root == null)
            {
                return;
            }

            string assemblyName =
                typeof(ThemeLoader).Assembly.GetName().Name ?? "Win7Taskbar";

            foreach (XAttribute attribute in document.Root.Attributes())
            {
                if (!attribute.IsNamespaceDeclaration)
                {
                    continue;
                }

                string value = attribute.Value;
                if (!value.StartsWith("clr-namespace:", StringComparison.Ordinal))
                {
                    continue;
                }

                if (value.IndexOf(';') >= 0)
                {
                    continue;  // gia' qualificato (mscorlib, RetroBar, ...)
                }

                attribute.Value = value + ";assembly=" + assemblyName;
                StartupGuard.Note(
                    "tema: xmlns '" + value + "' qualificato con '" +
                    attribute.Value + "'");
            }
        }

        /// <summary>Riferimento x:Static trovato nel tema (per la validazione).</summary>
        private sealed class StaticMemberRef
        {
            public string Key = "";
            public string Prefix = "";
            public string TypeName = "";
            public string MemberName = "";
            public string Raw = "";
        }

        /// <summary>
        /// Raccoglie gli elementi &lt;x:Static Member="prefisso:Tipo.membro"&gt;
        /// del tema. Gli x:Static INLINE negli attributi (es. dentro un Binding)
        /// non sono elementi e non vengono raccolti: usano namespace gia'
        /// qualificati e restano responsabilita' del parser XAML.
        /// </summary>
        private static List<StaticMemberRef> CollectStaticMembers(XDocument document)
        {
            var refs = new List<StaticMemberRef>();
            foreach (XElement element in document.Descendants(XamlNamespace + "Static"))
            {
                XAttribute? memberAttr = element.Attribute("Member");
                if (memberAttr == null || string.IsNullOrWhiteSpace(memberAttr.Value))
                {
                    continue;
                }

                string raw = memberAttr.Value.Trim();
                int colon = raw.IndexOf(':');
                int dot = raw.LastIndexOf('.');
                if (colon <= 0 || dot <= colon + 1 || dot >= raw.Length - 1)
                {
                    continue;  // formato non standard: ci pensera' il parser XAML
                }

                refs.Add(new StaticMemberRef
                {
                    Key = element.Attribute(XamlNamespace + "Key")?.Value ?? "",
                    Prefix = raw.Substring(0, colon),
                    TypeName = raw.Substring(colon + 1, dot - colon - 1),
                    MemberName = raw.Substring(dot + 1),
                    Raw = raw,
                });
            }

            return refs;
        }

        /// <summary>
        /// Risolve via reflection OGNI x:Static del tema PRIMA del parse.
        ///
        /// Se un membro manca (refuso nel tema), se la chiave non e' nel bundle
        /// o se il PNG non si decodifica, l'errore nomina l'esatta coppia
        /// chiave/membro invece di un generico XamlParseException.
        /// </summary>
        private static void ValidateStaticMembers(
            XDocument document, List<StaticMemberRef> refs)
        {
            if (refs.Count == 0 || document.Root == null)
            {
                return;
            }

            // prefisso -> namespace (dopo la qualifica con l'assembly).
            var namespaces = new Dictionary<string, string>(StringComparer.Ordinal);
            foreach (XAttribute attribute in document.Root.Attributes())
            {
                if (!attribute.IsNamespaceDeclaration)
                {
                    continue;
                }

                // "xmlns" e' il namespace di default (senza prefisso).
                if (attribute.Name.LocalName != "xmlns")
                {
                    namespaces[attribute.Name.LocalName] = attribute.Value;
                }
            }

            string ownAssembly =
                typeof(ThemeLoader).Assembly.GetName().Name ?? "Win7Taskbar";

            const BindingFlags staticFlags =
                BindingFlags.Public | BindingFlags.Static | BindingFlags.FlattenHierarchy;

            foreach (StaticMemberRef r in refs)
            {
                if (!namespaces.TryGetValue(r.Prefix, out string? ns) || ns == null)
                {
                    throw new InvalidOperationException(
                        $"Tema non valido: il prefisso '{r.Prefix}' di x:Static '{r.Raw}' " +
                        "non e' dichiarato come xmlns nella radice di Themes/Windows7.xaml.");
                }

                const string clrPrefix = "clr-namespace:";
                if (!ns.StartsWith(clrPrefix, StringComparison.Ordinal))
                {
                    continue;  // non e' un namespace CLR: ci pensa il parser XAML
                }

                string rest = ns.Substring(clrPrefix.Length);
                string clrNamespace = rest;
                string assemblyName = ownAssembly;
                int semi = rest.IndexOf(';');
                if (semi >= 0)
                {
                    clrNamespace = rest.Substring(0, semi);
                    const string asmMarker = "assembly=";
                    int asmAt = rest.IndexOf(
                        asmMarker, semi, StringComparison.OrdinalIgnoreCase);
                    if (asmAt >= 0)
                    {
                        assemblyName = rest.Substring(asmAt + asmMarker.Length).Trim();
                    }
                }

                Type? type = Type.GetType(
                    clrNamespace + "." + r.TypeName + ", " + assemblyName, false);
                type ??= typeof(ThemeLoader).Assembly.GetType(
                    clrNamespace + "." + r.TypeName, false);
                if (type == null)
                {
                    throw new InvalidOperationException(
                        $"Tema non valido: x:Static '{r.Raw}' (chiave '{r.Key}') - " +
                        $"tipo '{clrNamespace}.{r.TypeName}' non trovato " +
                        $"nell'assembly '{assemblyName}'.");
                }

                bool memberExists = type.GetProperty(r.MemberName, staticFlags) != null ||
                                    type.GetField(r.MemberName, staticFlags) != null;
                if (!memberExists)
                {
                    throw new InvalidOperationException(
                        $"Tema non valido: x:Static '{r.Raw}' (chiave '{r.Key}') - " +
                        $"il tipo '{type.FullName}' non espone alcun membro " +
                        $"statico pubblico '{r.MemberName}'.");
                }

                // Membri del bundle grafico: la proprieta' esiste, ma la chiave
                // potrebbe mancare o il PNG potrebbe non decodificarsi (in
                // entrambi i casi Get restituisce null invece di lanciare).
                if (type == typeof(GraphicalResourceBundle))
                {
                    if (!GraphicalResourceBundle.Keys.Contains(r.MemberName))
                    {
                        throw new InvalidOperationException(
                            $"Tema non valido: x:Static '{r.Raw}' (chiave '{r.Key}') - " +
                            $"il bundle grafico non contiene la chiave '{r.MemberName}' " +
                            $"({GraphicalResourceBundle.Count} chiavi disponibili).");
                    }

                    if (GraphicalResourceBundle.Get(r.MemberName) == null)
                    {
                        throw new InvalidOperationException(
                            $"Tema non valido: x:Static '{r.Raw}' (chiave '{r.Key}') - " +
                            $"la chiave '{r.MemberName}' esiste nel bundle ma non si " +
                            "decodifica (base64 o PNG corrotti in GraphicalResourceBundle.cs).");
                    }
                }
            }
        }

        /// <summary>
        /// Dopo il parse, ogni chiave prodotta da x:Static deve esistere e non
        /// essere null: un'immagine mancante darebbe una barra invisibile o
        /// rotta senza alcun errore visibile.
        /// </summary>
        private static void VerifyLoadedImages(
            ResourceDictionary dictionary, List<StaticMemberRef> refs)
        {
            foreach (StaticMemberRef r in refs)
            {
                if (string.IsNullOrEmpty(r.Key))
                {
                    continue;
                }

                if (!dictionary.Contains(r.Key))
                {
                    throw new InvalidOperationException(
                        $"Tema non valido: dopo il parse manca la chiave '{r.Key}' " +
                        $"(x:Static '{r.Raw}').");
                }

                if (dictionary[r.Key] == null)
                {
                    throw new InvalidOperationException(
                        $"Tema non valido: la chiave '{r.Key}' (x:Static '{r.Raw}') " +
                        "vale null dopo il parse: l'immagine non e' stata caricata.");
                }
            }
        }
    }
}
