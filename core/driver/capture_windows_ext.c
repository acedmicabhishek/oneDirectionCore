#ifdef _WIN32
#include "capture_windows.h"
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <endpointvolume.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>

#ifndef WAVE_FORMAT_IEEE_FLOAT
#define WAVE_FORMAT_IEEE_FLOAT 0x0003
#endif
#ifndef AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
#define AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM 0x80000000
#endif
#ifndef AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY
#define AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY 0x08000000
#endif

/* Frames kept for the DSP. Must match FFT_SIZE in dsp_windows.c. */
#define WINDOW_FRAMES 512

static const GUID _KSDATAFORMAT_SUBTYPE_IEEE_FLOAT = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
static const GUID _KSDATAFORMAT_SUBTYPE_PCM = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

typedef enum {
    FMT_UNSUPPORTED = 0,
    FMT_FLOAT32,
    FMT_PCM16,
    FMT_PCM24,
    FMT_PCM32
} SampleFormat_t;

static IMMDeviceEnumerator *pEnumerator = NULL;
static IMMDevice *pDevice = NULL;
static IAudioClient *pAudioClient = NULL;
static IAudioCaptureClient *pCaptureClient = NULL;
static WAVEFORMATEX *pFormat = NULL;
static SampleFormat_t sample_format = FMT_UNSUPPORTED;
/* Channel count of the device's own mix, i.e. what applications actually render. */
static volatile LONG device_channels = 0;

/* Sliding window holding the most recent WINDOW_FRAMES frames, oldest first. */
static AudioBuffer_t latest_buffer = {0};
static AudioBuffer_t ui_buffer = {0};
static ULONGLONG last_packet_ms = 0;
/* Bumped for every captured packet, so callers can tell whether the window changed. */
static volatile LONG packet_sequence = 0;

/* Loopback delivers no packets while nothing is playing; after this long the
 * window is treated as silence instead of being re-analysed forever. */
#define STALE_MS 100

/* ── Forwarding ──
 * When the game renders into a virtual surround device nobody can hear, the captured
 * audio is folded down to stereo and played on the device the user listens to. */
static IMMDevice *pOutDevice = NULL;
static IAudioClient *pOutClient = NULL;
static IAudioRenderClient *pRenderClient = NULL;
static IAudioEndpointVolume *pCaptureVolume = NULL;
static IAudioEndpointVolume *pOutVolume = NULL;
static UINT32 out_buffer_frames = 0;
static float mirrored_volume = -1.0f;
static BOOL mirrored_mute = FALSE;
static ULONGLONG last_mirror_ms = 0;
/* Capture-thread scratch holding the current packet as float. */
static float *packet_buf = NULL;
static UINT32 packet_cap = 0;

#define FORWARD_CUSHION_MS 30   /* silence queued ahead after a gap, so jitter does not underrun */
#define FORWARD_STARVED_MS 5    /* below this the output is treated as having run dry */
#define FORWARD_MAX_MS 90       /* above this the two device clocks have drifted: drop a packet */
#define FORWARD_DOWNMIX_GAIN 0.75f

static HANDLE capture_thread = NULL;
static volatile LONG running = 0;
static volatile LONG device_lost = 0;
static CRITICAL_SECTION buffer_cs;
static volatile LONG cs_ready = 0;
static int com_owned = 0;

static void ensure_cs(void) {
    if (InterlockedCompareExchange(&cs_ready, 1, 0) == 0) {
        InitializeCriticalSection(&buffer_cs);
    }
}

