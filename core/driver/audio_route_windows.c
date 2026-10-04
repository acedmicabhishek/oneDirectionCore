#ifdef _WIN32
#include "audio_route_windows.h"
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <propsys.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define ID_CHARS 200
#define MAX_ENDPOINTS 16

static const GUID _SUBTYPE_PCM = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
static const GUID _SUBTYPE_FLOAT = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

static const PROPERTYKEY KEY_FriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};
/* Device instance path of the adapter behind the endpoint, e.g. "{1}.ROOT\MEDIA\0000". */
static const PROPERTYKEY KEY_AdapterPath = {{0xb3f8fa53, 0x0004, 0x438e, {0x90, 0x03, 0x51, 0xa4, 0x6e, 0x13, 0x9b, 0xfc}}, 2};

/* IPolicyConfig is the undocumented interface the Sound control panel itself uses to change
 * a device's format and the default device; Windows has no public API for either. */
static const GUID CLSID_PolicyConfigClient = {0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9}};
static const GUID IID_IPolicyConfig = {0xf8679f50, 0x850a, 0x41cf, {0x9c, 0x72, 0x43, 0x0f, 0x29, 0x02, 0x90, 0xc8}};

typedef struct IPolicyConfig IPolicyConfig;
typedef struct IPolicyConfigVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IPolicyConfig*, REFIID, void**);
    ULONG (STDMETHODCALLTYPE *AddRef)(IPolicyConfig*);
    ULONG (STDMETHODCALLTYPE *Release)(IPolicyConfig*);
    HRESULT (STDMETHODCALLTYPE *GetMixFormat)(IPolicyConfig*, PCWSTR, WAVEFORMATEX**);
    HRESULT (STDMETHODCALLTYPE *GetDeviceFormat)(IPolicyConfig*, PCWSTR, INT, WAVEFORMATEX**);
    HRESULT (STDMETHODCALLTYPE *ResetDeviceFormat)(IPolicyConfig*, PCWSTR);
    HRESULT (STDMETHODCALLTYPE *SetDeviceFormat)(IPolicyConfig*, PCWSTR, WAVEFORMATEX*, WAVEFORMATEX*);
    void *GetProcessingPeriod;
    void *SetProcessingPeriod;
    void *GetShareMode;
    void *SetShareMode;
    void *GetPropertyValue;
    void *SetPropertyValue;
    HRESULT (STDMETHODCALLTYPE *SetDefaultEndpoint)(IPolicyConfig*, PCWSTR, ERole);
    void *SetEndpointVisibility;
} IPolicyConfigVtbl;
struct IPolicyConfig { const IPolicyConfigVtbl *lpVtbl; };

typedef struct {
    wchar_t id[ID_CHARS];
    wchar_t name[ID_CHARS];
    int is_virtual;
    int mix_channels;
    int supports_71;
} Endpoint_t;

/* What OD_Route_Prepare decided and changed, kept so it can be undone. */
static wchar_t capture_id[ID_CHARS], capture_name[ID_CHARS];
static wchar_t output_id[ID_CHARS], output_name[ID_CHARS];
static wchar_t previous_default_id[ID_CHARS];
static WAVEFORMATEXTENSIBLE previous_format;
static int have_previous_format = 0;
static int route_active = 0;

static void copy_w(wchar_t *dst, const wchar_t *src) {
    wcsncpy(dst, src ? src : L"", ID_CHARS - 1);
    dst[ID_CHARS - 1] = 0;
}

static void fill_format(WAVEFORMATEXTENSIBLE *f, WORD ch, DWORD mask, DWORD rate, WORD bits, WORD valid, const GUID *sub) {
    memset(f, 0, sizeof(*f));
    f->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    f->Format.nChannels = ch;
    f->Format.nSamplesPerSec = rate;
    f->Format.wBitsPerSample = bits;
    f->Format.nBlockAlign = ch * bits / 8;
    f->Format.nAvgBytesPerSec = rate * f->Format.nBlockAlign;
    f->Format.cbSize = 22;
    f->Samples.wValidBitsPerSample = valid;
    f->dwChannelMask = mask;
    f->SubFormat = *sub;
}

/* Find a format with this many channels that the device hardware accepts, staying as close
 * as possible to the rate and depth it runs at now. */
