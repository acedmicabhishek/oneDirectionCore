#ifdef _WIN32
#include "capture_windows.h"
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <propsys.h>
#include <functiondiscoverykeys_devpkey.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>


static const GUID _KSDATAFORMAT_SUBTYPE_IEEE_FLOAT = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
static const GUID _KSDATAFORMAT_SUBTYPE_PCM = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

static IMMDeviceEnumerator *pEnumerator = NULL;
static IMMDevice *pDevice = NULL;
static IAudioClient *pAudioClient = NULL;
static IAudioCaptureClient *pCaptureClient = NULL;
static WAVEFORMATEX *pFormat = NULL;
static AudioBuffer_t latest_buffer = {0};
static UINT32 g_current_buf_size = 0;
static HANDLE capture_thread = NULL;
static volatile int running = 0;
static CRITICAL_SECTION buffer_cs;

static IMMDevice *pRenderDevice = NULL;
static IAudioClient *pRenderAudioClient = NULL;
static IAudioRenderClient *pRenderClient = NULL;
static WAVEFORMATEX *pRenderFormat = NULL;
static UINT32 renderBufferFrameCount = 0;

static wchar_t g_renderDeviceId[OD_DEVICE_MAX_ID] = {0};
static wchar_t g_captureDeviceId[OD_DEVICE_MAX_ID] = {0};
static float g_volume_multiplier = 2.0f;

void OD_Capture_SetVolumeMultiplier(float multiplier) {
    g_volume_multiplier = multiplier;
}

static void GetDeviceFriendlyName(IMMDevice *dev, wchar_t *outName, int maxLen) {
    outName[0] = L'\0';
    IPropertyStore *pProps = NULL;
    if (SUCCEEDED(dev->lpVtbl->OpenPropertyStore(dev, STGM_READ, &pProps))) {
        PROPVARIANT varName;
        PropVariantInit(&varName);
        if (SUCCEEDED(pProps->lpVtbl->GetValue(pProps, &PKEY_Device_FriendlyName, &varName))) {
            if (varName.vt == VT_LPWSTR && varName.pwszVal) {
                wcsncpy(outName, varName.pwszVal, maxLen - 1);
                outName[maxLen - 1] = L'\0';
            }
            PropVariantClear(&varName);
        }
        pProps->lpVtbl->Release(pProps);
    }
}