static SampleFormat_t detect_format(const WAVEFORMATEX *fmt) {
    int is_float = 0, is_pcm = 0;
    if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const WAVEFORMATEXTENSIBLE *ex = (const WAVEFORMATEXTENSIBLE*)fmt;
        is_float = IsEqualGUID(&ex->SubFormat, &_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
        is_pcm = IsEqualGUID(&ex->SubFormat, &_KSDATAFORMAT_SUBTYPE_PCM);
    } else {
        is_float = (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
        is_pcm = (fmt->wFormatTag == WAVE_FORMAT_PCM);
    }

    if (is_float && fmt->wBitsPerSample == 32) return FMT_FLOAT32;
    if (is_pcm) {
        if (fmt->wBitsPerSample == 16) return FMT_PCM16;
        if (fmt->wBitsPerSample == 24) return FMT_PCM24;
        if (fmt->wBitsPerSample == 32) return FMT_PCM32;
    }
    return FMT_UNSUPPORTED;
}

static void convert_samples(float *dst, const BYTE *src, UINT32 sample_count) {
    switch (sample_format) {
    case FMT_FLOAT32:
        memcpy(dst, src, sample_count * sizeof(float));
        break;
    case FMT_PCM16: {
        const short *s = (const short*)src;
        for (UINT32 i = 0; i < sample_count; i++) dst[i] = s[i] / 32768.0f;
        break;
    }
    case FMT_PCM24:
        for (UINT32 i = 0; i < sample_count; i++) {
            const BYTE *p = src + i * 3;
            int32_t v = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24);
            dst[i] = v / 2147483648.0f;
        }
        break;
    case FMT_PCM32: {
        const int32_t *s = (const int32_t*)src;
        for (UINT32 i = 0; i < sample_count; i++) dst[i] = s[i] / 2147483648.0f;
        break;
    }
    default:
        memset(dst, 0, sample_count * sizeof(float));
        break;
    }
}

/* The volume keys act on the captured (default) device, which is not the one being heard,
 * so changes to its volume and mute are copied onto the listening device. */
static void mirror_volume(void) {
    float volume = 0.0f;
    BOOL mute = FALSE;
    if (!pCaptureVolume || !pOutVolume) return;
    if (SUCCEEDED(pCaptureVolume->lpVtbl->GetMasterVolumeLevelScalar(pCaptureVolume, &volume)) &&
        fabsf(volume - mirrored_volume) > 0.0005f) {
        pOutVolume->lpVtbl->SetMasterVolumeLevelScalar(pOutVolume, volume, NULL);
        mirrored_volume = volume;
    }
    if (SUCCEEDED(pCaptureVolume->lpVtbl->GetMute(pCaptureVolume, &mute)) && mute != mirrored_mute) {
        pOutVolume->lpVtbl->SetMute(pOutVolume, mute, NULL);
        mirrored_mute = mute;
    }
}

static void downmix_to_stereo(float *out, const float *in, UINT32 frames, UINT32 ch) {
    const float c = 0.7071f;
    for (UINT32 i = 0; i < frames; i++) {
        const float *f = in + (size_t)i * ch;
        float l, r;
        if (ch >= 8) {          /* FL FR FC LFE BL BR SL SR */
            l = f[0] + c * f[2] + 0.5f * f[3] + c * f[4] + c * f[6];
            r = f[1] + c * f[2] + 0.5f * f[3] + c * f[5] + c * f[7];
        } else if (ch >= 6) {   /* FL FR FC LFE BL BR */
            l = f[0] + c * f[2] + 0.5f * f[3] + c * f[4];
            r = f[1] + c * f[2] + 0.5f * f[3] + c * f[5];
        } else if (ch >= 2) {
            l = f[0];
            r = f[1];
        } else {
            l = r = f[0];
        }
        if (ch >= 6) { l *= FORWARD_DOWNMIX_GAIN; r *= FORWARD_DOWNMIX_GAIN; }
        if (l > 1.0f) l = 1.0f; else if (l < -1.0f) l = -1.0f;
        if (r > 1.0f) r = 1.0f; else if (r < -1.0f) r = -1.0f;
        out[i * 2] = l;
        out[i * 2 + 1] = r;
    }
}