static int find_device_format(IAudioClient *client, const WAVEFORMATEX *current, WORD ch, WAVEFORMATEXTENSIBLE *out) {
    DWORD mask = ch >= 8 ? 0x63F : 0x3;
    WORD cur_bits = current ? current->wBitsPerSample : 16;
    WORD cur_valid = cur_bits;
    DWORD cur_rate = current ? current->nSamplesPerSec : 48000;
    if (current && current->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        cur_valid = ((const WAVEFORMATEXTENSIBLE*)current)->Samples.wValidBitsPerSample;
    }

    WORD bits[4] = {cur_bits, 16, 24, 32};
    WORD valid[4] = {cur_valid, 16, 24, 32};
    DWORD rates[3] = {cur_rate, 48000, 44100};
    for (int r = 0; r < 3; r++) {
        for (int b = 0; b < 4; b++) {
            fill_format(out, ch, mask, rates[r], bits[b], valid[b], &_SUBTYPE_PCM);
            if (client->lpVtbl->IsFormatSupported(client, AUDCLNT_SHAREMODE_EXCLUSIVE, &out->Format, NULL) == S_OK) return 1;
        }
    }
    return 0;
}

static void read_string_prop(IPropertyStore *props, const PROPERTYKEY *key, wchar_t *dst) {
    PROPVARIANT v;
    PropVariantInit(&v);
    dst[0] = 0;
    if (SUCCEEDED(props->lpVtbl->GetValue(props, key, &v)) && v.vt == VT_LPWSTR && v.pwszVal) copy_w(dst, v.pwszVal);
    PropVariantClear(&v);
}

static int enumerate(IMMDeviceEnumerator *en, Endpoint_t *list, int max) {
    IMMDeviceCollection *col = NULL;
    if (FAILED(en->lpVtbl->EnumAudioEndpoints(en, eRender, DEVICE_STATE_ACTIVE, &col))) return 0;
    UINT count = 0;
    col->lpVtbl->GetCount(col, &count);

    int n = 0;
    for (UINT i = 0; i < count && n < max; i++) {
        IMMDevice *dev = NULL;
        if (FAILED(col->lpVtbl->Item(col, i, &dev))) continue;
        Endpoint_t *e = &list[n];
        memset(e, 0, sizeof(*e));

        LPWSTR id = NULL;
        if (SUCCEEDED(dev->lpVtbl->GetId(dev, &id))) { copy_w(e->id, id); CoTaskMemFree(id); }

        IPropertyStore *props = NULL;
        if (SUCCEEDED(dev->lpVtbl->OpenPropertyStore(dev, STGM_READ, &props))) {
            wchar_t adapter[ID_CHARS];
            read_string_prop(props, &KEY_FriendlyName, e->name);
            read_string_prop(props, &KEY_AdapterPath, adapter);
            /* Software-only drivers are enumerated by the ROOT bus; real hardware by HDAUDIO, USB, ... */
            e->is_virtual = wcsstr(adapter, L".ROOT\\") != NULL;
            props->lpVtbl->Release(props);
        }

        IAudioClient *client = NULL;
        if (SUCCEEDED(dev->lpVtbl->Activate(dev, &IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&client))) {
            WAVEFORMATEX *mix = NULL;
            if (SUCCEEDED(client->lpVtbl->GetMixFormat(client, &mix))) {
                e->mix_channels = mix->nChannels;
                CoTaskMemFree(mix);
            }
            WAVEFORMATEXTENSIBLE probe;
            e->supports_71 = find_device_format(client, NULL, 8, &probe);
            client->lpVtbl->Release(client);
        }

        dev->lpVtbl->Release(dev);
        if (e->id[0]) n++;
    }
    col->lpVtbl->Release(col);
    return n;
}

static void default_id(IMMDeviceEnumerator *en, ERole role, wchar_t *dst) {
    IMMDevice *dev = NULL;
    dst[0] = 0;
    if (SUCCEEDED(en->lpVtbl->GetDefaultAudioEndpoint(en, eRender, role, &dev))) {
        LPWSTR id = NULL;
        if (SUCCEEDED(dev->lpVtbl->GetId(dev, &id))) { copy_w(dst, id); CoTaskMemFree(id); }
        dev->lpVtbl->Release(dev);
    }
}

static Endpoint_t *find_by_id(Endpoint_t *list, int n, const wchar_t *id) {
    for (int i = 0; i < n; i++) if (wcscmp(list[i].id, id) == 0) return &list[i];
    return NULL;
}

static int mix_channels_of(IMMDeviceEnumerator *en, const wchar_t *id) {
    int channels = 0;
    IMMDevice *dev = NULL;
    if (FAILED(en->lpVtbl->GetDevice(en, id, &dev))) return 0;
    IAudioClient *client = NULL;
    if (SUCCEEDED(dev->lpVtbl->Activate(dev, &IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&client))) {
        WAVEFORMATEX *mix = NULL;
        if (SUCCEEDED(client->lpVtbl->GetMixFormat(client, &mix))) { channels = mix->nChannels; CoTaskMemFree(mix); }
        client->lpVtbl->Release(client);
    }
    dev->lpVtbl->Release(dev);
    return channels;
}