static DWORD WINAPI CaptureThreadProc(LPVOID lpParam) {
    (void)lpParam;
    while (running) {
        UINT32 packetLength = 0;
        HRESULT hr = pCaptureClient->lpVtbl->GetNextPacketSize(pCaptureClient, &packetLength);
        if (FAILED(hr)) { Sleep(1); continue; }

        while (packetLength > 0) {
            BYTE *pData = NULL;
            UINT32 numFrames = 0;
            DWORD flags = 0;

            hr = pCaptureClient->lpVtbl->GetBuffer(pCaptureClient, &pData, &numFrames, &flags, NULL, NULL);
            if (FAILED(hr)) break;

            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && pData && numFrames > 0) {
                UINT32 channels = pFormat->nChannels;
                UINT32 sample_count = numFrames * channels;
                UINT32 byte_count = sample_count * sizeof(float);

                EnterCriticalSection(&buffer_cs);
                if (latest_buffer.buffer == NULL || g_current_buf_size < byte_count) {
                    float* new_buf = (float*)realloc(latest_buffer.buffer, byte_count);
                    if (new_buf) {
                        latest_buffer.buffer = new_buf;
                        g_current_buf_size = byte_count;
                    }
                }

                if (latest_buffer.buffer) {
                    if (pFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
                        WAVEFORMATEXTENSIBLE *pEx = (WAVEFORMATEXTENSIBLE*)pFormat;
                        if (IsEqualGUID(&pEx->SubFormat, &_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
                            memcpy(latest_buffer.buffer, pData, byte_count);
                        } else if (IsEqualGUID(&pEx->SubFormat, &_KSDATAFORMAT_SUBTYPE_PCM)) {
                            if (pFormat->wBitsPerSample == 16) {
                                short *src = (short*)pData;
                                for (UINT32 i = 0; i < sample_count; i++) latest_buffer.buffer[i] = src[i] / 32768.0f;
                            } else if (pFormat->wBitsPerSample == 32) {
                                int32_t *src = (int32_t*)pData;
                                for (UINT32 i = 0; i < sample_count; i++) latest_buffer.buffer[i] = src[i] / 2147483648.0f;
                            }
                        }
                    } else {
                        if (pFormat->wBitsPerSample == 32) memcpy(latest_buffer.buffer, pData, byte_count);
                        else if (pFormat->wBitsPerSample == 16) {
                            short *src = (short*)pData;
                            for (UINT32 i = 0; i < sample_count; i++) latest_buffer.buffer[i] = src[i] / 32768.0f;
                        }
                    }
                    latest_buffer.num_samples = numFrames;
                    latest_buffer.channels = channels;
                    latest_buffer.sample_rate = pFormat->nSamplesPerSec;
                    LeaveCriticalSection(&buffer_cs);

                    // AUDIO FORWARDING
                    if (pRenderClient && pRenderFormat) {
                        UINT32 padding = 0;
                        if (SUCCEEDED(pRenderAudioClient->lpVtbl->GetCurrentPadding(pRenderAudioClient, &padding))) {
                            UINT32 availableSpace = renderBufferFrameCount - padding;
                            if (availableSpace >= numFrames) {
                                BYTE *pRenderData = NULL;
                                if (SUCCEEDED(pRenderClient->lpVtbl->GetBuffer(pRenderClient, numFrames, &pRenderData))) {
                                    UINT32 outChannels = pRenderFormat->nChannels;
                                    UINT32 inChannels = latest_buffer.channels;
                                    
                                    int isFloat = 0;
                                    if (pRenderFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
                                        WAVEFORMATEXTENSIBLE *pEx = (WAVEFORMATEXTENSIBLE*)pRenderFormat;
                                        if (IsEqualGUID(&pEx->SubFormat, &_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) isFloat = 1;
                                    } else if (pRenderFormat->wFormatTag == 3) { 
                                        isFloat = 1;
                                    }
                                    
                                    if (isFloat && pRenderFormat->wBitsPerSample == 32) {
                                        float *outBuf = (float*)pRenderData;
                                        for (UINT32 i = 0; i < numFrames; i++) {
                                            for (UINT32 c = 0; c < outChannels; c++) {
                                                float sample = 0;
                                                if (inChannels > 2) {
                                                    if (c == 0) { 
                                                        sample = latest_buffer.buffer[i * inChannels + 0];
                                                        if (inChannels >= 6) sample += latest_buffer.buffer[i * inChannels + 2] * 0.707f + latest_buffer.buffer[i * inChannels + 4];
                                                        if (inChannels >= 8) sample += latest_buffer.buffer[i * inChannels + 6];
                                                        sample *= (0.4f * g_volume_multiplier);
                                                    } else if (c == 1) { 
                                                        sample = latest_buffer.buffer[i * inChannels + 1];
                                                        if (inChannels >= 6) sample += latest_buffer.buffer[i * inChannels + 2] * 0.707f + latest_buffer.buffer[i * inChannels + 5];
                                                        if (inChannels >= 8) sample += latest_buffer.buffer[i * inChannels + 7];
                                                        sample *= (0.4f * g_volume_multiplier);
                                                    } else {
                                                        sample = 0.0f;
                                                    }
                                                } else {
                                                    sample = (c < inChannels) ? latest_buffer.buffer[i * inChannels + c] : 0.0f;
                                                }
                                                if (sample > 1.0f) sample = 1.0f;
                                                if (sample < -1.0f) sample = -1.0f;
                                                outBuf[i * outChannels + c] = sample;
                                            }
                                        }
                                    } else if (!isFloat && pRenderFormat->wBitsPerSample == 16) {
                                        short *outBuf16 = (short*)pRenderData;
                                        for (UINT32 i = 0; i < numFrames; i++) {
                                            for (UINT32 c = 0; c < outChannels; c++) {
                                                float sample = 0;
                                                if (inChannels > 2) {
                                                    if (c == 0) { 
                                                        sample = latest_buffer.buffer[i * inChannels + 0];
                                                        if (inChannels >= 6) sample += latest_buffer.buffer[i * inChannels + 2] * 0.707f + latest_buffer.buffer[i * inChannels + 4];
                                                        if (inChannels >= 8) sample += latest_buffer.buffer[i * inChannels + 6];
                                                        sample *= (0.4f * g_volume_multiplier);
                                                    } else if (c == 1) { 
                                                        sample = latest_buffer.buffer[i * inChannels + 1];
                                                        if (inChannels >= 6) sample += latest_buffer.buffer[i * inChannels + 2] * 0.707f + latest_buffer.buffer[i * inChannels + 5];
                                                        if (inChannels >= 8) sample += latest_buffer.buffer[i * inChannels + 7];
                                                        sample *= (0.4f * g_volume_multiplier);
                                                    } else {
                                                        sample = 0.0f;
                                                    }
                                                } else {
                                                    sample = (c < inChannels) ? latest_buffer.buffer[i * inChannels + c] : 0.0f;
                                                }
                                                if (sample > 1.0f) sample = 1.0f;
                                                if (sample < -1.0f) sample = -1.0f;
                                                outBuf16[i * outChannels + c] = (short)(sample * 32767.0f);
                                            }
                                        }
                                    } else {
                                        memset(pRenderData, 0, numFrames * pRenderFormat->nBlockAlign);
                                    }
                                    pRenderClient->lpVtbl->ReleaseBuffer(pRenderClient, numFrames, 0);
                                }
                            }
                        }
                    }

                    
                    static int log_counter = 0;
                    if (++log_counter >= 100) {
                        float sum_sq = 0;
                        EnterCriticalSection(&buffer_cs);
                        for (UINT32 i = 0; i < sample_count; i++) {
                            sum_sq += latest_buffer.buffer[i] * latest_buffer.buffer[i];
                        }
                        LeaveCriticalSection(&buffer_cs);
                        float rms = sqrtf(sum_sq / sample_count);
                        if (rms > 0.000001f) {
                            printf("[Capture Windows] Buffer: %u frames, %u ch, RMS: %.6f\n", numFrames, channels, rms);
                        }
                        log_counter = 0;
                    }
                }
            } else if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                if (latest_buffer.buffer) {
                    EnterCriticalSection(&buffer_cs);
                    memset(latest_buffer.buffer, 0, numFrames * pFormat->nChannels * sizeof(float));
                    latest_buffer.num_samples = numFrames;
                    LeaveCriticalSection(&buffer_cs);
                }
            }
            pCaptureClient->lpVtbl->ReleaseBuffer(pCaptureClient, numFrames);
            pCaptureClient->lpVtbl->GetNextPacketSize(pCaptureClient, &packetLength);
        }
        Sleep(1);
    }
    return 0;
}

