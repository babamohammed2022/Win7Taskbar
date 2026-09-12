using System;
using System.Runtime.InteropServices;

namespace Win7Taskbar.Interop
{
    internal static class NativeLocalization
    {
        private const string Dll = "Win7TaskbarCore.dll";

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Ansi)]
        private static extern void W7T_SetLanguage(string twoLetterCode);

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
    }
}
