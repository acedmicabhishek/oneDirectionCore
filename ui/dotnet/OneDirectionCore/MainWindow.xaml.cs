using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Ports;
using System.Linq;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
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
        private string _settingsPath = "settings_dotnet.cfg";
        private WinForms.NotifyIcon? _notifyIcon;
        private List<string> _outputDeviceIds = new();
        private List<string> _outputDeviceNames = new();
        private bool _suppressDeviceChange = false;
        private bool _userManuallySelected = false;
        private DispatcherTimer? _devicePollTimer;


        public MainWindow()
        {
            InitializeComponent();
            InitializeNotifyIcon();
            LoadPorts();
            LoadOutputDevices();
            AutoAssignOutputDevice();
            LoadSettings();
            StartDevicePolling();
        }

        private void StartDevicePolling()
        {
            _devicePollTimer = new DispatcherTimer();
            _devicePollTimer.Interval = TimeSpan.FromSeconds(3);
            _devicePollTimer.Tick += DevicePollTimer_Tick;
            _devicePollTimer.Start();
        }

        private void DevicePollTimer_Tick(object? sender, EventArgs e)
        {
            // Get fresh device list and compare
            var list = new NativeMethods.OD_DeviceList();
            list.Devices = new NativeMethods.OD_DeviceInfo[NativeMethods.OD_MAX_DEVICES];
            try { NativeMethods.OD_Capture_EnumRenderDevices(ref list); } catch { return; }

            var newIds = new List<string>();
            for (int i = 0; i < list.Count; i++)
                newIds.Add(list.Devices[i].Id ?? "");

            // Check if device list changed
            bool changed = newIds.Count != (_outputDeviceIds.Count - 1); // -1 for "Auto" entry
            if (!changed)
            {
                for (int i = 0; i < newIds.Count; i++)
                {
                    if (i + 1 >= _outputDeviceIds.Count || newIds[i] != _outputDeviceIds[i + 1])
                    { changed = true; break; }
                }
            }

            if (changed)
            {
                string currentId = ComboOutputDevice.SelectedIndex > 0 && ComboOutputDevice.SelectedIndex < _outputDeviceIds.Count
                    ? _outputDeviceIds[ComboOutputDevice.SelectedIndex] : "";

                LoadOutputDevices();

                // Try to re-select the previously selected device
                if (!string.IsNullOrEmpty(currentId))
                {
                    int idx = _outputDeviceIds.IndexOf(currentId);
                    if (idx >= 0)
                    {
                        _suppressDeviceChange = true;
                        ComboOutputDevice.SelectedIndex = idx;
                        _suppressDeviceChange = false;
                        return;
                    }
                }

                // Previous device disappeared or was Auto — auto-assign
                if (!_userManuallySelected)
                {
                    AutoAssignOutputDevice();
                }
            }
        }

        /// <summary>
        /// Auto-select the best output device. Priority: Earphone/Headphone > Speaker > first available.
        /// Skips CABLE/VB-Audio devices.
        /// </summary>
        private void AutoAssignOutputDevice()
        {
            _suppressDeviceChange = true;
            int bestIdx = 0; // default to Auto
            int speakerIdx = -1;

            for (int i = 1; i < _outputDeviceNames.Count; i++)
            {
                string name = _outputDeviceNames[i].ToUpperInvariant();

                // Skip virtual/cable devices
                if (name.Contains("CABLE") || name.Contains("VB-AUDIO") || name.Contains("FXSOUND")) continue;

                // Earphone/Headphone = highest priority — pick immediately
                if (name.Contains("EARPHONE") || name.Contains("HEADPHONE") || name.Contains("HEADSET") || name.Contains("EARBUDS"))
                {
                    bestIdx = i;
                    break;
                }

                // Speaker = fallback
                if (speakerIdx < 0 && (name.Contains("SPEAKER") || name.Contains("REALTEK")))
                {
                    speakerIdx = i;
                }
            }

            if (bestIdx == 0 && speakerIdx > 0) bestIdx = speakerIdx;

            ComboOutputDevice.SelectedIndex = bestIdx;
            _suppressDeviceChange = false;
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
            _devicePollTimer?.Stop();
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

        private void LoadOutputDevices()
        {
            _suppressDeviceChange = true;
            ComboOutputDevice.Items.Clear();
            _outputDeviceIds.Clear();
            _outputDeviceNames.Clear();
            ComboOutputDevice.Items.Add("Auto (first available)");
            _outputDeviceIds.Add("");
            _outputDeviceNames.Add("");
            try
            {
                var list = new NativeMethods.OD_DeviceList();
                list.Devices = new NativeMethods.OD_DeviceInfo[NativeMethods.OD_MAX_DEVICES];
                NativeMethods.OD_Capture_EnumRenderDevices(ref list);
                for (int i = 0; i < list.Count; i++)
                {
                    string name = list.Devices[i].Name ?? "Unknown";
                    ComboOutputDevice.Items.Add(name);
                    _outputDeviceIds.Add(list.Devices[i].Id ?? "");
                    _outputDeviceNames.Add(name);
                }
            }
            catch { }
            ComboOutputDevice.SelectedIndex = 0;
            _suppressDeviceChange = false;
        }

        private void LoadSettings()
        {
            if (File.Exists(_settingsPath))
            {
                try
                {
                    string[] lines = File.ReadAllLines(_settingsPath);
                    foreach (string line in lines)
                    {
                        string[] parts = line.Split('=');
                        if (parts.Length != 2) continue;

                        string key = parts[0].Trim();
                        string value = parts[1].Trim();

                        switch (key)
                        {
                            case "pollrate": SliderPollRate.Value = double.Parse(value); break;
                            case "channels_idx": ComboChannels.SelectedIndex = int.Parse(value); break;
                            case "preset_idx": ComboPreset.SelectedIndex = int.Parse(value); break;
                            case "sensitivity": SliderSensitivity.Value = double.Parse(value); break;
                            case "separation": SliderSeparation.Value = double.Parse(value); break;
                            case "range": SliderRange.Value = double.Parse(value); break;
                            case "fullscreen": CheckFullscreen.IsChecked = bool.Parse(value); break;
                            case "pos_idx": ComboPosition.SelectedIndex = int.Parse(value); break;
                            case "radar_size": SliderRadarSize.Value = double.Parse(value); break;
                            case "global_opacity": SliderGlobalOpacity.Value = double.Parse(value); break;
                            case "radar_opacity": SliderRadarOpacity.Value = double.Parse(value); break;
                            case "dot_opacity": SliderDotOpacity.Value = double.Parse(value); break;
                            case "max_entities": SliderMaxEntities.Value = double.Parse(value); break;
                            case "com_port": ComboPorts.Text = value; break;
                            case "min_to_tray": CheckTray.IsChecked = bool.Parse(value); break;
                            case "launch_startup": CheckStartup.IsChecked = bool.Parse(value); break;
                            case "smoothness": SliderSmoothness.Value = double.Parse(value); break;
                            case "audio_boost": SliderAudioBoost.Value = double.Parse(value); break;
                            case "output_device_idx":
                                int idx = int.Parse(value);
                                if (idx > 0 && idx < ComboOutputDevice.Items.Count)
                                {
                                    _suppressDeviceChange = true;
                                    ComboOutputDevice.SelectedIndex = idx;
                                    _userManuallySelected = true;
                                    _suppressDeviceChange = false;
                                }
                                break;
                        }
                    }
                }
                catch { }
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
                        string? exePath = Process.GetCurrentProcess().MainModule?.FileName;
                        if (exePath != null) key.SetValue("OneDirectionCore", exePath);
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
                using (StreamWriter sw = new StreamWriter(_settingsPath))
                {
                    sw.WriteLine($"pollrate={SliderPollRate.Value}");
                    sw.WriteLine($"channels_idx={ComboChannels.SelectedIndex}");
                    sw.WriteLine($"preset_idx={ComboPreset.SelectedIndex}");
                    sw.WriteLine($"sensitivity={SliderSensitivity.Value}");
                    sw.WriteLine($"separation={SliderSeparation.Value}");
                    sw.WriteLine($"range={SliderRange.Value}");
                    sw.WriteLine($"fullscreen={CheckFullscreen.IsChecked}");
                    sw.WriteLine($"min_to_tray={CheckTray.IsChecked}");
                    sw.WriteLine($"launch_startup={CheckStartup.IsChecked}");
                    sw.WriteLine($"pos_idx={ComboPosition.SelectedIndex}");
                    sw.WriteLine($"radar_size={SliderRadarSize.Value}");
                    sw.WriteLine($"global_opacity={SliderGlobalOpacity.Value}");
                    sw.WriteLine($"radar_opacity={SliderRadarOpacity.Value}");
                    sw.WriteLine($"dot_opacity={SliderDotOpacity.Value}");
                    sw.WriteLine($"max_entities={SliderMaxEntities.Value}");
                    sw.WriteLine($"com_port={ComboPorts.Text}");
                    sw.WriteLine($"smoothness={SliderSmoothness.Value}");
                    sw.WriteLine($"audio_boost={SliderAudioBoost.Value}");
                    sw.WriteLine($"output_device_idx={ComboOutputDevice.SelectedIndex}");
                }
            }
            catch { }
        }


        private void BtnStart_Click(object sender, RoutedEventArgs e)
        {
            if (_overlay != null) return;

            SaveSettings();

            int channels = 8; // Always 7.1 surround

            string preset = ComboPreset.SelectedIndex == 1 ? "pubg" : "none";

            try
            {
                // Set selected output device BEFORE Init
                int devIdx = ComboOutputDevice.SelectedIndex;
                if (devIdx >= 0 && devIdx < _outputDeviceIds.Count && !string.IsNullOrEmpty(_outputDeviceIds[devIdx]))
                {
                    NativeMethods.OD_Capture_SetRenderDeviceId(_outputDeviceIds[devIdx]);
                }
                else
                {
                    NativeMethods.OD_Capture_SetRenderDeviceId(null!);
                }

                int initResult = NativeMethods.OD_Capture_Init(channels);
                if (initResult != 1)
                {
                    System.Windows.MessageBox.Show($"Failed to initialize capture driver. HRESULT: 0x{initResult:X8}");
                    return;
                }
                NativeMethods.OD_Capture_Start();
                NativeMethods.OD_Classifier_Init();
                NativeMethods.OD_Classifier_SetPreset(preset);

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
                float volumeMultiplier = (float)(SliderAudioBoost.Value / 100.0);

                NativeMethods.OD_Capture_SetVolumeMultiplier(volumeMultiplier);
                
                _overlay = new OverlayWindow(sensitivity, separation, maxEntities, radarSize, globalOpacity, radarOpacity, dotOpacity, range, osdPos, fullscreen, smoothness);
                _overlay.Show();
                _overlay.StartEngine(pollRate);

                StatusLabel.Text = "ENGINE RUNNING";
                StatusLabel.Foreground = System.Windows.Media.Brushes.White;
                StatusIndicator.Background = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(0, 210, 180));
            }
            catch (Exception ex)
            {
                System.Windows.MessageBox.Show($"Failed to start overlay engine: {ex.Message}");
            }
        }

        private void BtnStop_Click(object sender, RoutedEventArgs e)
        {
            if (_overlay != null)
            {
                _overlay.StopEngine();
                _overlay.Close();
                _overlay = null;

                NativeMethods.OD_Capture_Stop();
            }
            StatusLabel.Text = "ENGINE STOPPED";
            StatusLabel.Foreground = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(136, 136, 136));
            StatusIndicator.Background = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(170, 68, 68));
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

        private void SliderAudioBoost_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            if (LblAudioBoost != null) LblAudioBoost.Text = $"{(int)e.NewValue}%";
            if (_overlay != null)
            {
                // Live update the core engine if running
                NativeMethods.OD_Capture_SetVolumeMultiplier((float)(e.NewValue / 100.0));
            }
        }

        private void BtnCheckUpdates_Click(object sender, RoutedEventArgs e)
        {
            System.Windows.MessageBox.Show("You are running the latest version.", "Check for Updates", MessageBoxButton.OK, MessageBoxImage.Information);
        }

        private void ComboOutputDevice_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            if (_suppressDeviceChange) return;

            // User made a manual selection — disable auto-assign
            _userManuallySelected = true;

            if (_overlay == null) return; // Engine not running, just save the setting

            // Auto-restart the engine with the new output device
            BtnStop_Click(null!, null!);
            BtnStart_Click(null!, null!);
        }

        private void BtnStereoOutput_Click(object sender, RoutedEventArgs e)
        {
            string deviceName = ComboOutputDevice.SelectedItem?.ToString() ?? "Auto";
            System.Windows.MessageBox.Show(
                $"Output is set to Stereo (2.0) on:\n\n🔊 {deviceName}\n\nAll 7.1 capture audio is automatically downmixed to stereo for your speakers/earphones.",
                "Stereo Output Active",
                MessageBoxButton.OK,
                MessageBoxImage.Information);
        }
    }
}