int OD_Capture_Init(int channels) {
    InitializeCriticalSection(&buffer_cs);
    HRESULT hr;
    
    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return hr; 
    
    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void**)&pEnumerator);
    if (FAILED(hr)) return hr;

    if (g_captureDeviceId[0] != L'\0') {
        hr = pEnumerator->lpVtbl->GetDevice(pEnumerator, g_captureDeviceId, &pDevice);
        if (FAILED(hr)) {
            printf("[Capture Windows] WARNING: Specified capture device not found, falling back to default.\n");
            hr = pEnumerator->lpVtbl->GetDefaultAudioEndpoint(pEnumerator, eRender, eConsole, &pDevice);
        }
    } else {
        hr = pEnumerator->lpVtbl->GetDefaultAudioEndpoint(pEnumerator, eRender, eConsole, &pDevice);
    }
    if (FAILED(hr)) return hr;
    
    hr = pDevice->lpVtbl->Activate(pDevice, &IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&pAudioClient);
    if (FAILED(hr)) return hr;
    
    hr = pAudioClient->lpVtbl->GetMixFormat(pAudioClient, &pFormat);
    if (FAILED(hr)) return hr;

    printf("[Capture Windows] System Mix Format: %u channels, %u Hz, %u bits/sample\n",
           pFormat->nChannels, pFormat->nSamplesPerSec, pFormat->wBitsPerSample);

    /* If the user requested more channels than the mix format provides,
     * try to modify the format to request multi-channel capture.
     * This works when the Windows audio endpoint is configured for 7.1. */
    if (channels > (int)pFormat->nChannels && pFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        WAVEFORMATEXTENSIBLE *pEx = (WAVEFORMATEXTENSIBLE*)pFormat;
        uint32_t requested = (uint32_t)channels;

        /* Save original values */
        uint32_t orig_ch = pFormat->nChannels;

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
            pFormat->nChannels = (WORD)orig_ch;
            pFormat->nBlockAlign = pFormat->nChannels * (pFormat->wBitsPerSample / 8);
            pFormat->nAvgBytesPerSec = pFormat->nSamplesPerSec * pFormat->nBlockAlign;
            if (pFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
                WAVEFORMATEXTENSIBLE *pEx2 = (WAVEFORMATEXTENSIBLE*)pFormat;
                if (orig_ch == 2) pEx2->dwChannelMask = 0x3; /* FL|FR */
            }
        }
    }

    printf("[Capture Windows] Final Format: %u channels, %u Hz, %u bits/sample\n",
           pFormat->nChannels, pFormat->nSamplesPerSec, pFormat->wBitsPerSample);
    if (pFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        WAVEFORMATEXTENSIBLE *pEx = (WAVEFORMATEXTENSIBLE*)pFormat;
        printf("[Capture Windows] Channel mask: 0x%08lX\n", (unsigned long)pEx->dwChannelMask);
    }
    fflush(stdout);

    hr = pAudioClient->lpVtbl->Initialize(pAudioClient, AUDCLNT_SHAREMODE_SHARED,
                                          AUDCLNT_STREAMFLAGS_LOOPBACK, 0, 0, pFormat, NULL);
    if (FAILED(hr)) return hr; 

    hr = pAudioClient->lpVtbl->GetService(pAudioClient, &IID_IAudioCaptureClient, (void**)&pCaptureClient);
    if (FAILED(hr)) return hr;

    if (g_renderDeviceId[0] != L'\0') {
        pEnumerator->lpVtbl->GetDevice(pEnumerator, g_renderDeviceId, &pRenderDevice);
    } else {
        LPWSTR captureId = NULL;
        if (SUCCEEDED(pDevice->lpVtbl->GetId(pDevice, &captureId))) {
            IMMDeviceCollection *pCollection = NULL;
            if (SUCCEEDED(pEnumerator->lpVtbl->EnumAudioEndpoints(pEnumerator, eRender, DEVICE_STATE_ACTIVE, &pCollection))) {
                UINT count = 0;
                pCollection->lpVtbl->GetCount(pCollection, &count);
                for (UINT i = 0; i < count; i++) {
                    IMMDevice *pDev = NULL;
                    pCollection->lpVtbl->Item(pCollection, i, &pDev);
                    LPWSTR id = NULL;
                    pDev->lpVtbl->GetId(pDev, &id);
                    if (id && wcscmp(captureId, id) != 0) {
                        pRenderDevice = pDev;
                        CoTaskMemFree(id);
                        break;
                    }
                    if (id) CoTaskMemFree(id);
                    pDev->lpVtbl->Release(pDev);
                }
                pCollection->lpVtbl->Release(pCollection);
            }
            CoTaskMemFree(captureId);
        }
    }
    
    if (pRenderDevice) {
        wchar_t rname[128];
        GetDeviceFriendlyName(pRenderDevice, rname, 128);
        printf("[Capture Windows] Render device: %ls\n", rname);
        if (SUCCEEDED(pRenderDevice->lpVtbl->Activate(pRenderDevice, &IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&pRenderAudioClient))) {
            if (SUCCEEDED(pRenderAudioClient->lpVtbl->GetMixFormat(pRenderAudioClient, &pRenderFormat))) {
                if (SUCCEEDED(pRenderAudioClient->lpVtbl->Initialize(pRenderAudioClient, AUDCLNT_SHAREMODE_SHARED, 0, 0, 0, pRenderFormat, NULL))) {
                    pRenderAudioClient->lpVtbl->GetBufferSize(pRenderAudioClient, &renderBufferFrameCount);
                    pRenderAudioClient->lpVtbl->GetService(pRenderAudioClient, &IID_IAudioRenderClient, (void**)&pRenderClient);
                    printf("[Capture Windows] Audio forwarding enabled.\n");
                }
            }
        }
    }
    
    return 1;
}

