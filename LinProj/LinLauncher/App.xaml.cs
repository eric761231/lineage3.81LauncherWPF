using System;
using System.Windows;

namespace LinLauncher
{
    public partial class App : Application
    {
        protected override void OnStartup(StartupEventArgs e)
        {
            AppDomain.CurrentDomain.UnhandledException += (s, ex) => {
                MessageBox.Show("Fatal Error: " + ex.ExceptionObject.ToString(), "Launcher Crash");
            };
            base.OnStartup(e);
        }

        private void Application_DispatcherUnhandledException(object sender, System.Windows.Threading.DispatcherUnhandledExceptionEventArgs e)
        {
            MessageBox.Show("UI Error: " + e.Exception.ToString(), "Launcher UI Crash");
            e.Handled = true;
        }
    }
}