/* Play one captured packet on the listening device. data == NULL means silence. */
static void forward_frames(const float *data, UINT32 frames) {
    if (!pRenderClient || frames == 0) return;

    UINT32 padding = 0;
    HRESULT hr = pOutClient->lpVtbl->GetCurrentPadding(pOutClient, &padding);
    if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
        /* The listening device was unplugged; let the caller restart and pick another. */
        InterlockedExchange(&device_lost, 1);
        return;
    }
    if (FAILED(hr)) return;

    UINT32 rate = pFormat->nSamplesPerSec;
    if (padding > rate / 1000 * FORWARD_MAX_MS) return;

    UINT32 lead = padding < rate / 1000 * FORWARD_STARVED_MS ? rate / 1000 * FORWARD_CUSHION_MS : 0;
    UINT32 avail = out_buffer_frames - padding;
    if (frames > avail) frames = avail;
    if (lead > avail - frames) lead = avail - frames;
    if (lead + frames == 0) return;

    BYTE *dst = NULL;
    if (FAILED(pRenderClient->lpVtbl->GetBuffer(pRenderClient, lead + frames, &dst))) return;
    float *out = (float*)dst;
    memset(out, 0, (size_t)lead * 2 * sizeof(float));
    out += (size_t)lead * 2;

    if (data) downmix_to_stereo(out, data, frames, latest_buffer.channels);
    else memset(out, 0, (size_t)frames * 2 * sizeof(float));
    pRenderClient->lpVtbl->ReleaseBuffer(pRenderClient, lead + frames, 0);
}

/* Append a packet (already float) to the sliding window. data == NULL means silence. */
static void push_frames(const float *data, UINT32 frames) {
    UINT32 ch = latest_buffer.channels;
    UINT32 filled = latest_buffer.num_samples;
    UINT32 skip = 0, keep, count;

    if (frames >= WINDOW_FRAMES) {
        skip = frames - WINDOW_FRAMES;
        keep = 0;
        count = WINDOW_FRAMES;
    } else {
        keep = WINDOW_FRAMES - frames;
        if (keep > filled) keep = filled;
        count = frames;
        if (keep > 0 && keep < filled) {
            memmove(latest_buffer.buffer,
                    latest_buffer.buffer + (size_t)(filled - keep) * ch,
                    (size_t)keep * ch * sizeof(float));
        }
    }

    float *dst = latest_buffer.buffer + (size_t)keep * ch;
    if (data) {
        memcpy(dst, data + (size_t)skip * ch, (size_t)count * ch * sizeof(float));
    } else {
        memset(dst, 0, (size_t)count * ch * sizeof(float));
    }
    latest_buffer.num_samples = keep + count;
    last_packet_ms = GetTickCount64();
    InterlockedIncrement(&packet_sequence);
}

/* Convert the device packet to float in the capture thread's scratch buffer. */
static const float *packet_as_float(const BYTE *data, UINT32 frames) {
    UINT32 samples = frames * pFormat->nChannels;
    if (samples > packet_cap) {
        float *grown = (float*)realloc(packet_buf, (size_t)samples * sizeof(float));
        if (!grown) return NULL;
        packet_buf = grown;
        packet_cap = samples;
    }
    convert_samples(packet_buf, data, samples);
    return packet_buf;
}

static DWORD WINAPI CaptureThreadProc(LPVOID lpParam) {
    (void)lpParam;
    while (running) {
        /* Runs on a timer rather than per packet so the keys still work while nothing is playing. */
        if (pOutVolume && GetTickCount64() - last_mirror_ms >= 100) {
            mirror_volume();
            last_mirror_ms = GetTickCount64();
        }

        UINT32 packetLength = 0;
        HRESULT hr = pCaptureClient->lpVtbl->GetNextPacketSize(pCaptureClient, &packetLength);
        if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
            /* Default device changed or was unplugged; the client is dead. */
            InterlockedExchange(&device_lost, 1);
            break;
        }
        if (FAILED(hr)) { Sleep(1); continue; }

        while (packetLength > 0) {
            BYTE *pData = NULL;
            UINT32 numFrames = 0;
            DWORD flags = 0;

            hr = pCaptureClient->lpVtbl->GetBuffer(pCaptureClient, &pData, &numFrames, &flags, NULL, NULL);
            if (FAILED(hr)) break;

            if (numFrames > 0) {
                int silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) || pData == NULL;
                /* A failed allocation degrades to silence for this packet. */
                const float *samples = silent ? NULL : packet_as_float(pData, numFrames);
                EnterCriticalSection(&buffer_cs);
                push_frames(samples, numFrames);
                LeaveCriticalSection(&buffer_cs);
                forward_frames(samples, numFrames);
            }

            pCaptureClient->lpVtbl->ReleaseBuffer(pCaptureClient, numFrames);
            hr = pCaptureClient->lpVtbl->GetNextPacketSize(pCaptureClient, &packetLength);
            if (FAILED(hr)) break;
        }
        Sleep(1);
    }
    return 0;
}

