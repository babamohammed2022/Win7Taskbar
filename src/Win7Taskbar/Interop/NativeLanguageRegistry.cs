// Win7Taskbar - elenco delle lingue, preso dal core nativo
// Copyright (c) 2026 Win7Taskbar contributors
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// ---------------------------------------------------------------------------
// PERCHE' QUESTO FILE ESISTE
//
// Il livello gestito e quello nativo si scambiano le lingue come INDICI
// (selettore di Proprieta', flyout di rete, jump list). Quando la lista
// gestita e quella nativa erano due elenchi scritti a mano, bastava
// dimenticarne una voce per far arrivare l'indice sbagliato: l'arabo, per
// esempio, non c'era qui, "IndexOf" rispondeva -1 e il nativo apriva le
// Proprieta' in ITALIANO su un sistema arabo.
//
// Ora l'elenco e' uno solo, quello del core (Strings.cpp), letto all'avvio
// tramite W7T_GetLanguageCount / W7T_GetLanguageCode. La copia di riserva
// qui sotto serve solo se la DLL non risponde: stesso ordine, stessa
// lunghezza, cosi' nessun indice puo' slittare.
// ---------------------------------------------------------------------------

using System;

namespace Win7Taskbar.Interop
{
    internal static class NativeLanguageRegistry
    {
        /// <summary>Ripiego usato solo se il core non risponde: stesso ordine
        /// di Strings.cpp (0=it ... 10=ar).</summary>
        private static readonly string[] FallbackCodes =
            { "it", "en", "es", "fr", "de", "pt", "pl", "ru", "ja", "zh", "ar" };

        private static readonly string[] FallbackNames =
            { "Italiano", "English", "Español", "Français", "Deutsch",
              "Português (Brasil)", "Polski", "Русский", "日本語", "中文 (简体)",
              "العربية" };

        private static readonly object Gate = new object();
        private static string[]? _codes;
        private static string[]? _names;

        private static void EnsureLoaded()
        {
            if (_codes != null)
            {
                return;
            }

            lock (Gate)
            {
                if (_codes != null)
                {
                    return;
                }

                string[] codes = FallbackCodes;
                string[] names = FallbackNames;

                try
                {
                    int count = NativeLocalization.LanguageCount();
                    if (count > 0)
                    {
                        var loadedCodes = new string[count];
                        var loadedNames = new string[count];
                        bool complete = true;
                        for (int i = 0; i < count; i++)
                        {
                            string? code = NativeLocalization.CodeAt(i);
                            string? name = NativeLocalization.NameAt(i);
                            if (string.IsNullOrEmpty(code))
                            {
                                complete = false;
                                break;
                            }
                            loadedCodes[i] = code!;
                            loadedNames[i] = string.IsNullOrEmpty(name) ? code! : name!;
                        }

                        if (complete)
                        {
                            codes = loadedCodes;
                            names = loadedNames;
                        }
                    }
                }
                catch
                {
                    // La DLL non risponde: si usa la copia di riserva.
                }

                _names = names;
                _codes = codes;
            }
        }

        /// <summary>Codici a due lettere, nell'ordine del core nativo.</summary>
        public static string[] Codes
        {
            get { EnsureLoaded(); return _codes!; }
        }

        /// <summary>Nomi nativi (le etichette del selettore di Proprieta').</summary>
        public static string[] Names
        {
            get { EnsureLoaded(); return _names!; }
        }

        /// <summary>Indice di un codice, oppure -1 se non e' supportato.</summary>
        public static int IndexOf(string? code)
        {
            if (string.IsNullOrEmpty(code))
            {
                return -1;
            }

            string[] codes = Codes;
            return Array.IndexOf(codes, code!.ToLowerInvariant());
        }

        /// <summary>
        /// Codice della lingua con quell'indice, oppure null se l'indice non
        /// esiste: il chiamante non deve mai scrivere un indice in una lingua
        /// diversa da quella che intendeva.
        /// </summary>
        public static string? CodeAt(int index)
        {
            string[] codes = Codes;
            return index >= 0 && index < codes.Length ? codes[index] : null;
        }
    }
}
