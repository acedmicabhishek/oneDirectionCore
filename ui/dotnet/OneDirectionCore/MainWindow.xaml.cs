using System;
using System.Diagnostics;
using System.IO;
using System.IO.Ports;
using System.Windows;
using System.Windows.Controls;

namespace OneDirectionCore
{
    public partial class MainWindow : Window
    {
        private OverlayWindow? _overlay;
        private string _settingsPath = "settings_dotnet.cfg";

        public MainWindow()
        {
            InitializeComponent();
            LoadPorts();
            LoadSettings();
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
                        }
                    }
                }
                catch { }
            }
        }

        private void SaveSettings()
        {
            try
            {
                using (StreamWriter sw = new StreamWriter(_settingsPath))
                {
                    sw.WriteLine($"pollrate={SliderPollRate.Value}");
                    sw.WriteLine($"channels_idx={ComboChannels.SelectedIndex}");
                    sw.WriteLine($"preset_idx={ComboPreset.SelectedIndex}");
                    sw.WriteLine($"sensitivity={SliderSensitivity.Value}");
                    sw.WriteLine($"separation={SliderSeparation.Value}");
                    sw.WriteLine($"range={SliderRange.Value}");
                    sw.WriteLine($"fullscreen={CheckFullscreen.IsChecked}");
                    sw.WriteLine($"pos_idx={ComboPosition.SelectedIndex}");
                    sw.WriteLine($"radar_size={SliderRadarSize.Value}");
                    sw.WriteLine($"global_opacity={SliderGlobalOpacity.Value}");
                    sw.WriteLine($"radar_opacity={SliderRadarOpacity.Value}");
                    sw.WriteLine($"dot_opacity={SliderDotOpacity.Value}");
                    sw.WriteLine($"max_entities={SliderMaxEntities.Value}");
                    sw.WriteLine($"com_port={ComboPorts.Text}");
                }
            }
            catch { }
        }

        private void BtnStart_Click(object sender, RoutedEventArgs e)
        {
            if (_overlay != null) return;

            SaveSettings();

            int channels = 2;
            if (ComboChannels.SelectedIndex == 1) channels = 6;
            else if (ComboChannels.SelectedIndex == 2) channels = 8;

            string preset = ComboPreset.SelectedIndex == 1 ? "pubg" : "none";

            try
            {
                int initResult = NativeMethods.OD_Capture_Init(channels);
                if (initResult != 1)
                {
                    MessageBox.Show($"Failed to initialize capture driver. HRESULT: 0x{initResult:X8}");
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

                _overlay = new OverlayWindow(sensitivity, separation, maxEntities, radarSize, globalOpacity, radarOpacity, dotOpacity, range, osdPos, fullscreen);
                _overlay.Show();
                _overlay.StartEngine(pollRate);

                StatusLabel.Text = "ENGINE RUNNING";
                StatusLabel.Foreground = System.Windows.Media.Brushes.White;
                StatusIndicator.Background = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(0, 210, 180));
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Failed to start overlay engine: {ex.Message}");
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
            Application.Current.Shutdown();
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
    }
}
