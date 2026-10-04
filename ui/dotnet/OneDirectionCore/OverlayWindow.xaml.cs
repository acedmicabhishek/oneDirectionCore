using System;
using System.Diagnostics;
using System.Globalization;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Threading;
using Color = System.Windows.Media.Color;
using Brush = System.Windows.Media.Brush;
using Pen = System.Windows.Media.Pen;
using Point = System.Windows.Point;
using Brushes = System.Windows.Media.Brushes;
using FlowDirection = System.Windows.FlowDirection;

namespace OneDirectionCore
{
    public partial class OverlayWindow : Window
    {
        private double _sensitivity;
        private double _separation;
        private int _maxEntities;
        private double _radarSize;
        private double _globalOpacity;
        private double _radarOpacity;
        private double _dotOpacity;
        private double _zoom;
        private int _osdPosition;
        private bool _fullscreen;
        private double _smoothness;

        private float _sweepAngle = 0.0f;
        private readonly Stopwatch _clock = Stopwatch.StartNew();
        private double _lastFrameSeconds;

        private class BlipState
        {
            public float Azimuth;
            public float Distance;
            public float Alpha;
            public int Type;
        }

        private const int MaxBlips = 10;
        private readonly BlipState[] _blips = Enumerable.Range(0, MaxBlips).Select(_ => new BlipState { Distance = 0.5f }).ToArray();


        private readonly Color _themeTeal = Color.FromRgb(0, 220, 180);

        // Static radar chrome; opacity is fixed for the window's lifetime, so these are built once.
        private readonly Brush _backgroundBrush;
        private readonly Brush _labelBrush;
        private readonly Pen _outerRingPen;
        private readonly Pen _innerRingPen;
        private readonly Pen _axisPen;
        private readonly Pen _sweepPen;
        private readonly Pen _crosshairPen;
        private FormattedText? _labelF, _labelR, _labelL;

        private sealed class RadarSurface : FrameworkElement
        {
            public Action<DrawingContext>? Paint;
            protected override void OnRender(DrawingContext dc) => Paint?.Invoke(dc);
        }

        private readonly RadarSurface _surface = new RadarSurface();

        // The radar is redrawn at a fixed 30 fps instead of the monitor's refresh rate: on a
        // 144 Hz screen that is a fifth of the work, and nothing on a radar moves fast enough to show it.
        private readonly DispatcherTimer _frameTimer = new DispatcherTimer(DispatcherPriority.Render) { Interval = TimeSpan.FromSeconds(1.0 / 30.0) };
        // Space around the radar square for its edge labels.
        private const double WindowPad = 8.0;

        // Engine thread: polls the capture buffer and runs the DSP at the configured rate.
        private Thread? _engineThread;
        private volatile bool _engineRunning;
        private readonly object _dataLock = new object();
        private NativeMethods.SpatialData _latestData;

        /// <summary>Raised on the UI thread when the audio device was unplugged or the default device changed.</summary>
        public event Action<OverlayWindow>? DeviceLost;

        // WinAPI constants for click-through
        private const int GWL_EXSTYLE = -20;
        private const int WS_EX_TRANSPARENT = 0x00000020;
        private const int WS_EX_TOOLWINDOW = 0x00000080;
        private const int WS_EX_LAYERED    = 0x00080000;
        private const int WS_EX_NOACTIVATE = 0x08000000;

        [DllImport("user32.dll")]
        private static extern int GetWindowLong(IntPtr hwnd, int index);

        [DllImport("user32.dll")]
        private static extern int SetWindowLong(IntPtr hwnd, int index, int newStyle);

        [DllImport("user32.dll")]
        private static extern bool SetWindowPos(IntPtr hwnd, IntPtr insertAfter, int x, int y, int cx, int cy, uint flags);

        private static readonly IntPtr HWND_TOPMOST = new IntPtr(-1);
        private const uint SWP_NOSIZE = 0x0001;
        private const uint SWP_NOMOVE = 0x0002;
        private const uint SWP_NOACTIVATE = 0x0010;

