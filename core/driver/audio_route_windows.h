#ifndef OD_AUDIO_ROUTE_WINDOWS_H
#define OD_AUDIO_ROUTE_WINDOWS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/* Surround routing.
 *
 * A game only mixes in 7.1 when the default playback device is a 7.1 device. Most headsets
 * are stereo, so the default device is switched to an installed virtual 7.1 device (for
 * example the FxSound or VB-Cable driver), the engine captures every channel from it, and
 * OD_Capture_InitDevices plays a stereo fold-down on the device the user listens to.
 */

#define OD_ROUTE_BLOCKED   (-1) /* a virtual device exists, but something keeps forcing it back to stereo */
#define OD_ROUTE_NONE        0  /* no virtual 7.1 device, or nothing to listen on: stereo only */
#define OD_ROUTE_NATIVE      1  /* the default device is already surround: capture it as is */
#define OD_ROUTE_VIRTUAL     2  /* routed: capture OD_Route_CaptureId, forward to OD_Route_OutputId */

/* preferred_output_name: friendly name of the device to listen on, or NULL/empty for automatic.
 * Calling it again while routed re-evaluates the route in place: a real device that has become
 * the Windows default is taken as the new device to listen on. */
__declspec(dllexport) int OD_Route_Prepare(const wchar_t* preferred_output_name);
/* Non-zero when the route no longer matches the system: the Windows output was switched away
 * from the virtual device, or headphones were plugged in. Stop capture and call
 * OD_Route_Prepare again. Cheap enough to poll about once a second. */
__declspec(dllexport) int OD_Route_NeedsUpdate(void);
/* Undo what OD_Route_Prepare changed (device format, default device). Safe to call when nothing is routed. */
__declspec(dllexport) void OD_Route_Restore(void);
/* Same, from values saved to disk, for recovering after a crash while routed. */
__declspec(dllexport) void OD_Route_RestoreSaved(const wchar_t* virtual_id, const wchar_t* previous_default_id);

__declspec(dllexport) const wchar_t* OD_Route_CaptureId(void);
__declspec(dllexport) const wchar_t* OD_Route_CaptureName(void);
__declspec(dllexport) const wchar_t* OD_Route_OutputId(void);
__declspec(dllexport) const wchar_t* OD_Route_OutputName(void);
/* Empty when the default device was left alone. */
__declspec(dllexport) const wchar_t* OD_Route_PreviousDefaultId(void);

/* Newline-separated friendly names of the real (non-virtual) playback devices. Returns the count. */
__declspec(dllexport) int OD_Route_ListOutputs(wchar_t* buffer, int buffer_chars);

#ifdef __cplusplus
}
#endif

#endif