/* Release everything acquired by OD_Capture_Init. Safe to call repeatedly. */
static void release_all(void) {
    if (pRenderClient) { pRenderClient->lpVtbl->Release(pRenderClient); pRenderClient = NULL; }
    if (pOutClient) { pOutClient->lpVtbl->Release(pOutClient); pOutClient = NULL; }
    if (pOutDevice) { pOutDevice->lpVtbl->Release(pOutDevice); pOutDevice = NULL; }
    if (pCaptureVolume) { pCaptureVolume->lpVtbl->Release(pCaptureVolume); pCaptureVolume = NULL; }
    if (pOutVolume) { pOutVolume->lpVtbl->Release(pOutVolume); pOutVolume = NULL; }
    out_buffer_frames = 0;
    free(packet_buf);
    packet_buf = NULL;
    packet_cap = 0;

    if (pCaptureClient) { pCaptureClient->lpVtbl->Release(pCaptureClient); pCaptureClient = NULL; }
    if (pAudioClient) { pAudioClient->lpVtbl->Release(pAudioClient); pAudioClient = NULL; }
    if (pDevice) { pDevice->lpVtbl->Release(pDevice); pDevice = NULL; }
    if (pEnumerator) { pEnumerator->lpVtbl->Release(pEnumerator); pEnumerator = NULL; }
    if (pFormat) { CoTaskMemFree(pFormat); pFormat = NULL; }
    sample_format = FMT_UNSUPPORTED;

    EnterCriticalSection(&buffer_cs);
    free(latest_buffer.buffer);
    free(ui_buffer.buffer);
    memset(&latest_buffer, 0, sizeof(latest_buffer));
    memset(&ui_buffer, 0, sizeof(ui_buffer));
    LeaveCriticalSection(&buffer_cs);

    if (com_owned) {
        CoUninitialize();
        com_owned = 0;
    }
}

static int fail_init(HRESULT hr) {
    release_all();
    return (int)hr;
}

/* Open the stereo stream on the listening device. Never forwards into the device being
 * captured: that would feed the output straight back into the capture. */
