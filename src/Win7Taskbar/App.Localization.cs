// Win7Taskbar - applicazione della lingua all'avvio e a ogni cambio
// Copyright (c) 2026 Win7Taskbar contributors
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

using RetroBar.Utilities;
using Win7Taskbar.Interop;

namespace Win7Taskbar
{
    public partial class App
    {
        static App()
        {
            /* Primo avvio: la lingua la dichiara WINDOWS al core nativo
             * (GetUserDefaultUILanguage), non la cultura di .NET, che puo'
             * essere impostata da altro. Si interviene SOLO quando non esiste
             * ancora una configurazione salvata: dopo, la scelta dell'utente
             * (o quella rilevata, che lui vede in Proprieta') vince sempre. */
            try
            {
                if (!Settings.HasPersistedConfig)
                {
                    string? systemLanguage = NativeLocalization.SystemLanguageCode();
                    if (!string.IsNullOrEmpty(systemLanguage))
                    {
                        Settings.Instance.Language = systemLanguage;
                    }
                }
            }
            catch
            {
                // Nessun blocco: senza core si resta sulla lingua rilevata
                // dal managed, che e' comunque quella di Windows.
            }

            Settings.Instance.PropertyChanged += (_, e) =>
            {
                if (e.PropertyName == nameof(Settings.Language))
                {
                    /* Una sola chiamata: cambia le stringhe brevi, la finestra
                     * Proprieta', la ricerca, i menu e i flyout batteria/rete. */
                    NativeLocalization.Apply(Settings.Instance.Language);
                }
            };
            NativeLocalization.Apply(Settings.Instance.Language);
        }
    }
}