int OD_Capture_Start(void) {
    if (pAudioClient) {
        pAudioClient->lpVtbl->Start(pAudioClient);
        running = 1;
        capture_thread = CreateThread(NULL, 0, CaptureThreadProc, NULL, 0, NULL);
        if (pRenderAudioClient) pRenderAudioClient->lpVtbl->Start(pRenderAudioClient);
    }
    return 1;
}

void OD_Capture_Stop(void) {
    running = 0;
    if (capture_thread) {
        WaitForSingleObject(capture_thread, 2000);
        CloseHandle(capture_thread);
        capture_thread = NULL;
    }
    if (pAudioClient) { pAudioClient->lpVtbl->Stop(pAudioClient); pAudioClient->lpVtbl->Release(pAudioClient); pAudioClient = NULL; }
    if (pRenderAudioClient) { pRenderAudioClient->lpVtbl->Stop(pRenderAudioClient); pRenderAudioClient->lpVtbl->Release(pRenderAudioClient); pRenderAudioClient = NULL; }
    if (pCaptureClient) { pCaptureClient->lpVtbl->Release(pCaptureClient); pCaptureClient = NULL; }
    if (pRenderClient) { pRenderClient->lpVtbl->Release(pRenderClient); pRenderClient = NULL; }
    if (pDevice) { pDevice->lpVtbl->Release(pDevice); pDevice = NULL; }
    if (pRenderDevice) { pRenderDevice->lpVtbl->Release(pRenderDevice); pRenderDevice = NULL; }
    if (pEnumerator) { pEnumerator->lpVtbl->Release(pEnumerator); pEnumerator = NULL; }
    if (pFormat) { CoTaskMemFree(pFormat); pFormat = NULL; }
    if (pRenderFormat) { CoTaskMemFree(pRenderFormat); pRenderFormat = NULL; }
    if (latest_buffer.buffer) { free(latest_buffer.buffer); latest_buffer.buffer = NULL; }
    g_current_buf_size = 0;
    renderBufferFrameCount = 0;
    DeleteCriticalSection(&buffer_cs);
    CoUninitialize();
}