static HRESULT open_forwarding(const wchar_t *output_id) {
    LPWSTR capture_id = NULL;
    HRESULT hr = pDevice->lpVtbl->GetId(pDevice, &capture_id);
    if (FAILED(hr)) return hr;
    int same = wcscmp(capture_id, output_id) == 0;
    CoTaskMemFree(capture_id);
    if (same) return E_INVALIDARG;

    hr = pEnumerator->lpVtbl->GetDevice(pEnumerator, output_id, &pOutDevice);
    if (FAILED(hr)) return hr;
    hr = pOutDevice->lpVtbl->Activate(pOutDevice, &IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&pOutClient);
    if (FAILED(hr)) return hr;

    /* Stereo float at the capture rate; Windows converts to whatever the device runs at. */
    WAVEFORMATEXTENSIBLE fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    fmt.Format.nChannels = 2;
    fmt.Format.nSamplesPerSec = pFormat->nSamplesPerSec;
    fmt.Format.wBitsPerSample = 32;
    fmt.Format.nBlockAlign = 8;
    fmt.Format.nAvgBytesPerSec = fmt.Format.nSamplesPerSec * 8;
    fmt.Format.cbSize = 22;
    fmt.Samples.wValidBitsPerSample = 32;
    fmt.dwChannelMask = 0x3;
    fmt.SubFormat = _KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

    hr = pOutClient->lpVtbl->Initialize(pOutClient, AUDCLNT_SHAREMODE_SHARED,
                                        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                        2000000, 0, &fmt.Format, NULL);
    if (FAILED(hr)) return hr;
    hr = pOutClient->lpVtbl->GetBufferSize(pOutClient, &out_buffer_frames);
    if (FAILED(hr)) return hr;
    hr = pOutClient->lpVtbl->GetService(pOutClient, &IID_IAudioRenderClient, (void**)&pRenderClient);
    if (FAILED(hr)) return hr;

    /* Optional: without these the volume keys just stop affecting what is heard.
     * Start from the listening device's level, so nothing gets louder when routing begins. */
    pDevice->lpVtbl->Activate(pDevice, &IID_IAudioEndpointVolume, CLSCTX_ALL, NULL, (void**)&pCaptureVolume);
    pOutDevice->lpVtbl->Activate(pOutDevice, &IID_IAudioEndpointVolume, CLSCTX_ALL, NULL, (void**)&pOutVolume);
    mirrored_volume = -1.0f;
    mirrored_mute = FALSE;
    if (pCaptureVolume && pOutVolume) {
        float volume = 0.0f;
        BOOL mute = FALSE;
        if (SUCCEEDED(pOutVolume->lpVtbl->GetMasterVolumeLevelScalar(pOutVolume, &volume))) {
            pCaptureVolume->lpVtbl->SetMasterVolumeLevelScalar(pCaptureVolume, volume, NULL);
            mirrored_volume = volume;
        }
        if (SUCCEEDED(pOutVolume->lpVtbl->GetMute(pOutVolume, &mute))) {
            pCaptureVolume->lpVtbl->SetMute(pCaptureVolume, mute, NULL);
            mirrored_mute = mute;
        }
    }
    last_mirror_ms = 0;
    return S_OK;
}

static int init_internal(const wchar_t *capture_id, int channels, const wchar_t *output_id);

int OD_Capture_Init(int channels) {
    return init_internal(NULL, channels, NULL);
}

int OD_Capture_InitDevices(const wchar_t *capture_id, const wchar_t *output_id) {
    return init_internal(capture_id, 0, output_id);
}

int OD_Capture_IsForwarding(void) {
    return pRenderClient ? 1 : 0;
}

