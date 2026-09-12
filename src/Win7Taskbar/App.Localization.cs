using RetroBar.Utilities;
using Win7Taskbar.Interop;

namespace Win7Taskbar
{
    public partial class App
    {
        static App()
        {
            Settings.Instance.PropertyChanged += (_, e) =>
            {
                if (e.PropertyName == nameof(Settings.Language))
                    NativeLocalization.Apply(Settings.Instance.Language);
            };
            NativeLocalization.Apply(Settings.Instance.Language);
        }
    }
}