static AudioBuffer_t ui_buffer = {0};

AudioBuffer_t* OD_Capture_GetLatestBuffer(void) {
    EnterCriticalSection(&buffer_cs);
    if (latest_buffer.buffer == NULL) {
        LeaveCriticalSection(&buffer_cs);
        return NULL;
    }
    
    
    UINT32 byte_count = latest_buffer.num_samples * latest_buffer.channels * sizeof(float);
    if (ui_buffer.buffer == NULL || ui_buffer.num_samples * ui_buffer.channels * sizeof(float) < byte_count) {
        float* new_buf = (float*)realloc(ui_buffer.buffer, byte_count);
        if (new_buf) {
            ui_buffer.buffer = new_buf;
        }
    }
    
    if (ui_buffer.buffer) {
        memcpy(ui_buffer.buffer, latest_buffer.buffer, byte_count);
        ui_buffer.num_samples = latest_buffer.num_samples;
        ui_buffer.channels = latest_buffer.channels;
        ui_buffer.sample_rate = latest_buffer.sample_rate;
    }
    LeaveCriticalSection(&buffer_cs);
    
    return ui_buffer.buffer ? &ui_buffer : NULL;
}

int OD_Capture_EnumRenderDevices(OD_DeviceList* outList) {
    if (!outList) return 0;
    outList->count = 0;

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    int needUninit = (SUCCEEDED(hr) && hr != RPC_E_CHANGED_MODE);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) { return 0; }

    IMMDeviceEnumerator *pEnum = NULL;
    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void**)&pEnum);
    if (FAILED(hr)) { if (needUninit) CoUninitialize(); return 0; }

    IMMDeviceCollection *pColl = NULL;
    hr = pEnum->lpVtbl->EnumAudioEndpoints(pEnum, eRender, DEVICE_STATE_ACTIVE, &pColl);
    if (FAILED(hr)) { pEnum->lpVtbl->Release(pEnum); if (needUninit) CoUninitialize(); return 0; }

    UINT count = 0;
    pColl->lpVtbl->GetCount(pColl, &count);

    for (UINT i = 0; i < count && outList->count < OD_MAX_DEVICES; i++) {
        IMMDevice *pDev = NULL;
        pColl->lpVtbl->Item(pColl, i, &pDev);
        if (!pDev) continue;

        LPWSTR id = NULL;
        pDev->lpVtbl->GetId(pDev, &id);

        OD_DeviceInfo *info = &outList->devices[outList->count];
        GetDeviceFriendlyName(pDev, info->name, OD_DEVICE_MAX_NAME);
        if (id) {
            wcsncpy(info->id, id, OD_DEVICE_MAX_ID - 1);
            info->id[OD_DEVICE_MAX_ID - 1] = L'\0';
            CoTaskMemFree(id);
        }
        outList->count++;
        pDev->lpVtbl->Release(pDev);
    }

    pColl->lpVtbl->Release(pColl);
    pEnum->lpVtbl->Release(pEnum);
    if (needUninit) CoUninitialize();
    return outList->count;
}