static int init_internal(const wchar_t *capture_id, int channels, const wchar_t *output_id) {
    ensure_cs();
    if (pAudioClient) OD_Capture_Stop();

    HRESULT hr;
    InterlockedExchange(&device_lost, 0);

    /* Only balance CoInitializeEx with CoUninitialize when it succeeded here;
     * RPC_E_CHANGED_MODE means the caller's thread already owns COM. */
    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr)) com_owned = 1;
    else if (hr != RPC_E_CHANGED_MODE) return (int)hr;

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void**)&pEnumerator);
    if (FAILED(hr)) return fail_init(hr);

    if (capture_id && capture_id[0]) {
        hr = pEnumerator->lpVtbl->GetDevice(pEnumerator, capture_id, &pDevice);
    } else {
        hr = pEnumerator->lpVtbl->GetDefaultAudioEndpoint(pEnumerator, eRender, eConsole, &pDevice);
    }
    if (FAILED(hr)) return fail_init(hr);

    hr = pDevice->lpVtbl->Activate(pDevice, &IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&pAudioClient);
    if (FAILED(hr)) return fail_init(hr);

    hr = pAudioClient->lpVtbl->GetMixFormat(pAudioClient, &pFormat);
    if (FAILED(hr)) return fail_init(hr);

    printf("[Capture Windows] System Mix Format: %u channels, %u Hz, %u bits/sample\n",
           pFormat->nChannels, pFormat->nSamplesPerSec, pFormat->wBitsPerSample);
    InterlockedExchange(&device_channels, pFormat->nChannels);

    /* If the user requested more channels than the mix format provides,
     * try to modify the format to request multi-channel capture.
     * This works when the Windows audio endpoint is configured for 7.1. */
    if (channels > (int)pFormat->nChannels && pFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        WAVEFORMATEXTENSIBLE *pEx = (WAVEFORMATEXTENSIBLE*)pFormat;
        uint32_t requested = (uint32_t)channels;

        /* Save original values */
        uint32_t orig_ch = pFormat->nChannels;
        DWORD orig_mask = pEx->dwChannelMask;

        /* Build the new channel mask */
        DWORD mask_71 = 0x63F; /* FL|FR|FC|LFE|BL|BR|SL|SR */
        DWORD mask_51 = 0x3F;  /* FL|FR|FC|LFE|BL|BR */

        if (requested >= 8) {
            pFormat->nChannels = 8;
            pEx->dwChannelMask = mask_71;
        } else if (requested >= 6) {
            pFormat->nChannels = 6;
            pEx->dwChannelMask = mask_51;
        }

        /* Recalculate block align and avg bytes */
        pFormat->nBlockAlign = pFormat->nChannels * (pFormat->wBitsPerSample / 8);
        pFormat->nAvgBytesPerSec = pFormat->nSamplesPerSec * pFormat->nBlockAlign;

        /* Check if the modified format is supported */
        WAVEFORMATEX *pClosest = NULL;
        hr = pAudioClient->lpVtbl->IsFormatSupported(pAudioClient, AUDCLNT_SHAREMODE_SHARED, pFormat, &pClosest);

        if (hr == S_OK) {
            printf("[Capture Windows] Multi-channel format (%u ch) accepted!\n", pFormat->nChannels);
            if (pClosest) CoTaskMemFree(pClosest);
        } else if (hr == S_FALSE && pClosest != NULL) {
            /* Windows suggested a closest match - use that instead */
            printf("[Capture Windows] Requested %u ch, Windows suggests %u ch\n",
                   pFormat->nChannels, pClosest->nChannels);
            CoTaskMemFree(pFormat);
            pFormat = pClosest;
        } else {
            /* Revert to original */
            printf("[Capture Windows] WARNING: %u-ch format not supported (hr=0x%08lX), falling back to %u ch\n",
                   pFormat->nChannels, (unsigned long)hr, orig_ch);
            if (pClosest) CoTaskMemFree(pClosest);
            pFormat->nChannels = (WORD)orig_ch;
            pEx->dwChannelMask = orig_mask;
            pFormat->nBlockAlign = pFormat->nChannels * (pFormat->wBitsPerSample / 8);
            pFormat->nAvgBytesPerSec = pFormat->nSamplesPerSec * pFormat->nBlockAlign;
        }
    }

    printf("[Capture Windows] Final Format: %u channels, %u Hz, %u bits/sample\n",
           pFormat->nChannels, pFormat->nSamplesPerSec, pFormat->wBitsPerSample);
    if (pFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        WAVEFORMATEXTENSIBLE *pEx = (WAVEFORMATEXTENSIBLE*)pFormat;
        printf("[Capture Windows] Channel mask: 0x%08lX\n", (unsigned long)pEx->dwChannelMask);
    }
    fflush(stdout);

    sample_format = detect_format(pFormat);
    if (sample_format == FMT_UNSUPPORTED || pFormat->nChannels == 0) {
        return fail_init(AUDCLNT_E_UNSUPPORTED_FORMAT);
    }

    hr = pAudioClient->lpVtbl->Initialize(pAudioClient, AUDCLNT_SHAREMODE_SHARED,
                                          AUDCLNT_STREAMFLAGS_LOOPBACK, 0, 0, pFormat, NULL);
    if (FAILED(hr)) return fail_init(hr);

    hr = pAudioClient->lpVtbl->GetService(pAudioClient, &IID_IAudioCaptureClient, (void**)&pCaptureClient);
    if (FAILED(hr)) return fail_init(hr);

    /* Both buffers are sized once here, so the capture thread never allocates. */
    size_t window_samples = (size_t)WINDOW_FRAMES * pFormat->nChannels;
    float *window = (float*)calloc(window_samples, sizeof(float));
    float *snapshot = (float*)calloc(window_samples, sizeof(float));
    if (!window || !snapshot) {
        free(window);
        free(snapshot);
        return fail_init(E_OUTOFMEMORY);
    }

    EnterCriticalSection(&buffer_cs);
    latest_buffer.buffer = window;
    latest_buffer.num_samples = 0;
    latest_buffer.channels = pFormat->nChannels;
    latest_buffer.sample_rate = pFormat->nSamplesPerSec;
    ui_buffer.buffer = snapshot;
    ui_buffer.num_samples = 0;
    ui_buffer.channels = pFormat->nChannels;
    ui_buffer.sample_rate = pFormat->nSamplesPerSec;
    LeaveCriticalSection(&buffer_cs);

    if (output_id && output_id[0]) {
        hr = open_forwarding(output_id);
        if (FAILED(hr)) return fail_init(hr);
    }

    return 1;
}

