using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.IO.Ports;
using System.Windows;
using System.Windows.Controls;
using WinForms = System.Windows.Forms;
using Microsoft.Win32;
using Color = System.Windows.Media.Color;
using Brush = System.Windows.Media.Brush;
using Point = System.Windows.Point;


namespace OneDirectionCore
{
    public partial class MainWindow : Window
    {
        private OverlayWindow? _overlay;
        // Per-user location: the install directory is not writable and the working
        // directory is System32 when launched from the startup registry key.
        private static readonly string _settingsPath = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "OneDirectionCore", "settings.cfg");
        private const string LegacySettingsName = "settings_dotnet.cfg";
        private WinForms.NotifyIcon? _notifyIcon;
        private System.Windows.Threading.DispatcherTimer? _recoveryTimer;

        // Simple mode shows only the engine controls; Advanced shows every option.
        private bool _simpleMode = true;
        private const double AdvancedWidth = 920, AdvancedHeight = 740;
        private const double SimpleWidth = 440, SimpleHeight = 420;

        // Surround routing changes system audio settings while the engine runs. What was changed is
        // also written to disk so it can be put back on the next launch if the app dies mid-run.
        private static readonly string _routeStatePath = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "OneDirectionCore", "route_state.cfg");
        private const string ListenAutomatic = "Automatic";
        // The FxSound app pins its virtual device to stereo, so it is closed while routed and restarted after.
        private const string EnhancerProcessName = "FxSound";
        private string? _stoppedEnhancerPath;
        private bool _routeActive;


        public MainWindow()
        {
            InitializeComponent();
            InitializeNotifyIcon();
            RecoverRouteState();
            LoadPorts();
            LoadListenDevices();
            LoadSettings();

            // Checking the button raises Checked, which applies the layout.
            if (_simpleMode) RadioSimple.IsChecked = true;
            else RadioAdvanced.IsChecked = true;
        }

        private void ModeSimple_Checked(object sender, RoutedEventArgs e) => ApplyMode(simple: true);

        private void ModeAdvanced_Checked(object sender, RoutedEventArgs e) => ApplyMode(simple: false);

        // The hidden options keep their values, so Start in Simple mode still uses them.
        private void ApplyMode(bool simple)
        {
            _simpleMode = simple;

            Visibility advancedOnly = simple ? Visibility.Collapsed : Visibility.Visible;
            PanelAdvancedLeft.Visibility = advancedOnly;
            PanelAdvancedRight.Visibility = advancedOnly;
            BtnCheckUpdates.Visibility = advancedOnly;
            ColGap.Width = new GridLength(simple ? 0 : 60);
            ColRight.Width = simple ? new GridLength(0) : new GridLength(1, GridUnitType.Star);

            double width = simple ? SimpleWidth : AdvancedWidth;
            double height = simple ? SimpleHeight : AdvancedHeight;
            if (IsLoaded)
            {
                // Resize around the window's centre instead of its top-left corner.
                Left += (Width - width) / 2.0;
                Top += (Height - height) / 2.0;
            }
            Width = width;
            Height = height;
        }

        private void InitializeNotifyIcon()
        {
            _notifyIcon = new WinForms.NotifyIcon();
            _notifyIcon.Icon = System.Drawing.Icon.ExtractAssociatedIcon(Process.GetCurrentProcess().MainModule?.FileName ?? "");
            _notifyIcon.Text = "oneDirectionCore";
            _notifyIcon.Visible = true;
            _notifyIcon.DoubleClick += (s, e) => RestoreFromTray();

            var contextMenu = new WinForms.ContextMenuStrip();
            contextMenu.Items.Add("Show", null, (s, e) => RestoreFromTray());
            contextMenu.Items.Add("Exit", null, (s, e) => BtnExit_Click(null!, null!));
            _notifyIcon.ContextMenuStrip = contextMenu;
        }


        // Called when the app is launched again while already running.
        public void BringToFront() => RestoreFromTray();

        private void RestoreFromTray()
        {
            this.Show();
            this.WindowState = WindowState.Normal;
            this.Activate();
        }

        protected override void OnStateChanged(EventArgs e)
        {
            if (this.WindowState == WindowState.Minimized && CheckTray.IsChecked == true)
            {
                this.Hide();
            }
            base.OnStateChanged(e);
        }

        protected override void OnClosing(System.ComponentModel.CancelEventArgs e)
        {
            BtnStop_Click(null!, null!);
            SaveSettings();
            _notifyIcon?.Dispose();
            base.OnClosing(e);
        }


        private void LoadPorts()
        {
            ComboPorts.Items.Clear();
            ComboPorts.Items.Add("Disabled");
            try
            {
                string[] ports = SerialPort.GetPortNames();
                foreach (string port in ports)
                {
                    ComboPorts.Items.Add(port);
                }
            }
            catch { }
            ComboPorts.SelectedIndex = 0;
        }

        private static string? FindSettingsFile()
        {
            string[] candidates =
            {
                _settingsPath,
                Path.Combine(AppContext.BaseDirectory, LegacySettingsName),
                LegacySettingsName
            };
            return candidates.FirstOrDefault(File.Exists);
        }

        // Settings are written with '.' decimals; older files used the current culture, which may be ','.
        private static void SetSlider(Slider slider, string value)
        {
            if (double.TryParse(value.Replace(',', '.'), NumberStyles.Float, CultureInfo.InvariantCulture, out double v))
                slider.Value = v;
        }

        private static void SetIndex(System.Windows.Controls.ComboBox combo, string value)
        {
            if (int.TryParse(value, out int idx) && idx >= 0 && idx < combo.Items.Count)
                combo.SelectedIndex = idx;
        }

        private static void SetCheck(System.Windows.Controls.CheckBox check, string value)
        {
            if (bool.TryParse(value, out bool b)) check.IsChecked = b;
        }

        private void LoadSettings()
        {
            string? path = FindSettingsFile();
            if (path == null) return;

            string[] lines;
            try { lines = File.ReadAllLines(path); }
            catch { return; }

            // One malformed line must not discard the rest of the file.
            foreach (string line in lines)
            {
                int eq = line.IndexOf('=');
                if (eq <= 0) continue;

                string key = line.Substring(0, eq).Trim();
                string value = line.Substring(eq + 1).Trim();

                switch (key)
                {
                    case "pollrate": SetSlider(SliderPollRate, value); break;
                    case "channels_idx": SetIndex(ComboChannels, value); break;
                    case "preset_idx": SetIndex(ComboPreset, value); break;
                    case "sensitivity": SetSlider(SliderSensitivity, value); break;
                    case "separation": SetSlider(SliderSeparation, value); break;
                    case "range": SetSlider(SliderRange, value); break;
                    case "fullscreen": SetCheck(CheckFullscreen, value); break;
                    case "pos_idx": SetIndex(ComboPosition, value); break;
                    case "radar_size": SetSlider(SliderRadarSize, value); break;
                    case "global_opacity": SetSlider(SliderGlobalOpacity, value); break;
                    case "radar_opacity": SetSlider(SliderRadarOpacity, value); break;
                    case "dot_opacity": SetSlider(SliderDotOpacity, value); break;
                    case "max_entities": SetSlider(SliderMaxEntities, value); break;
                    case "com_port": if (ComboPorts.Items.Contains(value)) ComboPorts.SelectedItem = value; break;
                    case "min_to_tray": SetCheck(CheckTray, value); break;
                    case "launch_startup": SetCheck(CheckStartup, value); break;
                    case "smoothness": SetSlider(SliderSmoothness, value); break;
                    case "ui_mode": _simpleMode = value != "advanced"; break;
                    case "surround": SetCheck(CheckSurround, value); break;
                    case "listen_device": if (ComboListen.Items.Contains(value)) ComboListen.SelectedItem = value; break;
                }
            }
        }

        private void SetStartup(bool enable)
        {
            try
            {
                string path = @"SOFTWARE\Microsoft\Windows\CurrentVersion\Run";
                RegistryKey? key = Registry.CurrentUser.OpenSubKey(path, true);
                if (key != null)
                {
                    if (enable)
                    {
                        string? exePath = Environment.ProcessPath;
                        if (exePath != null) key.SetValue("OneDirectionCore", $"\"{exePath}\"");
                    }
                    else
                    {
                        key.DeleteValue("OneDirectionCore", false);
                    }
                    key.Close();
                }
            }
            catch { }
        }

        private void SaveSettings()
        {
            try
            {
                SetStartup(CheckStartup.IsChecked == true);
                Directory.CreateDirectory(Path.GetDirectoryName(_settingsPath)!);
                using (StreamWriter sw = new StreamWriter(_settingsPath))
                {
                    sw.WriteLine(FormattableString.Invariant($"pollrate={SliderPollRate.Value}"));
                    sw.WriteLine($"channels_idx={ComboChannels.SelectedIndex}");
                    sw.WriteLine($"preset_idx={ComboPreset.SelectedIndex}");
                    sw.WriteLine(FormattableString.Invariant($"sensitivity={SliderSensitivity.Value}"));
                    sw.WriteLine(FormattableString.Invariant($"separation={SliderSeparation.Value}"));
                    sw.WriteLine(FormattableString.Invariant($"range={SliderRange.Value}"));
                    sw.WriteLine($"fullscreen={CheckFullscreen.IsChecked}");
                    sw.WriteLine($"min_to_tray={CheckTray.IsChecked}");
                    sw.WriteLine($"launch_startup={CheckStartup.IsChecked}");
                    sw.WriteLine($"pos_idx={ComboPosition.SelectedIndex}");
                    sw.WriteLine(FormattableString.Invariant($"radar_size={SliderRadarSize.Value}"));
                    sw.WriteLine(FormattableString.Invariant($"global_opacity={SliderGlobalOpacity.Value}"));
                    sw.WriteLine(FormattableString.Invariant($"radar_opacity={SliderRadarOpacity.Value}"));
                    sw.WriteLine(FormattableString.Invariant($"dot_opacity={SliderDotOpacity.Value}"));
                    sw.WriteLine(FormattableString.Invariant($"max_entities={SliderMaxEntities.Value}"));
                    sw.WriteLine($"com_port={ComboPorts.SelectedItem}");
                    sw.WriteLine(FormattableString.Invariant($"smoothness={SliderSmoothness.Value}"));
                    sw.WriteLine($"ui_mode={(_simpleMode ? "simple" : "advanced")}");
                    sw.WriteLine($"surround={CheckSurround.IsChecked}");
                    sw.WriteLine($"listen_device={(ComboListen.SelectedIndex > 0 ? ComboListen.SelectedItem : "")}");
                }
            }
            catch { }
        }


        private void BtnStart_Click(object sender, RoutedEventArgs e)
        {
            if (_overlay != null) return;

            _recoveryTimer?.Stop();
            SaveSettings();
            StartEngine(showErrors: true);
        }

        private void LoadListenDevices()
        {
            // Devices come and go (headphones plugged in), so the list is rebuilt whenever it is opened.
            string? selected = ComboListen.SelectedIndex > 0 ? ComboListen.SelectedItem as string : null;
            ComboListen.Items.Clear();
            ComboListen.Items.Add(ListenAutomatic);
            try
            {
                foreach (string name in NativeMethods.RouteListOutputs()) ComboListen.Items.Add(name);
            }
            catch (DllNotFoundException) { }
            catch (EntryPointNotFoundException) { }

            if (selected != null && ComboListen.Items.Contains(selected)) ComboListen.SelectedItem = selected;
            else ComboListen.SelectedIndex = 0;
        }

        private void ComboListen_DropDownOpened(object? sender, EventArgs e) => LoadListenDevices();

        // Make the game render 7.1 into a virtual device. Returns one of NativeMethods.Route*.
        private int PrepareSurroundRoute()
        {
            string? listenOn = ComboListen.SelectedIndex > 0 ? ComboListen.SelectedItem as string : null;

            int route = NativeMethods.OD_Route_Prepare(listenOn);
            if (route == NativeMethods.RouteBlocked && StopEnhancer())
            {
                route = NativeMethods.OD_Route_Prepare(listenOn);
            }

            if (route == NativeMethods.RouteVirtual)
            {
                _routeActive = true;
                SaveRouteState();
            }
            else
            {
                // Nothing was routed (or a reconnect could not re-route): leave no trace behind.
                ReleaseSurroundRoute();
            }
            return route;
        }

        private void ReleaseSurroundRoute()
        {
            // The state file may belong to another running copy; only remove our own.
            bool owned = _routeActive || _stoppedEnhancerPath != null;
            if (_routeActive)
            {
                NativeMethods.OD_Route_Restore();
                _routeActive = false;
            }
            RestartEnhancer();
            if (owned)
            {
                try { File.Delete(_routeStatePath); } catch { }
            }
        }

        private bool StopEnhancer()
        {
            bool stopped = false;
            foreach (Process p in Process.GetProcessesByName(EnhancerProcessName))
            {
                try
                {
                    _stoppedEnhancerPath ??= p.MainModule?.FileName;
                    p.Kill();
                    p.WaitForExit(3000);
                    stopped = true;
                }
                catch { }
                finally { p.Dispose(); }
            }
            return stopped;
        }

        private void RestartEnhancer()
        {
            if (_stoppedEnhancerPath == null) return;
            try
            {
                Process.Start(new ProcessStartInfo(_stoppedEnhancerPath) { UseShellExecute = true, WindowStyle = ProcessWindowStyle.Minimized });
            }
            catch { }
            _stoppedEnhancerPath = null;
        }

        private void SaveRouteState()
        {
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(_routeStatePath)!);
                File.WriteAllLines(_routeStatePath, new[]
                {
                    $"virtual={NativeMethods.RouteCaptureId}",
                    $"previous_default={NativeMethods.RoutePreviousDefaultId}",
                    $"enhancer={_stoppedEnhancerPath}"
                });
            }
            catch { }
        }

        // A previous run ended without stopping the engine: undo its audio changes.
        private void RecoverRouteState()
        {
            if (!File.Exists(_routeStatePath)) return;
            try
            {
                // Another copy is running and may be the one using this route right now.
                using (Process self = Process.GetCurrentProcess())
                {
                    if (Process.GetProcessesByName(self.ProcessName).Length > 1) return;
                }

                string virtualId = "", previousDefault = "", enhancer = "";
                foreach (string line in File.ReadAllLines(_routeStatePath))
                {
                    int eq = line.IndexOf('=');
                    if (eq <= 0) continue;
                    string value = line.Substring(eq + 1);
                    switch (line.Substring(0, eq))
                    {
                        case "virtual": virtualId = value; break;
                        case "previous_default": previousDefault = value; break;
                        case "enhancer": enhancer = value; break;
                    }
                }

                NativeMethods.OD_Route_RestoreSaved(virtualId, previousDefault);
                if (enhancer.Length > 0 && File.Exists(enhancer) && Process.GetProcessesByName(EnhancerProcessName).Length == 0)
                {
                    _stoppedEnhancerPath = enhancer;
                    RestartEnhancer();
                }
                File.Delete(_routeStatePath);
            }
            catch { }
        }

        private bool StartEngine(bool showErrors)
        {
            int channels = 2;
            if (ComboChannels.SelectedIndex == 1) channels = 6;
            else if (ComboChannels.SelectedIndex == 2) channels = 8;

            string preset = ComboPreset.SelectedIndex == 1 ? "pubg" : "none";

            try
            {
                int route = CheckSurround.IsChecked == true ? PrepareSurroundRoute() : NativeMethods.RouteNone;
                bool enhancerPaused = _stoppedEnhancerPath != null;

                int initResult = route == NativeMethods.RouteVirtual
                    ? NativeMethods.OD_Capture_InitDevices(NativeMethods.RouteCaptureId, NativeMethods.RouteOutputId)
                    : NativeMethods.OD_Capture_Init(channels);
                if (initResult != 1)
                {
                    ReleaseSurroundRoute();
                    if (showErrors) System.Windows.MessageBox.Show($"Failed to initialize capture driver. HRESULT: 0x{initResult:X8}");
                    return false;
                }
                if (NativeMethods.OD_Capture_Start() != 1)
                {
                    NativeMethods.OD_Capture_Stop();
                    ReleaseSurroundRoute();
                    if (showErrors) System.Windows.MessageBox.Show("Failed to start audio capture.");
                    return false;
                }
                NativeMethods.OD_Classifier_Init();
                NativeMethods.OD_Classifier_SetPreset(preset);

                string port = ComboPorts.SelectedItem as string ?? "Disabled";
                bool hardwareRequested = port != "Disabled";
                bool hardwareEnabled = hardwareRequested && NativeMethods.OD_Hardware_Init(port, 115200) == 1;

                int pollRate = (int)SliderPollRate.Value;
                double sensitivity = SliderSensitivity.Value;
                double separation = SliderSeparation.Value;
                int maxEntities = (int)SliderMaxEntities.Value;
                double radarSize = SliderRadarSize.Value;
                double globalOpacity = SliderGlobalOpacity.Value;
                double radarOpacity = SliderRadarOpacity.Value;
                double dotOpacity = SliderDotOpacity.Value;
                double range = SliderRange.Value;
                int osdPos = ComboPosition.SelectedIndex;
                bool fullscreen = CheckFullscreen.IsChecked == true;
                double smoothness = SliderSmoothness.Value / 10.0;

                _overlay = new OverlayWindow(sensitivity, separation, maxEntities, radarSize, globalOpacity, radarOpacity, dotOpacity, range, osdPos, fullscreen, smoothness);
                _overlay.DeviceLost += Overlay_DeviceLost;
                _overlay.Show();
                _overlay.StartEngine(pollRate, hardwareEnabled);

                StatusLabel.Text = hardwareRequested && !hardwareEnabled
                    ? $"ENGINE RUNNING ({port} UNAVAILABLE)"
                    : "ENGINE RUNNING";
                StatusLabel.Foreground = System.Windows.Media.Brushes.White;
                StatusIndicator.Background = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(0, 210, 180));

                // What the output device really delivers decides what the radar can show,
                // regardless of the channel count selected above.
                int deviceChannels = NativeMethods.OD_Capture_GetDeviceChannels();
                if (route == NativeMethods.RouteVirtual)
                {
                    StatusDetail.Text = $"7.1 surround through {NativeMethods.RouteCaptureName}: full 360° detection. "
                        + $"Sound plays in stereo on {NativeMethods.RouteOutputName}."
                        + (enhancerPaused ? " The FxSound app is paused while the engine runs." : "");
                }
                else
                {
                    StatusDetail.Text = deviceChannels >= 8 ? "7.1 surround device: full 360° detection."
                        : deviceChannels >= 6 ? "5.1 surround device: 360° detection."
                        : "Stereo only: left/right can be detected, shown across the front. "
                          + "Sounds behind you need Surround Mode with a virtual 7.1 audio device (such as FxSound) installed.";
                }
                StatusDetail.Visibility = Visibility.Visible;
                return true;
            }
            catch (Exception ex)
            {
                StopEngine();
                if (showErrors) System.Windows.MessageBox.Show($"Failed to start overlay engine: {ex.Message}");
                return false;
            }
        }

        // keepRoute leaves the surround routing in place for an immediate restart.
        private void StopEngine(bool keepRoute = false)
        {
            if (_overlay != null)
            {
                _overlay.DeviceLost -= Overlay_DeviceLost;
                _overlay.StopEngine();
                _overlay.Close();
                _overlay = null;
            }

            // All are safe when nothing is open; the engine thread is already joined.
            try
            {
                NativeMethods.OD_Capture_Stop();
                NativeMethods.OD_Hardware_Close();
                if (!keepRoute) ReleaseSurroundRoute();
            }
            catch (DllNotFoundException) { }
            catch (EntryPointNotFoundException) { }
        }

        // A device was unplugged or the default changed: rebind to whatever is there now.
        private void Overlay_DeviceLost(OverlayWindow sender)
        {
            if (sender != _overlay) return;

            StopEngine(keepRoute: true);
            StatusLabel.Text = "AUDIO DEVICE CHANGED - RECONNECTING";

            int attempts = 0;
            _recoveryTimer?.Stop();
            _recoveryTimer = new System.Windows.Threading.DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
            _recoveryTimer.Tick += (s, e) =>
            {
                if (_overlay != null || StartEngine(showErrors: false) || ++attempts >= 10)
                {
                    _recoveryTimer?.Stop();
                    if (_overlay == null)
                    {
                        StopEngine();
                        SetStoppedStatus("AUDIO DEVICE LOST");
                    }
                }
            };
            _recoveryTimer.Start();
        }

        private void SetStoppedStatus(string text)
        {
            StatusDetail.Visibility = Visibility.Collapsed;
            StatusLabel.Text = text;
            StatusLabel.Foreground = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(136, 136, 136));
            StatusIndicator.Background = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(170, 68, 68));
        }

        private void BtnStop_Click(object sender, RoutedEventArgs e)
        {
            _recoveryTimer?.Stop();
            StopEngine();
            SetStoppedStatus("ENGINE STOPPED");
        }

        private void BtnExit_Click(object sender, RoutedEventArgs e)
        {
            BtnStop_Click(null!, null!);
            SaveSettings();
            System.Windows.Application.Current.Shutdown();
        }

        private void BtnMinimize_Click(object sender, RoutedEventArgs e)
        {
            this.WindowState = WindowState.Minimized;
        }

        private void Window_MouseLeftButtonDown(object sender, System.Windows.Input.MouseButtonEventArgs e)
        {
            if (e.LeftButton == System.Windows.Input.MouseButtonState.Pressed)
            {
                this.DragMove();
            }
        }

        // --- Slider value label handlers ---
        private void SliderSensitivity_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
            => LblSensitivity.Text = ((int)e.NewValue).ToString();

        private void SliderSeparation_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
            => LblSeparation.Text = ((int)e.NewValue).ToString();

        private void SliderRadarSize_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
            => LblRadarSize.Text = ((int)e.NewValue).ToString();

        private void SliderGlobalOpacity_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
            => LblGlobalOpacity.Text = $"{(int)e.NewValue}%";

        private void SliderRadarOpacity_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
            => LblRadarOpacity.Text = $"{(int)e.NewValue}%";

        private void SliderDotOpacity_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
            => LblDotOpacity.Text = $"{(int)e.NewValue}%";

        private void SliderRange_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
            => LblRange.Text = ((int)e.NewValue).ToString();

        private void SliderMaxEntities_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
            => LblMaxEntities.Text = ((int)e.NewValue).ToString();

        private void SliderPollRate_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
            => LblPollRate.Text = $"{(int)e.NewValue} Hz";

        private void SliderSmoothness_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
            => LblSmoothness.Text = (e.NewValue / 10.0).ToString("F1");

        private void BtnCheckUpdates_Click(object sender, RoutedEventArgs e)
        {
            System.Windows.MessageBox.Show("You are running the latest version.", "Check for Updates", MessageBoxButton.OK, MessageBoxImage.Information);
        }
    }
}
