#include "dsp_windows.h"
#include "classifier_windows.h"
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define PI 3.14159265358979323846f

/* ──────────────────── DFT helpers ──────────────────── */

/* Goertzel: same |X(bin)|^2 / n as a direct DFT, without per-sample trig. */
static float dft_bin_energy(const float* samples, uint32_t n, uint32_t bin, uint32_t total_bins) {
    double coeff = 2.0 * cos(2.0 * 3.14159265358979323846 * (double)bin / (double)total_bins);
    double s1 = 0.0, s2 = 0.0;
    for (uint32_t i = 0; i < n; i++) {
        double s0 = (double)samples[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    if (power < 0.0) power = 0.0;
    return (float)(power / (double)n);
}

/* Fold the candidate in entities[entity_count] into an existing entity within
 * `separation` degrees, otherwise keep it as a new one. */
static void merge_or_add(SpatialData_t* result, float separation) {
    const SoundEntity_t* cand = &result->entities[result->entity_count];

    for (int e = 0; e < result->entity_count; e++) {
        float diff = result->entities[e].azimuth_angle - cand->azimuth_angle;
        if (diff > 180.0f) diff -= 360.0f;
        if (diff < -180.0f) diff += 360.0f;

        if (fabsf(diff) < separation) {
            /* Midpoint along the shorter arc, so 350 and 10 give 0, not 180. */
            float mid = cand->azimuth_angle + diff * 0.5f;
            if (mid < 0.0f) mid += 360.0f;
            if (mid >= 360.0f) mid -= 360.0f;
            result->entities[e].azimuth_angle = mid;
            if (cand->distance < result->entities[e].distance)
                result->entities[e].distance = cand->distance;
            return;
        }
    }

    result->entity_count++;
}

static float channel_energy(const float* interleaved, uint32_t num_frames, uint32_t total_channels, uint32_t ch_idx) {
    float e = 0.0f;
    for (uint32_t i = 0; i < num_frames; i++) {
        float s = interleaved[i * total_channels + ch_idx];
        e += s * s;
    }
    return e / (float)num_frames;
}

/* ──────────────────── Band helpers ──────────────────── */

#define NUM_BANDS 4
static const uint32_t band_start[] = { 1, 6, 21, 85 };
static const uint32_t band_end[]   = { 6, 21, 85, 256 };
#define FFT_SIZE 512

/* ──────────────────── Channel angle map ──────────────────── 
 *
 *  Standard 7.1 channel order (WAVEFORMATEXTENSIBLE):
 *    0: Front Left     (FL)   – 315° (i.e. -45°)
 *    1: Front Right    (FR)   –  45°
 *    2: Front Center   (FC)   –   0°
 *    3: LFE            (Sub)  – omitted (non-directional)
 *    4: Back Left      (BL)   – 225° (i.e. -135°)
 *    5: Back Right     (BR)   – 135°
 *    6: Side Left      (SL)   – 270° (i.e. -90°)
 *    7: Side Right     (SR)   –  90°
 *
 *  0° = directly ahead, clockwise positive.
 */

/* Angles in degrees for each channel index (for 8-channel 7.1 layout). */
static const float ch_angle_8[] = {
    315.0f,  /*  0  FL  */
     45.0f,  /*  1  FR  */
      0.0f,  /*  2  FC  */
     -1.0f,  /*  3  LFE – skip */
    225.0f,  /*  4  BL  */
    135.0f,  /*  5  BR  */
    270.0f,  /*  6  SL  */
     90.0f,  /*  7  SR  */
};

/* 5.1 layout (6 channels) */
static const float ch_angle_6[] = {
    315.0f,  /*  0  FL  */
     45.0f,  /*  1  FR  */
      0.0f,  /*  2  FC  */
     -1.0f,  /*  3  LFE – skip */
    225.0f,  /*  4  BL / SL  */
    135.0f,  /*  5  BR / SR  */
};

/* Left/right weights for the classifier's stereo downmix: |sin| of each channel's angle,
 * with the centre channel going to both sides. LFE is left out. */
static const float mix_left_8[]  = { 0.70710678f, 0.0f, 0.707f, 0.0f, 0.70710678f, 0.0f, 1.0f, 0.0f };
static const float mix_right_8[] = { 0.0f, 0.70710678f, 0.707f, 0.0f, 0.0f, 0.70710678f, 0.0f, 1.0f };
static const float mix_left_6[]  = { 0.70710678f, 0.0f, 0.707f, 0.0f, 0.70710678f, 0.0f };
static const float mix_right_6[] = { 0.0f, 0.70710678f, 0.707f, 0.0f, 0.0f, 0.70710678f };

/* ──────────────────── Main DSP entry ──────────────────── */

SpatialData_t OD_DSP_ProcessBuffer(const AudioBuffer_t* buffer, float sensitivity, float separation) {
    SpatialData_t result;
    memset(&result, 0, sizeof(SpatialData_t));

    if (buffer == NULL || buffer->buffer == NULL || buffer->num_samples == 0 || buffer->channels < 1) {
        return result;
    }

    uint32_t n = buffer->num_samples;
    if (n > FFT_SIZE) n = FFT_SIZE;
    uint32_t ch = buffer->channels;

    /* ── Left/right downmix ──
     * Read by the classifier and by the stereo fallback. With surround input and the
     * classifier off nothing uses it, so it is not built at all. */
    int classify = OD_Classifier_IsEnabled();
    int need_stereo = classify || ch < 6;

    float left[FFT_SIZE];
    float right[FFT_SIZE];
    if (need_stereo) {
        memset(left, 0, sizeof(left));
        memset(right, 0, sizeof(right));

        if (ch == 1) {
            for (uint32_t i = 0; i < n; i++) left[i] = right[i] = buffer->buffer[i];
        } else if (ch == 2) {
            for (uint32_t i = 0; i < n; i++) {
                left[i]  = buffer->buffer[i * 2 + 0];
                right[i] = buffer->buffer[i * 2 + 1];
            }
        } else {
            /* For multi-channel: simple stereo downmix for the classifier only */
            const float *wl = (ch >= 8) ? mix_left_8 : mix_left_6;
            const float *wr = (ch >= 8) ? mix_right_8 : mix_right_6;
            uint32_t dir = (ch >= 8) ? 8u : 6u;
            if (dir > ch) dir = ch;
            for (uint32_t i = 0; i < n; i++) {
                float l = 0.0f, r = 0.0f;
                for (uint32_t c = 0; c < dir; c++) {
                    float s = buffer->buffer[i * ch + c];
                    l += s * wl[c];
                    r += s * wr[c];
                }
                left[i]  = l;
                right[i] = r;
            }
        }
    }

    /* Run classifier on the downmixed stereo */
    ClassResult_t class_result = { SOUND_UNKNOWN, 0.0f };
    if (classify) {
        SpectralFeatures_t features = OD_Classifier_ExtractFeatures(left, right, n, buffer->sample_rate);
        class_result = OD_Classifier_Classify(&features);
    }

    if (sensitivity < 0.01f) return result;

    float min_thresh = 0.00001f;
    float max_thresh = 0.5f;
    float threshold = max_thresh * powf(min_thresh / max_thresh, sensitivity);

    /* ── Diagnostic logging ── */
    if (need_stereo) {
        static float peak_energy = 0;
        static int peak_counter = 0;
        float current_max = 0;
        for (uint32_t i = 0; i < n; i++) {
            float e = left[i]*left[i] + right[i]*right[i];
            if (e > current_max) current_max = e;
        }
        if (current_max > peak_energy) peak_energy = current_max;
        if (++peak_counter >= 100) {
            if (peak_energy > 0) {
                printf("[DSP Windows] Peak Energy: %.6f, Threshold: %.6f, Ch: %u\n", peak_energy, threshold, ch);
                fflush(stdout);
            }
            peak_energy = 0;
            peak_counter = 0;
        }
    }

    /* ────────────────────────────────────────────────────────
     *  MULTI-CHANNEL SPATIAL PROCESSING (>= 6 channels)
     *
     *  For each frequency band we compute the energy in
     *  every directional channel and sum their unit-vectors
     *  weighted by energy.  The resulting vector gives us
     *  azimuth (full 360°) and a distance proxy.
     * ──────────────────────────────────────────────────────── */

    if (ch >= 6) {
        /* Extract per-channel mono streams (skip LFE at index 3) */
        uint32_t dir_count = (ch >= 8) ? 8 : 6;
        const float *angle_table = (ch >= 8) ? ch_angle_8 : ch_angle_6;

        float ch_mono[8][FFT_SIZE];
        memset(ch_mono, 0, sizeof(ch_mono));
        for (uint32_t c = 0; c < dir_count; c++) {
            if (angle_table[c] < 0.0f) continue; /* LFE */
            for (uint32_t i = 0; i < n; i++) {
                ch_mono[c][i] = buffer->buffer[i * ch + c];
            }
        }

        for (int band = 0; band < NUM_BANDS && result.entity_count < 10; band++) {
            /* Energy per channel in this band */
            float ch_energy[8];
            memset(ch_energy, 0, sizeof(ch_energy));
            float total_energy = 0.0f;

            uint32_t step = (band_end[band] - band_start[band]);
            if (step < 1) step = 1;
            if (step > 8) step = step / 4;

            for (uint32_t c = 0; c < dir_count; c++) {
                if (angle_table[c] < 0.0f) continue; /* LFE */
                for (uint32_t bin = band_start[band]; bin < band_end[band]; bin += (step > 1 ? step : 1)) {
                    ch_energy[c] += dft_bin_energy(ch_mono[c], n, bin, FFT_SIZE);
                }
                total_energy += ch_energy[c];
            }

            if (total_energy < threshold) continue;

            /* Vector sum: weight each channel's unit-vector by its energy */
            float vx = 0.0f, vy = 0.0f;
            for (uint32_t c = 0; c < dir_count; c++) {
                if (angle_table[c] < 0.0f) continue;
                float rad = angle_table[c] * PI / 180.0f;
                vx += sinf(rad) * ch_energy[c];
                vy += cosf(rad) * ch_energy[c];
            }

            /* Azimuth from the vector */
            float azimuth = atan2f(vx, vy) * 180.0f / PI;
            if (azimuth < 0.0f) azimuth += 360.0f;

            /* Distance: louder → closer */
            float avg = total_energy / (float)(dir_count - 1); /* exclude LFE count */
            float distance = 1.0f / (1.0f + sqrtf(avg) * 15.0f);
            if (distance < 0.1f) distance = 0.1f;
            if (distance > 0.95f) distance = 0.95f;

            /* Confidence: how directional is the sound?
             * (magnitude of resultant vector / total energy) */
            float mag = sqrtf(vx * vx + vy * vy);
            float confidence = (total_energy > 0.0f) ? (mag / total_energy) : 0.0f;
            if (confidence > 1.0f) confidence = 1.0f;

            /* Merge nearby entities */
            SoundEntity_t* entity = &result.entities[result.entity_count];
            entity->azimuth_angle = azimuth;
            entity->distance = distance;
            entity->confidence = confidence;
            entity->signature_match_id = band;
            entity->sound_type = class_result.type;

            merge_or_add(&result, separation);
        }

        return result;
    }

    /* ────────────────────────────────────────────────────────
     *  STEREO / MONO FALLBACK  (channels < 6)
     *  Original left/right pan logic.
     * ──────────────────────────────────────────────────────── */

    for (int band = 0; band < NUM_BANDS && result.entity_count < 10; band++) {
        float energy_l = 0.0f;
        float energy_r = 0.0f;

        uint32_t step = (band_end[band] - band_start[band]);
        if (step < 1) step = 1;
        if (step > 8) step = step / 4;

        for (uint32_t bin = band_start[band]; bin < band_end[band]; bin += (step > 1 ? step : 1)) {
            energy_l += dft_bin_energy(left, n, bin, FFT_SIZE);
            energy_r += dft_bin_energy(right, n, bin, FFT_SIZE);
        }

        float total = energy_l + energy_r;
        if (total < threshold) continue;

        float pan = 0.0f;
        if (total > 0.0f) {
            pan = (energy_r - energy_l) / total;
        }

        float azimuth = pan * 90.0f;
        if (azimuth < 0) azimuth += 360.0f;

        float avg = total * 0.5f;
        float distance = 1.0f / (1.0f + sqrtf(avg) * 15.0f);
        if (distance < 0.1f) distance = 0.1f;
        if (distance > 0.95f) distance = 0.95f;

        SoundEntity_t* entity = &result.entities[result.entity_count];
        entity->azimuth_angle = azimuth;
        entity->distance = distance;
        entity->confidence = fabsf(pan);
        entity->signature_match_id = band;
        entity->sound_type = class_result.type;

        merge_or_add(&result, separation);
    }

    return result;
}

int OD_DSP_LoadSignature(int id, const char* file_path) {
    printf("[OD Core Windows] Loading DSP Signature ID %d from %s...\n", id, file_path);
    return 1;
}
