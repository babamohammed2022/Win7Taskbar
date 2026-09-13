using System;
using System.Runtime.InteropServices;

namespace Win7Taskbar.Interop
{
    internal static class NativeLocalization
    {
        private const string Dll = "Win7TaskbarCore.dll";

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Ansi)]
        private static extern void W7T_SetLanguage(string twoLetterCode);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        private static extern int W7T_GetLanguageCount();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Ansi)]
        private static extern IntPtr W7T_GetLanguageCode(int index);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Unicode)]
        private static extern IntPtr W7T_GetLanguageName(int index);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        private static extern int W7T_DetectSystemLanguageIndex();

        /// <summary>
        /// Lingua di TUTTO il nativo (stringhe, Proprieta', ricerca, menu,
        /// flyout batteria e rete) in una sola chiamata: non esistono piu'
        /// due percorsi paralleli per cambiare lingua.
        /// </summary>
        public static void Apply(string languageCode)
        {
            try
            {
                W7T_SetLanguage(languageCode);
            }
            catch (DllNotFoundException) { }
            catch (EntryPointNotFoundException) { }
            catch (BadImageFormatException) { }
        }

        /// <summary>
        /// Codice a due lettere della lingua dell'interfaccia di Windows
        /// secondo il core nativo (gia' filtrato sulle lingue supportate),
        /// oppure null se il core non risponde: in quel caso il chiamante
        /// usa il rilevamento gestito di <c>Settings.DetectSystemLanguage()</c>.
        /// </summary>
        public static string? SystemLanguageCode()
        {
            try
            {
                int index = W7T_DetectSystemLanguageIndex();
                string? code = CodeAt(index);
                return string.IsNullOrEmpty(code) ? null : code;
            }
            catch (DllNotFoundException) { return null; }
            catch (EntryPointNotFoundException) { return null; }
            catch (BadImageFormatException) { return null; }
        }

        /// <summary>Numero di lingue del core, 0 se il core non risponde.</summary>
        public static int LanguageCount()
        {
            try { return W7T_GetLanguageCount(); }
            catch (DllNotFoundException) { return 0; }
            catch (EntryPointNotFoundException) { return 0; }
            catch (BadImageFormatException) { return 0; }
        }

        /// <summary>Codice a due lettere della lingua in posizione
        /// <paramref name="index"/>, null se fuori elenco o non disponibile.</summary>
        public static string? CodeAt(int index)
        {
            try
            {
                IntPtr ptr = W7T_GetLanguageCode(index);
                return ptr == IntPtr.Zero ? null : Marshal.PtrToStringAnsi(ptr);
            }
            catch (DllNotFoundException) { return null; }
            catch (EntryPointNotFoundException) { return null; }
            catch (BadImageFormatException) { return null; }
        }

        /// <summary>Nome nativo della lingua (come compare nel selettore di
        /// Proprieta'), null se non disponibile.</summary>
        public static string? NameAt(int index)
        {
            try
            {
                IntPtr ptr = W7T_GetLanguageName(index);
                return ptr == IntPtr.Zero ? null : Marshal.PtrToStringUni(ptr);
            }
            catch (DllNotFoundException) { return null; }
            catch (EntryPointNotFoundException) { return null; }
            catch (BadImageFormatException) { return null; }
        }
    }
}