/* Switch a device to `ch` channels. Saves the format it had when `save` is set. */
static int set_channels(IMMDeviceEnumerator *en, IPolicyConfig *pc, const wchar_t *id, WORD ch, int save) {
    int ok = 0;
    IMMDevice *dev = NULL;
    if (FAILED(en->lpVtbl->GetDevice(en, id, &dev))) return 0;

    IAudioClient *client = NULL;
    WAVEFORMATEX *current = NULL;
    if (SUCCEEDED(dev->lpVtbl->Activate(dev, &IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&client))) {
        pc->lpVtbl->GetDeviceFormat(pc, id, FALSE, &current);
        if (save && current) {
            size_t size = sizeof(WAVEFORMATEX) + current->cbSize;
            if (size > sizeof(previous_format)) size = sizeof(previous_format);
            memset(&previous_format, 0, sizeof(previous_format));
            memcpy(&previous_format, current, size);
            have_previous_format = 1;
        }

        WAVEFORMATEXTENSIBLE device_fmt, mix_fmt;
        if (find_device_format(client, current, ch, &device_fmt)) {
            fill_format(&mix_fmt, ch, device_fmt.dwChannelMask, device_fmt.Format.nSamplesPerSec, 32, 32, &_SUBTYPE_FLOAT);
            ok = SUCCEEDED(pc->lpVtbl->SetDeviceFormat(pc, id, &device_fmt.Format, &mix_fmt.Format));
        }
        client->lpVtbl->Release(client);
    }
    if (current) CoTaskMemFree(current);
    dev->lpVtbl->Release(dev);
    return ok;
}

static void set_default(IPolicyConfig *pc, const wchar_t *id) {
    /* Communications is left alone so voice chat stays on the user's own device. */
    pc->lpVtbl->SetDefaultEndpoint(pc, id, eConsole);
    pc->lpVtbl->SetDefaultEndpoint(pc, id, eMultimedia);
}

/* COM is entered per call; nothing is kept between calls. */
static int com_enter(IMMDeviceEnumerator **en, IPolicyConfig **pc, int *owned) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    *owned = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return 0;

    *en = NULL;
    *pc = NULL;
    if (SUCCEEDED(CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void**)en)) &&
        SUCCEEDED(CoCreateInstance(&CLSID_PolicyConfigClient, NULL, CLSCTX_ALL, &IID_IPolicyConfig, (void**)pc))) {
        return 1;
    }
    if (*en) { (*en)->lpVtbl->Release(*en); *en = NULL; }
    if (*owned) CoUninitialize();
    return 0;
}

static void com_leave(IMMDeviceEnumerator *en, IPolicyConfig *pc, int owned) {
    if (pc) pc->lpVtbl->Release(pc);
    if (en) en->lpVtbl->Release(en);
    if (owned) CoUninitialize();
}

