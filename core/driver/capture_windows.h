#ifndef OD_CAPTURE_WINDOWS_H
#define OD_CAPTURE_WINDOWS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    float* buffer;
    uint32_t num_samples; 
    uint32_t channels;    
    uint32_t sample_rate;
} AudioBuffer_t;


__declspec(dllexport) int OD_Capture_Init(int channels);
/* Capture from a specific playback device (NULL = default) and, when output_id is given,
 * play a stereo fold-down of what is captured on that other device. Used when the game
 * renders into a virtual surround device: the radar gets every channel, the ears get stereo. */
__declspec(dllexport) int OD_Capture_InitDevices(const wchar_t* capture_id, const wchar_t* output_id);
__declspec(dllexport) int OD_Capture_IsForwarding(void);
__declspec(dllexport) int OD_Capture_Start(void);
__declspec(dllexport) void OD_Capture_Stop(void);
__declspec(dllexport) AudioBuffer_t* OD_Capture_GetLatestBuffer(void);
/* Non-zero once the capture device was invalidated (unplugged / default changed);
 * call OD_Capture_Stop then OD_Capture_Init to recover. */
__declspec(dllexport) int OD_Capture_IsDeviceLost(void);
/* Channels in the output device's own mix format (2 = stereo, 6 = 5.1, 8 = 7.1),
 * valid after a successful OD_Capture_Init. Direction behind the listener needs 6 or more. */
__declspec(dllexport) int OD_Capture_GetDeviceChannels(void);
/* Changes whenever new audio has been captured. Polling faster than packets arrive (every
 * 10 ms) would otherwise re-analyse an identical window. */
__declspec(dllexport) int OD_Capture_GetSequence(void);


#ifdef __cplusplus
}
#endif

#endif 
