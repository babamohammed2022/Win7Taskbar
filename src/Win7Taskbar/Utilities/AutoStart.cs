// Win7Taskbar - avvio automatico con Windows (reversibile)
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
//
// Perche' esiste questo file
// --------------------------
// La casella "avvio automatico" della scheda Informazioni della finestra
// Proprieta' usa ESATTAMENTE l'implementazione di RetroBar, copiata da:
//
//   RetroBar/PropertiesWindow.xaml.cs  -> LoadAutoStart() e
//                                         CbAutoStart_OnChecked()
//   RetroBar/Utilities/ExePath.cs      -> GetExecutablePath()
//
// (progetto dremin/RetroBar, Apache License 2.0; attribuzione completa in
// docs/CREDITS.txt e docs/THIRD-PARTY-NOTICES.md). Il meccanismo e' quello
// classico e reversibile: lo stato si legge dai NOMI DEI VALORI della chiave
// HKCU\Software\Microsoft\Windows\CurrentVersion\Run; attivare l'opzione
// scrive il percorso dell'eseguibile (fra virgolette, perche' i valori Run
// sono righe di comando) nel valore "Win7Taskbar", disattivarla elimina il
// valore. Windows non viene modificato in nessun altro modo.
//
// Rispetto all'originale cambiano solo: il nome del valore ("Win7Taskbar"
// invece di "RetroBar"), il recapito degli errori (DiagnosticLogger invece
// di ShellLogger) e il punto in cui la scelta viene confermata: RetroBar
// applica al click della casella, qui al OK/Applica della finestra
// Proprieta', come tutte le altre impostazioni del dialogo.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32;

namespace Win7Taskbar.Utilities
{
    /// <summary>
    /// Avvio automatico del programma all'avvio di Windows, reversibile.
    /// Port completo dell'implementazione di RetroBar (vedi intestazione).
    /// </summary>
    internal static class AutoStart
    {
        /// <summary>Chiave Run dell'utente corrente, la stessa di RetroBar.</summary>
        private const string RunKeyPath =
            @"Software\Microsoft\Windows\CurrentVersion\Run";

        /// <summary>Nome del valore: l'equivalente del "RetroBar" originale.</summary>
        private const string ValueName = "Win7Taskbar";

        // -----------------------------------------------------------------
        //  ExePath - port di RetroBar/Utilities/ExePath.cs
        // -----------------------------------------------------------------

        [DllImport("kernel32.dll")]
        private static extern uint GetModuleFileName(
            IntPtr hModule, StringBuilder lpFilename, int nSize);

        private static readonly int MAX_PATH = 260;

        /// <summary>Percorso dell'eseguibile in esecuzione (GetModuleFileName
        /// con modulo NULL), identico a RetroBar ExePath.GetExecutablePath().
        /// </summary>
        internal static string GetExecutablePath()
        {
            var sb = new StringBuilder(MAX_PATH);
            GetModuleFileName(IntPtr.Zero, sb, MAX_PATH);
            return sb.ToString();
        }

        // -----------------------------------------------------------------
        //  Stato - port di RetroBar PropertiesWindow.LoadAutoStart()
        // -----------------------------------------------------------------

        /// <summary>
        /// Legge lo stato corrente dell'avvio automatico dal registro:
        /// l'opzione e' attiva se la chiave Run contiene il valore del
        /// programma. Ogni errore viene registrato e l'opzione risulta
        /// disattivata, come nell'originale.
        /// </summary>
        public static bool IsEnabled()
        {
            try
            {
                RegistryKey? rKey = Registry.CurrentUser.OpenSubKey(RunKeyPath, false);
                List<string>? rKeyValueNames = rKey?.GetValueNames().ToList();

                if (rKeyValueNames != null)
                {
                    if (rKeyValueNames.Contains(ValueName))
                    {
                        return true;
                    }
                    else
                    {
                        return false;
                    }
                }

                return false;
            }
            catch (Exception e)
            {
                /* RetroBar: "Unable to load autorun setting from registry". */
                DiagnosticLogger.WriteException("AUTOSTART", e,
                    "unable to load autorun setting from registry");
                return false;
            }
        }

        // -----------------------------------------------------------------
        //  Scrittura - port di RetroBar PropertiesWindow.CbAutoStart_OnChecked()
        // -----------------------------------------------------------------

        /// <summary>
        /// Attiva o disattiva l'avvio automatico (reversibile): con la
        /// spunta scrive nella chiave Run il percorso dell'eseguibile come
        /// riga di comando (fra virgolette, per i nomi utente con spazi),
        /// senza spunta elimina il valore. Ogni errore viene registrato,
        /// come nell'originale.
        /// </summary>
        public static void SetEnabled(bool enable)
        {
            try
            {
                RegistryKey? rKey = Registry.CurrentUser.CreateSubKey(RunKeyPath);

                if (!enable)
                {
                    /* RetroBar usa DeleteValue(name): qui l'opzione si
                     * conferma col OK/Applica del dialogo (non al click della
                     * casella), quindi la richiesta di rimozione puo' arrivare
                     * anche quando il valore non c'e' gia' piu'; il secondo
                     * parametro rende la rimozione idempotente invece di
                     * generare un'eccezione. La reversibilita' non cambia. */
                    rKey?.DeleteValue(ValueName, throwOnMissingValue: false);
                }
                else
                {
                    // Registry Run values are command lines; quote the executable path to handle spaces in usernames/paths.
                    rKey?.SetValue(ValueName, $"\"{GetExecutablePath()}\"");
                }
            }
            catch (Exception exception)
            {
                /* RetroBar: "Unable to update registry autorun setting". */
                DiagnosticLogger.WriteException("AUTOSTART", exception,
                    "unable to update registry autorun setting");
            }
        }
    }
}
