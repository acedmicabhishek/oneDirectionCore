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

        /* Device info matching C structs */
        public const int OD_DEVICE_MAX_NAME = 128;
        public const int OD_DEVICE_MAX_ID = 256;
        public const int OD_MAX_DEVICES = 16;

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        public struct OD_DeviceInfo
        {
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
            public string Name;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
            public string Id;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        public struct OD_DeviceList
        {
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)]
            public OD_DeviceInfo[] Devices;
            public int Count;
        }

        
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int OD_Capture_Init(int channels);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int OD_Capture_Start();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void OD_Capture_Stop();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr OD_Capture_GetLatestBuffer();

        /* Device enumeration and selection */
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int OD_Capture_EnumRenderDevices(ref OD_DeviceList outList);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        public static extern int OD_Capture_SetRenderDeviceId([MarshalAs(UnmanagedType.LPWStr)] string deviceId);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void OD_Capture_SetVolumeMultiplier(float multiplier);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        public static extern int OD_Capture_SetCaptureDeviceByName([MarshalAs(UnmanagedType.LPWStr)] string substringMatch);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        public static extern int OD_Capture_FindVBCable([MarshalAs(UnmanagedType.LPWStr)] System.Text.StringBuilder outId, int maxLen);

        
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
