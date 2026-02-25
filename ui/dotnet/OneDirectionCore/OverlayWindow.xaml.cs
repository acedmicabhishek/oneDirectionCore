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

            this.WindowState = WindowState.Maximized;
            this.Background = Brushes.Transparent;
            this.AllowsTransparency = true;
            this.WindowStyle = WindowStyle.None;
            this.Topmost = true;
            this.ShowInTaskbar = false;
            this.IsHitTestVisible = false;

            CompositionTarget.Rendering += OnRendering;
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

        public void StopEngine()
        {
            CompositionTarget.Rendering -= OnRendering;
            RadarCanvas.Children.Clear();
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

            // Smoothness: 0 = snappy (lerpSpeed=20), 5 = very smooth (lerpSpeed=1)
            float lerpSpeed = (float)(20.0 / (1.0 + _smoothness * 3.8));

            for (int i = 0; i < MaxBlips; i++)
            {
                if (i < entities.Count)
                {
                    float targetAz = entities[i].AzimuthAngle;
                    float targetDist = entities[i].Distance;

                    float diff = targetAz - _blips[i].Azimuth;
                    if (diff > 180.0f) diff -= 360.0f;
                    if (diff < -180.0f) diff += 360.0f;
                    _blips[i].Azimuth += diff * dt * lerpSpeed;
                    if (_blips[i].Azimuth < 0) _blips[i].Azimuth += 360.0f;
                    if (_blips[i].Azimuth >= 360.0f) _blips[i].Azimuth -= 360.0f;

                    _blips[i].Distance += (targetDist - _blips[i].Distance) * dt * lerpSpeed;
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

        private void DrawHUD()
        {
            RadarCanvas.Children.Clear();

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

                
                Ellipse bg = new Ellipse {
                    Width = radius * 2 + 10, Height = radius * 2 + 10,
                    Fill = new SolidColorBrush(Color.FromArgb((byte)(_globalOpacity * _radarOpacity * 200), 10, 15, 25)),
                };
                Canvas.SetLeft(bg, cx - (radius + 5));
                Canvas.SetTop(bg, cy - (radius + 5));
                RadarCanvas.Children.Add(bg);

                
                DrawCircle(cx, cy, radius, _themeTeal, (byte)(_globalOpacity * _radarOpacity * 255), 2);
                DrawCircle(cx, cy, radius * 0.66, _themeTeal, (byte)(_globalOpacity * _radarOpacity * 51), 1);
                DrawCircle(cx, cy, radius * 0.33, _themeTeal, (byte)(_globalOpacity * _radarOpacity * 51), 1);

                
                DrawLine(cx - radius, cy, cx + radius, cy, _themeTeal, (byte)(_globalOpacity * _radarOpacity * 38));
                DrawLine(cx, cy - radius, cx, cy + radius, _themeTeal, (byte)(_globalOpacity * _radarOpacity * 38));

                
                DrawText("F", cx - 4, cy - radius - 18, 12, (byte)(_globalOpacity * _radarOpacity * 128));
                DrawText("R", cx + radius + 6, cy - 8, 12, (byte)(_globalOpacity * _radarOpacity * 128));
                DrawText("L", cx - radius - 16, cy - 8, 12, (byte)(_globalOpacity * _radarOpacity * 128));

                
                double sr = (_sweepAngle - 90.0) * (Math.PI / 180.0);
                DrawLine(cx, cy, cx + Math.Cos(sr) * radius, cy + Math.Sin(sr) * radius, _themeTeal, (byte)(_globalOpacity * _radarOpacity * 76));
            }
            else
            {
                
                DrawLine(cx - 8, cy, cx + 8, cy, Colors.White, 38);
                DrawLine(cx, cy - 8, cx, cy + 8, Colors.White, 38);
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

                
                Ellipse glow = new Ellipse {
                    Width = _fullscreen ? 44 : 24, Height = _fullscreen ? 44 : 24,
                    Fill = new SolidColorBrush(Color.FromArgb((byte)(alpha * 0.15), rCol, gCol, 20))
                };
                Canvas.SetLeft(glow, bx - glow.Width/2);
                Canvas.SetTop(glow, by - glow.Height/2);
                RadarCanvas.Children.Add(glow);

                
                DrawBlipIcon(bx, by, _fullscreen ? 12 : 6, _blips[i].Type, blipColor, alpha);
            }
        }

        private void DrawCircle(double x, double y, double r, Color color, byte alpha, double thickness)
        {
            Ellipse el = new Ellipse {
                Width = r * 2, Height = r * 2,
                Stroke = new SolidColorBrush(Color.FromArgb(alpha, color.R, color.G, color.B)),
                StrokeThickness = thickness
            };
            Canvas.SetLeft(el, x - r);
            Canvas.SetTop(el, y - r);
            RadarCanvas.Children.Add(el);
        }

        private void DrawLine(double x1, double y1, double x2, double y2, Color color, byte alpha)
        {
            Line line = new Line {
                X1 = x1, Y1 = y1, X2 = x2, Y2 = y2,
                Stroke = new SolidColorBrush(Color.FromArgb(alpha, color.R, color.G, color.B)),
                StrokeThickness = 1
            };
            RadarCanvas.Children.Add(line);
        }

        private void DrawText(string text, double x, double y, double size, byte alpha)
        {
            TextBlock tb = new TextBlock {
                Text = text, FontSize = size,
                Foreground = new SolidColorBrush(Color.FromArgb(alpha, 255, 255, 255))
            };
            Canvas.SetLeft(tb, x);
            Canvas.SetTop(tb, y);
            RadarCanvas.Children.Add(tb);
        }

        private void DrawBlipIcon(double x, double y, double size, int type, Color color, byte alpha)
        {
            Brush brush = new SolidColorBrush(Color.FromArgb(alpha, color.R, color.G, color.B));
            
            if (type == 1) 
            {
                Polygon poly = new Polygon { Fill = brush };
                double s = size;
                poly.Points.Add(new Point(x - s * 0.4, y + s * 0.6));
                poly.Points.Add(new Point(x + s * 0.4, y + s * 0.6));
                poly.Points.Add(new Point(x + s * 0.6, y - s * 0.1));
                poly.Points.Add(new Point(x + s * 0.2, y - s * 0.6));
                poly.Points.Add(new Point(x - s * 0.2, y - s * 0.6));
                poly.Points.Add(new Point(x - s * 0.6, y - s * 0.1));
                RadarCanvas.Children.Add(poly);
            }
            else if (type == 2 || type == 3) 
            {
                Polygon tri = new Polygon { Fill = brush };
                tri.Points.Add(new Point(x, y - size * 0.8));
                tri.Points.Add(new Point(x - size * 0.6, y + size * 0.6));
                tri.Points.Add(new Point(x + size * 0.6, y + size * 0.6));
                RadarCanvas.Children.Add(tri);
                
                Rectangle stem = new Rectangle { Width = size * 0.4, Height = size * 0.3, Fill = brush };
                Canvas.SetLeft(stem, x - size * 0.2);
                Canvas.SetTop(stem, y + size * 0.6);
                RadarCanvas.Children.Add(stem);
            }
            else 
            {
                Ellipse dot = new Ellipse { Width = size * 1.6, Height = size * 1.6, Fill = brush };
                Canvas.SetLeft(dot, x - size * 0.8);
                Canvas.SetTop(dot, y - size * 0.8);
                RadarCanvas.Children.Add(dot);
            }
        }
    }
}
