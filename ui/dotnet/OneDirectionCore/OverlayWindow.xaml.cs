using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Shapes;
using System.Windows.Threading;
using Color = System.Windows.Media.Color;
using Brush = System.Windows.Media.Brush;
using Point = System.Windows.Point;
using Rectangle = System.Windows.Shapes.Rectangle;
using Ellipse = System.Windows.Shapes.Ellipse;
using Line = System.Windows.Shapes.Line;
using Polygon = System.Windows.Shapes.Polygon;
using Brushes = System.Windows.Media.Brushes;

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
        private double _fadeTime;

        private float _sweepAngle = 0.0f;
        private DateTime _lastFrameTime = DateTime.Now;

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
        private readonly Color _themeBackground = Color.FromRgb(10, 15, 25);

        // WinAPI constants for click-through
        private const int GWL_EXSTYLE = -20;
        private const int WS_EX_TRANSPARENT = 0x00000020;
        private const int WS_EX_LAYERED    = 0x00080000;

        [DllImport("user32.dll")]
        private static extern int GetWindowLong(IntPtr hwnd, int index);

        [DllImport("user32.dll")]
        private static extern int SetWindowLong(IntPtr hwnd, int index, int newStyle);

        private RadarVisualHost _radarHost = new RadarVisualHost();
        private DispatcherTimer _topmostTimer;

        public class RadarVisualHost : FrameworkElement
        {
            private DrawingVisual _visual;
            public RadarVisualHost()
            {
                _visual = new DrawingVisual();
                this.AddVisualChild(_visual);
                this.AddLogicalChild(_visual);
            }
            public DrawingContext RenderOpen() => _visual.RenderOpen();
            protected override int VisualChildrenCount => 1;
            protected override Visual GetVisualChild(int index) => _visual;
            public void Clear() 
            {
                using (var dc = _visual.RenderOpen()) { }
            }
        }

        public OverlayWindow(double sensitivity, double separation, int maxEntities, double radarSize, double globalOpacity, double radarOpacity, double dotOpacity, double range, int osdPos, bool fullscreen, double smoothness, double fadeTime)
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
            _fadeTime = fadeTime;

            this.WindowState = WindowState.Maximized;
            this.Background = Brushes.Transparent;
            this.AllowsTransparency = true;
            this.WindowStyle = WindowStyle.None;
            this.Topmost = true;
            this.ShowInTaskbar = false;
            this.IsHitTestVisible = false;

            RadarCanvas.Children.Add(_radarHost);

            CompositionTarget.Rendering += OnRendering;

            _topmostTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
            _topmostTimer.Tick += (s, e) => {
                this.Topmost = false;
                this.Topmost = true;
            };
            _topmostTimer.Start();
        }

        protected override void OnSourceInitialized(EventArgs e)
        {
            base.OnSourceInitialized(e);
            // Apply WS_EX_TRANSPARENT so Windows ignores ALL mouse input on this window
            var hwnd = new WindowInteropHelper(this).Handle;
            int style = GetWindowLong(hwnd, GWL_EXSTYLE);
            SetWindowLong(hwnd, GWL_EXSTYLE, style | WS_EX_LAYERED | WS_EX_TRANSPARENT);
        }

        public void StartEngine(int pollRate)
        {
            
        }

        public void UpdateFadeTime(double fadeTime)
        {
            _fadeTime = fadeTime;
        }

        public void StopEngine()
        {
            CompositionTarget.Rendering -= OnRendering;
            _topmostTimer.Stop();
            _radarHost.Clear();
        }

        private void OnRendering(object? sender, EventArgs e)
        {
            DateTime now = DateTime.Now;
            double dt = (now - _lastFrameTime).TotalSeconds;
            _lastFrameTime = now;

            IntPtr bufferPtr = NativeMethods.OD_Capture_GetLatestBuffer();
            NativeMethods.SpatialData data = default;
            if (bufferPtr != IntPtr.Zero)
            {
                data = NativeMethods.OD_DSP_ProcessBuffer(bufferPtr, (float)_sensitivity, (float)_separation);
            }

            UpdateLogic(data, (float)dt);
            DrawHUD();
        }

        private void UpdateLogic(NativeMethods.SpatialData data, float dt)
        {
            int activeCount = data.EntityCount;
            if (activeCount > _maxEntities) activeCount = _maxEntities;

            var entities = data.GetEntities().Take(activeCount).OrderBy(e => e.Distance).ToList();

            // Smoothness: 0 = snap instantly to target with zero latency over the draw frame. 
            // values > 0 use exponential interpolation
            float lerpSpeed = (float)(20.0 / (1.0 + _smoothness * 3.8));
            bool snapInstant = (_smoothness < 0.01);

            for (int i = 0; i < MaxBlips; i++)
            {
                if (i < entities.Count)
                {
                    float targetAz = entities[i].AzimuthAngle;
                    float targetDist = entities[i].Distance;

                    if (snapInstant || _blips[i].Alpha < 0.01f) 
                    {
                        // Instantly teleport to target (low latency mode) or if dot was previously invisible
                        _blips[i].Azimuth = targetAz;
                        _blips[i].Distance = targetDist;
                    } 
                    else 
                    {
                        float diff = targetAz - _blips[i].Azimuth;
                        if (diff > 180.0f) diff -= 360.0f;
                        if (diff < -180.0f) diff += 360.0f;
                        _blips[i].Azimuth += diff * dt * lerpSpeed;
                        if (_blips[i].Azimuth < 0) _blips[i].Azimuth += 360.0f;
                        if (_blips[i].Azimuth >= 360.0f) _blips[i].Azimuth -= 360.0f;

                        _blips[i].Distance += (targetDist - _blips[i].Distance) * dt * lerpSpeed;
                    }
                    
                    _blips[i].Alpha = 1.0f;
                    _blips[i].Type = entities[i].SoundType;
                }
                else
                {
                    float fadeSpeed = (float)(1.0 / (_fadeTime > 0.01 ? _fadeTime : 0.01));
                    if (i >= _maxEntities) fadeSpeed *= 3.0f;
                    _blips[i].Alpha -= dt * fadeSpeed;
                    if (_blips[i].Alpha < 0) _blips[i].Alpha = 0;
                }
            }

            _sweepAngle += dt * 90.0f;
            if (_sweepAngle >= 360.0f) _sweepAngle -= 360.0f;
        }

        private void DrawHUD()
        {
            using (DrawingContext dc = _radarHost.RenderOpen())
            {
                double width = _radarSize;
                double height = _radarSize;
                double half = width / 2.0;
                double radius = (_fullscreen ? (this.ActualHeight * 0.45) : (half - 15.0));

                double cx = _fullscreen ? (this.ActualWidth / 2.0) : half;
                double cy = _fullscreen ? (this.ActualHeight / 2.0) : half;

                if (_fullscreen)
                {
                    width = this.ActualWidth;
                    height = this.ActualHeight;
                }

                if (!_fullscreen)
                {
                    double margin = 40.0; 
                    double px, py;
                    
                    double screenW = this.ActualWidth;
                    double screenH = this.ActualHeight;

                    if (screenW < 300 || screenH < 300) return; 

                    switch (_osdPosition) {
                        case 0: px = margin; py = margin; break;
                        case 1: px = (screenW - width) / 2.0; py = margin; break;
                        case 2: px = screenW - width - margin; py = margin; break;
                        case 3: px = margin; py = screenH - height - margin; break;
                        case 4: px = (screenW - width) / 2.0; py = screenH - height - margin; break;
                        case 5: px = screenW - width - margin; py = screenH - height - margin; break;
                        default: px = (screenW - width) / 2.0; py = margin; break;
                    }
                    Canvas.SetLeft(RadarCanvas, px);
                    Canvas.SetTop(RadarCanvas, py);

                    Brush bgBrush = new SolidColorBrush(Color.FromArgb((byte)(_globalOpacity * _radarOpacity * 200), 10, 15, 25));
                    bgBrush.Freeze();
                    dc.DrawEllipse(bgBrush, null, new Point(cx, cy), radius + 5, radius + 5);

                    DrawCircle(dc, cx, cy, radius, _themeTeal, (byte)(_globalOpacity * _radarOpacity * 255), 2);
                    DrawCircle(dc, cx, cy, radius * 0.66, _themeTeal, (byte)(_globalOpacity * _radarOpacity * 51), 1);
                    DrawCircle(dc, cx, cy, radius * 0.33, _themeTeal, (byte)(_globalOpacity * _radarOpacity * 51), 1);

                    DrawLine(dc, cx - radius, cy, cx + radius, cy, _themeTeal, (byte)(_globalOpacity * _radarOpacity * 38));
                    DrawLine(dc, cx, cy - radius, cx, cy + radius, _themeTeal, (byte)(_globalOpacity * _radarOpacity * 38));

                    DrawText(dc, "F", cx - 4, cy - radius - 18, 12, (byte)(_globalOpacity * _radarOpacity * 128));
                    DrawText(dc, "R", cx + radius + 6, cy - 8, 12, (byte)(_globalOpacity * _radarOpacity * 128));
                    DrawText(dc, "L", cx - radius - 16, cy - 8, 12, (byte)(_globalOpacity * _radarOpacity * 128));

                    double sr = (_sweepAngle - 90.0) * (Math.PI / 180.0);
                    DrawLine(dc, cx, cy, cx + Math.Cos(sr) * radius, cy + Math.Sin(sr) * radius, _themeTeal, (byte)(_globalOpacity * _radarOpacity * 76));
                }
                else
                {
                    DrawLine(dc, cx - 8, cy, cx + 8, cy, Colors.White, 38);
                    DrawLine(dc, cx, cy - 8, cx, cy + 8, Colors.White, 38);
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

                    Brush glowBrush = new SolidColorBrush(Color.FromArgb((byte)(alpha * 0.15), rCol, gCol, 20));
                    glowBrush.Freeze();
                    double gw = _fullscreen ? 22 : 12; // Radius
                    dc.DrawEllipse(glowBrush, null, new Point(bx, by), gw, gw);

                    DrawBlipIcon(dc, bx, by, _fullscreen ? 12 : 6, _blips[i].Type, blipColor, alpha);
                }
            }
        }

        private void DrawCircle(DrawingContext dc, double x, double y, double r, Color color, byte alpha, double thickness)
        {
            var pen = new System.Windows.Media.Pen(new SolidColorBrush(Color.FromArgb(alpha, color.R, color.G, color.B)), thickness);
            pen.Freeze();
            dc.DrawEllipse(null, pen, new Point(x, y), r, r);
        }

        private void DrawLine(DrawingContext dc, double x1, double y1, double x2, double y2, Color color, byte alpha)
        {
            var pen = new System.Windows.Media.Pen(new SolidColorBrush(Color.FromArgb(alpha, color.R, color.G, color.B)), 1);
            pen.Freeze();
            dc.DrawLine(pen, new Point(x1, y1), new Point(x2, y2));
        }

        private void DrawText(DrawingContext dc, string text, double x, double y, double size, byte alpha)
        {
            Brush brush = new SolidColorBrush(Color.FromArgb(alpha, 255, 255, 255));
            brush.Freeze();
            var ft = new FormattedText(text, System.Globalization.CultureInfo.InvariantCulture, System.Windows.FlowDirection.LeftToRight, 
                new Typeface("Segoe UI"), size, brush, VisualTreeHelper.GetDpi(this).PixelsPerDip);
            dc.DrawText(ft, new Point(x, y));
        }

        private void DrawBlipIcon(DrawingContext dc, double x, double y, double size, int type, Color color, byte alpha)
        {
            Brush brush = new SolidColorBrush(Color.FromArgb(alpha, color.R, color.G, color.B));
            brush.Freeze();
            
            if (type == 1) 
            {
                var geo = new StreamGeometry();
                using (var ctx = geo.Open())
                {
                    double s = size;
                    ctx.BeginFigure(new Point(x - s * 0.4, y + s * 0.6), true, true);
                    ctx.PolyLineTo(new[] {
                        new Point(x + s * 0.4, y + s * 0.6),
                        new Point(x + s * 0.6, y - s * 0.1),
                        new Point(x + s * 0.2, y - s * 0.6),
                        new Point(x - s * 0.2, y - s * 0.6),
                        new Point(x - s * 0.6, y - s * 0.1)
                    }, true, false);
                }
                geo.Freeze();
                dc.DrawGeometry(brush, null, geo);
            }
            else if (type == 2 || type == 3) 
            {
                var geo = new StreamGeometry();
                using (var ctx = geo.Open())
                {
                    ctx.BeginFigure(new Point(x, y - size * 0.8), true, true);
                    ctx.PolyLineTo(new[] {
                        new Point(x - size * 0.6, y + size * 0.6),
                        new Point(x + size * 0.6, y + size * 0.6)
                    }, true, false);
                }
                geo.Freeze();
                dc.DrawGeometry(brush, null, geo);
                dc.DrawRectangle(brush, null, new Rect(x - size * 0.2, y + size * 0.6, size * 0.4, size * 0.3));
            }
            else 
            {
                dc.DrawEllipse(brush, null, new Point(x, y), size * 0.8, size * 0.8);
            }
        }
    }
}