int OD_Capture_Start(void) {
    if (!pAudioClient || !pCaptureClient || capture_thread) return 0;

    HRESULT hr = pAudioClient->lpVtbl->Start(pAudioClient);
    if (FAILED(hr)) return 0;

    if (pOutClient && FAILED(pOutClient->lpVtbl->Start(pOutClient))) {
        pAudioClient->lpVtbl->Stop(pAudioClient);
        return 0;
    }

    InterlockedExchange(&running, 1);
    capture_thread = CreateThread(NULL, 0, CaptureThreadProc, NULL, 0, NULL);
    if (!capture_thread) {
        InterlockedExchange(&running, 0);
        if (pOutClient) pOutClient->lpVtbl->Stop(pOutClient);
        pAudioClient->lpVtbl->Stop(pAudioClient);
        return 0;
    }
    return 1;
}

void OD_Capture_Stop(void) {
    ensure_cs();
    InterlockedExchange(&running, 0);
    if (capture_thread) {
        /* The thread touches the COM objects, so it must be gone before release. */
        WaitForSingleObject(capture_thread, INFINITE);
        CloseHandle(capture_thread);
        capture_thread = NULL;
    }
    if (pOutClient) pOutClient->lpVtbl->Stop(pOutClient);
    if (pAudioClient) pAudioClient->lpVtbl->Stop(pAudioClient);
    release_all();
}

int OD_Capture_IsDeviceLost(void) {
    return device_lost ? 1 : 0;
}

int OD_Capture_GetDeviceChannels(void) {
    return (int)device_channels;
}

int OD_Capture_GetSequence(void) {
    return (int)packet_sequence;
}

AudioBuffer_t* OD_Capture_GetLatestBuffer(void) {
    if (!cs_ready) return NULL;

    AudioBuffer_t *out = NULL;
    EnterCriticalSection(&buffer_cs);
    if (latest_buffer.num_samples > 0 && GetTickCount64() - last_packet_ms > STALE_MS) {
        latest_buffer.num_samples = 0;
    }
    if (latest_buffer.buffer && ui_buffer.buffer && latest_buffer.num_samples > 0) {
        memcpy(ui_buffer.buffer, latest_buffer.buffer,
               (size_t)latest_buffer.num_samples * latest_buffer.channels * sizeof(float));
        ui_buffer.num_samples = latest_buffer.num_samples;
        ui_buffer.channels = latest_buffer.channels;
        ui_buffer.sample_rate = latest_buffer.sample_rate;
        out = &ui_buffer;
    }
    LeaveCriticalSection(&buffer_cs);

    return out;
}
#endif
