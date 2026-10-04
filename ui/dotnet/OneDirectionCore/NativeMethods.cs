using System;
using System.Runtime.InteropServices;

namespace OneDirectionCore
{
    internal static class NativeMethods
    {
        private const string DllName = "od_core.dll";

        [StructLayout(LayoutKind.Sequential)]
        public struct AudioBuffer
        {
            public IntPtr Buffer;
            public uint NumSamples;
            public uint Channels;
            public uint SampleRate;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct SoundEntity
        {
            public float AzimuthAngle;
            public float Distance;
            public int SignatureMatchId;
            public float Confidence;
            public int SoundType;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct SpatialData
        {
            public SoundEntity E0, E1, E2, E3, E4, E5, E6, E7, E8, E9;
            public int EntityCount;

            public SoundEntity[] GetEntities()
            {
                return new[] { E0, E1, E2, E3, E4, E5, E6, E7, E8, E9 };
            }
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct ClassResult
        {
            public int Type;
            public float Confidence;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct SpectralFeatures
        {
            public float Energy;
            public float SpectralCentroid;
            public float SpectralSpread;
            public float HighFreqRatio;
            public float LowFreqRatio;
            public float MidFreqRatio;
            public float Transient;
            public float ZeroCrossingRate;
        }

        
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int OD_Capture_Init(int channels);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int OD_Capture_Start();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void OD_Capture_Stop();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr OD_Capture_GetLatestBuffer();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int OD_Capture_IsDeviceLost();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int OD_Capture_GetDeviceChannels();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int OD_Capture_InitDevices([MarshalAs(UnmanagedType.LPWStr)] string? captureId,
                                                        [MarshalAs(UnmanagedType.LPWStr)] string? outputId);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int OD_Capture_IsForwarding();

        // Surround routing; values mirror audio_route_windows.h
        public const int RouteBlocked = -1;
        public const int RouteNone = 0;
        public const int RouteNative = 1;
        public const int RouteVirtual = 2;

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int OD_Route_Prepare([MarshalAs(UnmanagedType.LPWStr)] string? preferredOutputName);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void OD_Route_Restore();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void OD_Route_RestoreSaved([MarshalAs(UnmanagedType.LPWStr)] string virtualId,
                                                        [MarshalAs(UnmanagedType.LPWStr)] string previousDefaultId);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr OD_Route_CaptureId();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr OD_Route_CaptureName();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr OD_Route_OutputId();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr OD_Route_OutputName();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr OD_Route_PreviousDefaultId();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        private static extern int OD_Route_ListOutputs(System.Text.StringBuilder buffer, int bufferChars);

        public static string RouteCaptureId => Marshal.PtrToStringUni(OD_Route_CaptureId()) ?? "";
        public static string RouteCaptureName => Marshal.PtrToStringUni(OD_Route_CaptureName()) ?? "";
        public static string RouteOutputId => Marshal.PtrToStringUni(OD_Route_OutputId()) ?? "";
        public static string RouteOutputName => Marshal.PtrToStringUni(OD_Route_OutputName()) ?? "";
        public static string RoutePreviousDefaultId => Marshal.PtrToStringUni(OD_Route_PreviousDefaultId()) ?? "";

        public static string[] RouteListOutputs()
        {
            var buffer = new System.Text.StringBuilder(4096);
            int count = OD_Route_ListOutputs(buffer, buffer.Capacity);
            return count > 0 ? buffer.ToString().Split('\n') : Array.Empty<string>();
        }

        
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern SpatialData OD_DSP_ProcessBuffer(IntPtr buffer, float sensitivity, float separation);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int OD_DSP_LoadSignature(int id, [MarshalAs(UnmanagedType.LPStr)] string filePath);

        
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void OD_Classifier_Init();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void OD_Classifier_SetPreset([MarshalAs(UnmanagedType.LPStr)] string presetName);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr OD_Classifier_TypeName(int type);

        
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int OD_Hardware_Init([MarshalAs(UnmanagedType.LPStr)] string comPort, int baudRate);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int OD_Hardware_SendDirectionLog(float azimuth);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void OD_Hardware_Close();
    }
}
