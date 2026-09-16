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
using System.IO;
using System.Windows;
using System.Windows.Markup;
using System.Xml.Linq;

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
    /// </summary>
    public static class ThemeLoader
    {
        private const string BaseUri =
            "pack://application:,,,/RetroBar;component/Themes/Base.xaml";

        private const string OverridesUri =
            "pack://application:,,,/Win7Taskbar;component/Themes/Overrides.xaml";

        private static readonly XNamespace Presentation =
            "http://schemas.microsoft.com/winfx/2006/xaml/presentation";

        /// <summary>
        /// Restituisce il dizionario radice pronto per Application.Resources.
        /// </summary>
        public static ResourceDictionary Build()
        {
            // Controllo preliminare: il tema e le PNG stanno su disco accanto
            // all'eseguibile, quindi l'errore di gran lunga piu' frequente
            // e' uno ZIP estratto senza mantenere le sottocartelle. Meglio un
            // messaggio che dice cosa manca di una XamlParseException criptica.
            VerifyLayoutOnDisk();

            var root = new ResourceDictionary();
            root.MergedDictionaries.Add(LoadThemeWithBase());
            root.MergedDictionaries.Add(new ResourceDictionary
            {
                Source = new Uri(OverridesUri, UriKind.Absolute)
            });

            // v2.21: SuperbarButton eredita da TaskButton, ma il BasedOn
            // non puo' stare nel BAML (StaticResource irrisolvibile al
            // parse del dizionario isolato): lo colleghiamo qui, prima
            // che qualunque finestra usi lo stile.
            try
            {
                // l'indicizzatore cerca anche nei dizionari mergiati
                object? sbObj = root["SuperbarButton"];
                object? tbObj = root["TaskButton"];
                if (sbObj is System.Windows.Style superbar &&
                    tbObj is System.Windows.Style taskButton)
                {
                    superbar.BasedOn = taskButton;
                }
            }
            catch
            {
                // senza eredita' lo stile resta valido (template proprio)
            }

            return root;
        }

        /// <summary>
        /// Percorso di Themes/Windows7.xaml accanto all'eseguibile.
        /// Il tema resta un file su disco perche' referenzia le PNG con URI
        /// relativi ("../Resources/..."), esattamente come nel repository.
        /// </summary>
        public static string ThemeFilePath => Path.Combine(
            AppDomain.CurrentDomain.BaseDirectory, "Themes", "Windows7.xaml");

        /// <summary>
        /// Verifica che accanto all'eseguibile ci siano Themes\ e Resources\,
        /// e che dentro Resources\ ci siano davvero le PNG del tema. Senza di
        /// esse il tema carica ma i pulsanti restano vuoti.
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
            else if (Directory.GetFiles(resources, "*.png", SearchOption.AllDirectories).Length == 0)
            {
                missing.Add("Resources\\*.png");
            }

            if (missing.Count > 0)
            {
                throw new FileNotFoundException(
                    "Estrazione incompleta: mancano " + string.Join(", ", missing) +
                    " accanto a Win7Taskbar.exe (cartella " + baseDir + "). " +
                    "Estrai di nuovo lo ZIP mantenendo la struttura delle cartelle: " +
                    "il tema e le immagini vengono letti da disco a runtime.",
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
                    "Il tema e le PNG in Resources/ devono essere copiati nell'output.",
                    path);
            }

            XDocument document = XDocument.Load(path, LoadOptions.PreserveWhitespace);
            if (document.Root == null)
            {
                throw new InvalidOperationException("Themes/Windows7.xaml non e' un XML valido.");
            }

            // Inietta <ResourceDictionary.MergedDictionaries> come PRIMO figlio:
            // il parser deve incontrarlo prima delle chiavi che ne dipendono.
            var mergedElement = new XElement(
                Presentation + "ResourceDictionary.MergedDictionaries",
                new XElement(Presentation + "ResourceDictionary",
                    new XAttribute("Source", BaseUri)));

            document.Root.AddFirst(mergedElement);

            // BaseUri fa risolvere gli UriSource relativi delle PNG
            // ("../Resources/...") rispetto alla cartella Themes.
            var context = new ParserContext
            {
                BaseUri = new Uri(path, UriKind.Absolute)
            };

            // XamlReader.Load accetta ParserContext solo con uno Stream:
            // serializziamo il documento modificato in memoria.
            using var buffer = new MemoryStream();
            document.Save(buffer, SaveOptions.DisableFormatting);
            buffer.Position = 0;

            object loaded = XamlReader.Load(buffer, context);

            if (loaded is not ResourceDictionary dictionary)
            {
                throw new InvalidOperationException(
                    "Themes/Windows7.xaml non contiene un ResourceDictionary.");
            }

            return dictionary;
        }
    }
}
