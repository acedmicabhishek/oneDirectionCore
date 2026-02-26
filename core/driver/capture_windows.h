#ifndef OD_CAPTURE_WINDOWS_H
#define OD_CAPTURE_WINDOWS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float* buffer;
    uint32_t num_samples; 
    uint32_t channels;    
    uint32_t sample_rate;
} AudioBuffer_t;

#define OD_DEVICE_MAX_NAME 128
#define OD_DEVICE_MAX_ID   256
#define OD_MAX_DEVICES     16

typedef struct {
    wchar_t name[OD_DEVICE_MAX_NAME];
    wchar_t id[OD_DEVICE_MAX_ID];
} OD_DeviceInfo;

typedef struct {
    OD_DeviceInfo devices[OD_MAX_DEVICES];
    int count;
} OD_DeviceList;


__declspec(dllexport) int OD_Capture_Init(int channels);
__declspec(dllexport) int OD_Capture_Start(void);
__declspec(dllexport) void OD_Capture_Stop(void);
__declspec(dllexport) AudioBuffer_t* OD_Capture_GetLatestBuffer(void);


__declspec(dllexport) int OD_Capture_EnumRenderDevices(OD_DeviceList* outList);
__declspec(dllexport) int OD_Capture_SetRenderDeviceId(const wchar_t* deviceId);
__declspec(dllexport) void OD_Capture_SetVolumeMultiplier(float multiplier);
__declspec(dllexport) int OD_Capture_SetCaptureDeviceByName(const wchar_t* substringMatch);
__declspec(dllexport) int OD_Capture_FindVBCable(wchar_t* outId, int maxLen);


#ifdef __cplusplus
}
#endif

#endif 