int OD_Capture_SetRenderDeviceId(const wchar_t* deviceId) {
    if (deviceId) {
        wcsncpy(g_renderDeviceId, deviceId, OD_DEVICE_MAX_ID - 1);
        g_renderDeviceId[OD_DEVICE_MAX_ID - 1] = L'\0';
    } else {
        g_renderDeviceId[0] = L'\0';
    }
    return 1;
}

int OD_Capture_SetCaptureDeviceByName(const wchar_t* substringMatch) {
    g_captureDeviceId[0] = L'\0';
    if (!substringMatch) return 0;

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    int needUninit = (SUCCEEDED(hr) && hr != RPC_E_CHANGED_MODE);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) { return 0; }

    IMMDeviceEnumerator *pEnum = NULL;
    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void**)&pEnum);
    if (FAILED(hr)) { if (needUninit) CoUninitialize(); return 0; }

    IMMDeviceCollection *pColl = NULL;
    hr = pEnum->lpVtbl->EnumAudioEndpoints(pEnum, eRender, DEVICE_STATE_ACTIVE, &pColl);
    if (FAILED(hr)) { pEnum->lpVtbl->Release(pEnum); if (needUninit) CoUninitialize(); return 0; }

    UINT count = 0;
    pColl->lpVtbl->GetCount(pColl, &count);
    int found = 0;

    for (UINT i = 0; i < count; i++) {
        IMMDevice *pDev = NULL;
        pColl->lpVtbl->Item(pColl, i, &pDev);
        if (!pDev) continue;

        wchar_t name[OD_DEVICE_MAX_NAME];
        GetDeviceFriendlyName(pDev, name, OD_DEVICE_MAX_NAME);

        if (wcsstr(name, substringMatch) != NULL) {
            LPWSTR id = NULL;
            pDev->lpVtbl->GetId(pDev, &id);
            if (id) {
                wcsncpy(g_captureDeviceId, id, OD_DEVICE_MAX_ID - 1);
                g_captureDeviceId[OD_DEVICE_MAX_ID - 1] = L'\0';
                CoTaskMemFree(id);
                found = 1;
            }
            pDev->lpVtbl->Release(pDev);
            break;
        }
        pDev->lpVtbl->Release(pDev);
    }

    pColl->lpVtbl->Release(pColl);
    pEnum->lpVtbl->Release(pEnum);
    if (needUninit) CoUninitialize();
    return found;
}

int OD_Capture_FindVBCable(wchar_t* outId, int maxLen) {
    if (!outId || maxLen <= 0) return 0;
    outId[0] = L'\0';

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    int needUninit = (SUCCEEDED(hr) && hr != RPC_E_CHANGED_MODE);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) { return 0; }

    IMMDeviceEnumerator *pEnum = NULL;
    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void**)&pEnum);
    if (FAILED(hr)) { if (needUninit) CoUninitialize(); return 0; }

    IMMDeviceCollection *pColl = NULL;
    hr = pEnum->lpVtbl->EnumAudioEndpoints(pEnum, eRender, DEVICE_STATE_ACTIVE, &pColl);
    if (FAILED(hr)) { pEnum->lpVtbl->Release(pEnum); if (needUninit) CoUninitialize(); return 0; }

    UINT count = 0;
    pColl->lpVtbl->GetCount(pColl, &count);
    int found = 0;

    for (UINT i = 0; i < count; i++) {
        IMMDevice *pDev = NULL;
        pColl->lpVtbl->Item(pColl, i, &pDev);
        if (!pDev) continue;

        wchar_t name[OD_DEVICE_MAX_NAME];
        GetDeviceFriendlyName(pDev, name, OD_DEVICE_MAX_NAME);

        if (wcsstr(name, L"CABLE") != NULL || wcsstr(name, L"VB-Audio") != NULL) {
            LPWSTR id = NULL;
            pDev->lpVtbl->GetId(pDev, &id);
            if (id) {
                wcsncpy(outId, id, maxLen - 1);
                outId[maxLen - 1] = L'\0';
                CoTaskMemFree(id);
                found = 1;
            }
            pDev->lpVtbl->Release(pDev);
            break;
        }
        pDev->lpVtbl->Release(pDev);
    }

    pColl->lpVtbl->Release(pColl);
    pEnum->lpVtbl->Release(pEnum);
    if (needUninit) CoUninitialize();
    return found;
}

#endif
