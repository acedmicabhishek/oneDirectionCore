using System.Configuration;
using System.Data;
using System.Threading;
using System.Windows;

namespace OneDirectionCore;

public partial class App : System.Windows.Application
{
    // Two copies would fight over the audio routing, so a second launch just brings up the first.
    private const string InstanceName = "OneDirectionCore.SingleInstance";
    private const string ShowSignalName = "OneDirectionCore.ShowWindow";
    private Mutex? _instanceMutex;
    private EventWaitHandle? _showSignal;

    protected override void OnStartup(StartupEventArgs e)
    {
        _instanceMutex = new Mutex(true, InstanceName, out bool first);
        if (!first)
        {
            try
            {
                using EventWaitHandle signal = EventWaitHandle.OpenExisting(ShowSignalName);
                signal.Set();
            }
            catch { }
            Environment.Exit(0);
        }

        _showSignal = new EventWaitHandle(false, EventResetMode.AutoReset, ShowSignalName);
        var listener = new Thread(() =>
        {
            while (_showSignal.WaitOne())
            {
                Dispatcher.BeginInvoke(new Action(() => (MainWindow as MainWindow)?.BringToFront()));
            }
        })
        { IsBackground = true, Name = "ODC Show Signal" };
        listener.Start();

        base.OnStartup(e);
    }
}