        // A game that marks its own window topmost when it takes focus ends up above the overlay,
        // so the overlay is moved back to the top of the topmost band once a second.
        private IntPtr _hwnd;
        private readonly DispatcherTimer _topmostTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };

        [DllImport("winmm.dll")]
        private static extern uint timeBeginPeriod(uint period);

        [DllImport("winmm.dll")]
        private static extern uint timeEndPeriod(uint period);

        public OverlayWindow(double sensitivity, double separation, int maxEntities, double radarSize, double globalOpacity, double radarOpacity, double dotOpacity, double range, int osdPos, bool fullscreen, double smoothness)
        {
            InitializeComponent();

            _sensitivity = sensitivity / 100.0;
            _separation = 60.0 - (separation * 0.55);
            _maxEntities = maxEntities;
            _radarSize = radarSize;
            _globalOpacity = globalOpacity / 100.0;
            _radarOpacity = radarOpacity / 100.0;
            _dotOpacity = dotOpacity / 100.0;
            _zoom = range / 50.0;
            _osdPosition = osdPos;
            _fullscreen = fullscreen;
            _smoothness = smoothness;

            double chrome = _globalOpacity * _radarOpacity;
            _backgroundBrush = Frozen(new SolidColorBrush(Color.FromArgb((byte)(chrome * 200), 10, 15, 25)));
            _labelBrush = Frozen(new SolidColorBrush(Color.FromArgb((byte)(chrome * 128), 255, 255, 255)));
            _outerRingPen = MakePen(_themeTeal, (byte)(chrome * 255), 2);
            _innerRingPen = MakePen(_themeTeal, (byte)(chrome * 51), 1);
            _axisPen = MakePen(_themeTeal, (byte)(chrome * 38), 1);
            _sweepPen = MakePen(_themeTeal, (byte)(chrome * 76), 1);
            _crosshairPen = MakePen(Colors.White, 38, 1);

            if (_fullscreen) this.WindowState = WindowState.Maximized;
            else PlaceRadarWindow();
            this.Background = Brushes.Transparent;
            this.AllowsTransparency = true;
            this.WindowStyle = WindowStyle.None;
            this.Topmost = true;
            this.ShowInTaskbar = false;
            this.IsHitTestVisible = false;

            _surface.Paint = DrawHUD;
            this.Content = _surface;

            _frameTimer.Tick += OnFrame;
            _frameTimer.Start();
        }

        // In radar mode the window is only as large as the radar. A full-screen transparent
        // window makes Windows blend the whole screen over the game on every frame.
        private void PlaceRadarWindow()
        {
            double screenW = SystemParameters.PrimaryScreenWidth;
            double screenH = SystemParameters.PrimaryScreenHeight;
            double size = _radarSize;
            double margin = 40.0;
            double px, py;

            switch (_osdPosition) {
                case 0: px = margin; py = margin; break;
                case 1: px = (screenW - size) / 2.0; py = margin; break;
                case 2: px = screenW - size - margin; py = margin; break;
                case 3: px = margin; py = screenH - size - margin; break;
                case 4: px = (screenW - size) / 2.0; py = screenH - size - margin; break;
                case 5: px = screenW - size - margin; py = screenH - size - margin; break;
                default: px = (screenW - size) / 2.0; py = margin; break;
            }

            this.WindowStartupLocation = WindowStartupLocation.Manual;
            this.WindowState = WindowState.Normal;
            this.Left = px - WindowPad;
            this.Top = py - WindowPad;
            this.Width = size + WindowPad * 2;
            this.Height = size + WindowPad * 2;
        }

        private static T Frozen<T>(T freezable) where T : Freezable
        {
            freezable.Freeze();
            return freezable;
        }

        private static Pen MakePen(Color color, byte alpha, double thickness)
            => Frozen(new Pen(new SolidColorBrush(Color.FromArgb(alpha, color.R, color.G, color.B)), thickness));