int OD_Route_Prepare(const wchar_t *preferred_output_name) {
    IMMDeviceEnumerator *en;
    IPolicyConfig *pc;
    int owned;
    if (route_active) OD_Route_Restore();
    capture_id[0] = capture_name[0] = output_id[0] = output_name[0] = previous_default_id[0] = 0;
    have_previous_format = 0;
    if (!com_enter(&en, &pc, &owned)) return OD_ROUTE_NONE;

    int result = OD_ROUTE_NONE;
    Endpoint_t list[MAX_ENDPOINTS];
    int n = enumerate(en, list, MAX_ENDPOINTS);

    wchar_t console_id[ID_CHARS], comm_id[ID_CHARS];
    default_id(en, eConsole, console_id);
    default_id(en, eCommunications, comm_id);
    Endpoint_t *def = find_by_id(list, n, console_id);

    if (def && def->mix_channels >= 6) {
        result = OD_ROUTE_NATIVE;
        goto done;
    }

    /* The virtual device the game will render into: the default one when it qualifies. */
    Endpoint_t *virt = (def && def->is_virtual && def->supports_71) ? def : NULL;
    for (int i = 0; i < n && !virt; i++) {
        if (list[i].is_virtual && list[i].supports_71) virt = &list[i];
    }
    if (!virt) goto done;

    /* The device the user hears. When the virtual device is already the default (an audio
     * enhancer made it so), the communications default still points at the real one. */
    Endpoint_t *out = NULL;
    if (preferred_output_name && preferred_output_name[0]) {
        for (int i = 0; i < n && !out; i++) {
            if (!list[i].is_virtual && wcscmp(list[i].name, preferred_output_name) == 0) out = &list[i];
        }
    }
    if (!out && def && def != virt && !def->is_virtual) out = def;
    if (!out) {
        Endpoint_t *comm = find_by_id(list, n, comm_id);
        if (comm && comm != virt && !comm->is_virtual) out = comm;
    }
    for (int i = 0; i < n && !out; i++) {
        if (!list[i].is_virtual) out = &list[i];
    }
    if (!out) goto done;

    if (!set_channels(en, pc, virt->id, 8, 1)) goto done;

    /* Another program owning the device (the FxSound app does) puts it straight back to stereo. */
    Sleep(400);
    if (mix_channels_of(en, virt->id) < 6) {
        if (have_previous_format) {
            WAVEFORMATEXTENSIBLE mix_fmt;
            fill_format(&mix_fmt, previous_format.Format.nChannels, previous_format.dwChannelMask,
                        previous_format.Format.nSamplesPerSec, 32, 32, &_SUBTYPE_FLOAT);
            pc->lpVtbl->SetDeviceFormat(pc, virt->id, &previous_format.Format, &mix_fmt.Format);
        }
        have_previous_format = 0;
        result = OD_ROUTE_BLOCKED;
        goto done;
    }

    if (def != virt) {
        copy_w(previous_default_id, console_id);
        set_default(pc, virt->id);
    }

    copy_w(capture_id, virt->id);
    copy_w(capture_name, virt->name);
    copy_w(output_id, out->id);
    copy_w(output_name, out->name);
    route_active = 1;
    result = OD_ROUTE_VIRTUAL;

done:
    com_leave(en, pc, owned);
    return result;
}

void OD_Route_Restore(void) {
    if (!route_active) return;
    route_active = 0;

    IMMDeviceEnumerator *en;
    IPolicyConfig *pc;
    int owned;
    if (!com_enter(&en, &pc, &owned)) return;

    if (have_previous_format) {
        WAVEFORMATEXTENSIBLE mix_fmt;
        fill_format(&mix_fmt, previous_format.Format.nChannels, previous_format.dwChannelMask,
                    previous_format.Format.nSamplesPerSec, 32, 32, &_SUBTYPE_FLOAT);
        pc->lpVtbl->SetDeviceFormat(pc, capture_id, &previous_format.Format, &mix_fmt.Format);
        have_previous_format = 0;
    }
    if (previous_default_id[0]) set_default(pc, previous_default_id);

    com_leave(en, pc, owned);
}

void OD_Route_RestoreSaved(const wchar_t *virtual_id, const wchar_t *saved_default_id) {
    IMMDeviceEnumerator *en;
    IPolicyConfig *pc;
    int owned;
    if (!com_enter(&en, &pc, &owned)) return;

    if (virtual_id && virtual_id[0]) set_channels(en, pc, virtual_id, 2, 0);
    if (saved_default_id && saved_default_id[0]) set_default(pc, saved_default_id);

    com_leave(en, pc, owned);
}

const wchar_t *OD_Route_CaptureId(void) { return capture_id; }
const wchar_t *OD_Route_CaptureName(void) { return capture_name; }
const wchar_t *OD_Route_OutputId(void) { return output_id; }
const wchar_t *OD_Route_OutputName(void) { return output_name; }
const wchar_t *OD_Route_PreviousDefaultId(void) { return previous_default_id; }

int OD_Route_ListOutputs(wchar_t *buffer, int buffer_chars) {
    if (!buffer || buffer_chars <= 0) return 0;
    buffer[0] = 0;

    IMMDeviceEnumerator *en;
    IPolicyConfig *pc;
    int owned;
    if (!com_enter(&en, &pc, &owned)) return 0;

    Endpoint_t list[MAX_ENDPOINTS];
    int n = enumerate(en, list, MAX_ENDPOINTS);
    int count = 0;
    size_t used = 0;
    for (int i = 0; i < n; i++) {
        if (list[i].is_virtual) continue;
        size_t len = wcslen(list[i].name);
        if (used + len + 2 > (size_t)buffer_chars) break;
        if (count > 0) buffer[used++] = L'\n';
        memcpy(buffer + used, list[i].name, len * sizeof(wchar_t));
        used += len;
        buffer[used] = 0;
        count++;
    }

    com_leave(en, pc, owned);
    return count;
}
#endif