        protected override void OnSourceInitialized(EventArgs e)
        {
            base.OnSourceInitialized(e);
            // WS_EX_TRANSPARENT makes Windows ignore ALL mouse input on this window;
            // NOACTIVATE/TOOLWINDOW keep it from stealing focus from the game or showing in Alt+Tab.
            var hwnd = new WindowInteropHelper(this).Handle;
            int style = GetWindowLong(hwnd, GWL_EXSTYLE);
            SetWindowLong(hwnd, GWL_EXSTYLE, style | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);

            _hwnd = hwnd;
            _topmostTimer.Tick += (s, args) => SetWindowPos(_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            _topmostTimer.Start();
        }

        public void StartEngine(int pollRate, bool hardwareEnabled)
        {
            if (_engineThread != null) return;
            if (pollRate < 1) pollRate = 60;

            _engineRunning = true;
            _engineThread = new Thread(() => EngineLoop(pollRate, hardwareEnabled))
            {
                IsBackground = true,
                Name = "ODC Engine"
            };
            _engineThread.Start();
        }

        public void StopEngine()
        {
            _frameTimer.Stop();
            _topmostTimer.Stop();

            _engineRunning = false;
            if (_engineThread != null)
            {
                // Must finish before the caller tears down the native capture buffers.
                _engineThread.Join();
                _engineThread = null;
            }
        }

        protected override void OnClosed(EventArgs e)
        {
            StopEngine();
            base.OnClosed(e);
        }

        private void EngineLoop(int pollRate, bool hardwareEnabled)
        {
            // Default timer granularity (~15.6 ms) would cap the loop near 64 Hz.
            timeBeginPeriod(1);
            try
            {
                var timer = Stopwatch.StartNew();
                double interval = 1.0 / pollRate;
                double next = 0.0;
                float sensitivity = (float)_sensitivity;
                float separation = (float)_separation;

                int analysedSequence = -1;
                bool showing = false;
                double nextRouteCheck = 1.0;

                while (_engineRunning)
                {
                    if (NativeMethods.OD_Capture_IsDeviceLost() != 0)
                    {
                        Dispatcher.BeginInvoke(new Action(() => DeviceLost?.Invoke(this)));
                        break;
                    }

                    // Headphones plugged in, or the Windows output switched: the surround route has to follow.
                    if (timer.Elapsed.TotalSeconds >= nextRouteCheck)
                    {
                        nextRouteCheck = timer.Elapsed.TotalSeconds + 1.0;
                        if (NativeMethods.OD_Route_NeedsUpdate() != 0)
                        {
                            Dispatcher.BeginInvoke(new Action(() => DeviceLost?.Invoke(this)));
                            break;
                        }
                    }

                    // Read before fetching the buffer, so the buffer is never older than the number.
                    int sequence = NativeMethods.OD_Capture_GetSequence();
                    IntPtr bufferPtr = NativeMethods.OD_Capture_GetLatestBuffer();

                    if (bufferPtr == IntPtr.Zero)
                    {
                        // Nothing is playing: clear the radar once.
                        if (showing)
                        {
                            lock (_dataLock) _latestData = default;
                            showing = false;
                        }
                    }
                    else if (sequence != analysedSequence)
                    {
                        // Audio arrives every 10 ms; polling faster would only re-analyse the same window.
                        analysedSequence = sequence;
                        NativeMethods.SpatialData data = NativeMethods.OD_DSP_ProcessBuffer(bufferPtr, sensitivity, separation);
                        lock (_dataLock) _latestData = data;
                        showing = true;

                        if (hardwareEnabled && data.EntityCount > 0)
                        {
                            NativeMethods.OD_Hardware_SendDirectionLog(data.E0.AzimuthAngle);
                        }
                    }

                    next += interval;
                    double wait = next - timer.Elapsed.TotalSeconds;
                    if (wait > 0) Thread.Sleep(TimeSpan.FromSeconds(wait));
                    else next = timer.Elapsed.TotalSeconds;
                }
            }
            finally
            {
                timeEndPeriod(1);
            }
        }

        private void OnFrame(object? sender, EventArgs e)
        {
            double now = _clock.Elapsed.TotalSeconds;
            double dt = now - _lastFrameSeconds;
            _lastFrameSeconds = now;
            if (dt > 0.1) dt = 0.1;

            NativeMethods.SpatialData data;
            lock (_dataLock) data = _latestData;

            UpdateLogic(data, (float)dt);
            _surface.InvalidateVisual();
        }

        private void UpdateLogic(NativeMethods.SpatialData data, float dt)
        {
            int activeCount = data.EntityCount;
            if (activeCount > _maxEntities) activeCount = _maxEntities;

            var entities = data.GetEntities().Take(activeCount).OrderBy(e => e.Distance).ToList();

            // Smoothness: 0 = snappy (lerpSpeed=20), 5 = very smooth (lerpSpeed=1)
            float lerpSpeed = (float)(20.0 / (1.0 + _smoothness * 3.8));
            float lerp = Math.Min(1.0f, dt * lerpSpeed);

            for (int i = 0; i < MaxBlips; i++)
            {
                if (i < entities.Count)
                {
                    float targetAz = entities[i].AzimuthAngle;
                    float targetDist = entities[i].Distance;

                    if (_blips[i].Alpha <= 0.01f)
                    {
                        // A blip that had faded out appears in place instead of sliding in from its stale position.
                        _blips[i].Azimuth = targetAz;
                        _blips[i].Distance = targetDist;
                    }
                    else
                    {
                        float diff = targetAz - _blips[i].Azimuth;
                        if (diff > 180.0f) diff -= 360.0f;
                        if (diff < -180.0f) diff += 360.0f;
                        _blips[i].Azimuth += diff * lerp;
                        if (_blips[i].Azimuth < 0) _blips[i].Azimuth += 360.0f;
                        if (_blips[i].Azimuth >= 360.0f) _blips[i].Azimuth -= 360.0f;

                        _blips[i].Distance += (targetDist - _blips[i].Distance) * lerp;
                    }
                    _blips[i].Alpha = 1.0f;
                    _blips[i].Type = entities[i].SoundType;
                }
                else
                {
                    float fadeSpeed = (i >= _maxEntities) ? 10.0f : 3.0f;
                    _blips[i].Alpha -= dt * fadeSpeed;
                    if (_blips[i].Alpha < 0) _blips[i].Alpha = 0;
                }
            }

            _sweepAngle += dt * 90.0f;
            if (_sweepAngle >= 360.0f) _sweepAngle -= 360.0f;
        }

        private void DrawHUD(DrawingContext dc)
        {
            double half = _radarSize / 2.0;
            double radius = (_fullscreen ? (this.ActualHeight * 0.45) : (half - 15.0));

            double cx = this.ActualWidth / 2.0;
            double cy = this.ActualHeight / 2.0;

            if (!_fullscreen)
            {
                // The window itself sits at the chosen screen corner (see PlaceRadarWindow).
                cx = WindowPad + half;
                cy = WindowPad + half;
                Point center = new Point(cx, cy);


                dc.DrawEllipse(_backgroundBrush, null, center, radius + 5, radius + 5);


                dc.DrawEllipse(null, _outerRingPen, center, radius, radius);
                dc.DrawEllipse(null, _innerRingPen, center, radius * 0.66, radius * 0.66);
                dc.DrawEllipse(null, _innerRingPen, center, radius * 0.33, radius * 0.33);


                dc.DrawLine(_axisPen, new Point(cx - radius, cy), new Point(cx + radius, cy));
                dc.DrawLine(_axisPen, new Point(cx, cy - radius), new Point(cx, cy + radius));


                _labelF ??= MakeLabel("F");
                _labelR ??= MakeLabel("R");
                _labelL ??= MakeLabel("L");
                dc.DrawText(_labelF, new Point(cx - 4, cy - radius - 18));
                dc.DrawText(_labelR, new Point(cx + radius + 6, cy - 8));
                dc.DrawText(_labelL, new Point(cx - radius - 16, cy - 8));


                double sr = (_sweepAngle - 90.0) * (Math.PI / 180.0);
                dc.DrawLine(_sweepPen, center, new Point(cx + Math.Cos(sr) * radius, cy + Math.Sin(sr) * radius));
            }
            else
            {

                dc.DrawLine(_crosshairPen, new Point(cx - 8, cy), new Point(cx + 8, cy));
                dc.DrawLine(_crosshairPen, new Point(cx, cy - 8), new Point(cx, cy + 8));
            }


            for (int i = 0; i < MaxBlips; i++)
            {
                if (_blips[i].Alpha < 0.01f) continue;

                double angleRad = (_blips[i].Azimuth - 90.0) * (Math.PI / 180.0);
                double d = _blips[i].Distance * radius * _zoom;
                if (d > radius) d = radius;

                double bx = cx + Math.Cos(angleRad) * d;
                double by = cy + Math.Sin(angleRad) * d;

                byte alpha = (byte)(_blips[i].Alpha * _globalOpacity * _dotOpacity * 255);
                float t = _blips[i].Distance;
                byte rCol = (byte)(255 * (1.0 - t));
                byte gCol = (byte)(255 * t);
                Color blipColor = Color.FromRgb(rCol, gCol, 20);


                double glowRadius = _fullscreen ? 22 : 12;
                dc.DrawEllipse(new SolidColorBrush(Color.FromArgb((byte)(alpha * 0.15), rCol, gCol, 20)), null, new Point(bx, by), glowRadius, glowRadius);


                DrawBlipIcon(dc, bx, by, _fullscreen ? 12 : 6, _blips[i].Type, blipColor, alpha);
            }
        }

        private FormattedText MakeLabel(string text)
            => new FormattedText(text, CultureInfo.InvariantCulture, FlowDirection.LeftToRight,
                                 new Typeface("Segoe UI"), 12, _labelBrush, VisualTreeHelper.GetDpi(this).PixelsPerDip);

        private static void DrawPolygon(DrawingContext dc, Brush brush, params Point[] points)
        {
            StreamGeometry geometry = new StreamGeometry();
            using (StreamGeometryContext ctx = geometry.Open())
            {
                ctx.BeginFigure(points[0], true, true);
                for (int i = 1; i < points.Length; i++) ctx.LineTo(points[i], false, false);
            }
            geometry.Freeze();
            dc.DrawGeometry(brush, null, geometry);
        }

        private static void DrawBlipIcon(DrawingContext dc, double x, double y, double size, int type, Color color, byte alpha)
        {
            Brush brush = new SolidColorBrush(Color.FromArgb(alpha, color.R, color.G, color.B));

            if (type == 1)
            {
                double s = size;
                DrawPolygon(dc, brush,
                    new Point(x - s * 0.4, y + s * 0.6),
                    new Point(x + s * 0.4, y + s * 0.6),
                    new Point(x + s * 0.6, y - s * 0.1),
                    new Point(x + s * 0.2, y - s * 0.6),
                    new Point(x - s * 0.2, y - s * 0.6),
                    new Point(x - s * 0.6, y - s * 0.1));
            }
            else if (type == 2 || type == 3)
            {
                DrawPolygon(dc, brush,
                    new Point(x, y - size * 0.8),
                    new Point(x - size * 0.6, y + size * 0.6),
                    new Point(x + size * 0.6, y + size * 0.6));

                dc.DrawRectangle(brush, null, new Rect(x - size * 0.2, y + size * 0.6, size * 0.4, size * 0.3));
            }
            else
            {
                dc.DrawEllipse(brush, null, new Point(x, y), size * 0.8, size * 0.8);
            }
        }
    }
}
