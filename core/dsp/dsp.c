#include "dsp.h"
#include "classifier.h"
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define PI 3.14159265358979323846f

/* ──────────────────── DFT helpers ──────────────────── */

static float dft_bin_energy(const float* samples, uint32_t n, uint32_t bin, uint32_t total_bins) {
    float real = 0.0f, imag = 0.0f;
    float freq = (float)bin / (float)total_bins;
    for (uint32_t i = 0; i < n; i++) {
        float angle = 2.0f * PI * freq * (float)i;
        real += samples[i] * cosf(angle);
        imag += samples[i] * sinf(angle);
    }
    return (real * real + imag * imag) / (float)n;
}

/* ──────────────────── Band helpers ──────────────────── */

#define NUM_BANDS 4
static const uint32_t band_start[] = { 1, 6, 21, 85 };
static const uint32_t band_end[]   = { 6, 21, 85, 256 };
#define FFT_SIZE 512

/* ──────────────────── Channel angle map ──────────────────── 
 *
 *  Linux follows standard surround layouts (typically matching Windows/WAVEFORMATEXTENSIBLE).
 *  Channels: 0:FL, 1:FR, 2:FC, 3:LFE, 4:BL, 5:BR, 6:SL, 7:SR
 */

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

static const float ch_angle_6[] = {
    315.0f,  /*  0  FL  */
     45.0f,  /*  1  FR  */
      0.0f,  /*  2  FC  */
     -1.0f,  /*  3  LFE – skip */
    225.0f,  /*  4  BL / SL  */
    135.0f,  /*  5  BR / SR  */
};

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

    /* ── Mono/Stereo downmix for classifier ── */
    float left[FFT_SIZE];
    float right[FFT_SIZE];
    memset(left, 0, sizeof(left));
    memset(right, 0, sizeof(right));

    for (uint32_t i = 0; i < n; i++) {
        if (ch == 1) {
            left[i] = buffer->buffer[i];
            right[i] = buffer->buffer[i];
        } else if (ch == 2) {
            left[i]  = buffer->buffer[i * 2 + 0];
            right[i] = buffer->buffer[i * 2 + 1];
        } else {
            /* Multi-channel vector downmix for classifier only */
            float l = 0.0f, r = 0.0f;
            uint32_t dir_count = (ch >= 8) ? 8 : (ch >= 6 ? 6 : 0);
            const float *angle_table = (ch >= 8) ? ch_angle_8 : ch_angle_6;

            if (dir_count > 0) {
                for (uint32_t c = 0; c < dir_count; c++) {
                    float s = buffer->buffer[i * ch + c];
                    float a = angle_table[c];
                    if (a < 0.0f) continue;
                    float rad = a * PI / 180.0f;
                    float lr_weight = sinf(rad);
                    if (lr_weight < 0.0f) l += s * (-lr_weight);
                    else r += s * lr_weight;
                    if (fabsf(lr_weight) < 0.15f) {
                        l += s * 0.707f;
                        r += s * 0.707f;
                    }
                }
            } else {
                l = buffer->buffer[i * ch + 0];
                r = buffer->buffer[i * ch + 1];
            }
            left[i] = l;
            right[i] = r;
        }
    }

    SpectralFeatures_t features = OD_Classifier_ExtractFeatures(left, right, n, buffer->sample_rate);
    ClassResult_t class_result = OD_Classifier_Classify(&features);

    if (sensitivity < 0.01f) return result;

    float min_thresh = 0.00001f;
    float max_thresh = 0.5f;
    float threshold = max_thresh * powf(min_thresh / max_thresh, sensitivity);

    /* ── Multi-channel Spatial Processing (Vector Summation) ── */
    if (ch >= 6) {
        uint32_t dir_count = (ch >= 8) ? 8 : 6;
        const float *angle_table = (ch >= 8) ? ch_angle_8 : ch_angle_6;

        float ch_mono[8][FFT_SIZE];
        memset(ch_mono, 0, sizeof(ch_mono));
        for (uint32_t c = 0; c < dir_count; c++) {
            if (angle_table[c] < 0.0f) continue;
            for (uint32_t i = 0; i < n; i++) {
                ch_mono[c][i] = buffer->buffer[i * ch + c];
            }
        }

        for (int band = 0; band < NUM_BANDS && result.entity_count < 10; band++) {
            float ch_energy[8];
            memset(ch_energy, 0, sizeof(ch_energy));
            float total_energy = 0.0f;

            uint32_t step = (band_end[band] - band_start[band]);
            if (step < 1) step = 1;
            if (step > 8) step = step / 4;

            for (uint32_t c = 0; c < dir_count; c++) {
                if (angle_table[c] < 0.0f) continue;
                for (uint32_t bin = band_start[band]; bin < band_end[band]; bin += (step > 1 ? step : 1)) {
                    ch_energy[c] += dft_bin_energy(ch_mono[c], n, bin, FFT_SIZE);
                }
                total_energy += ch_energy[c];
            }

            if (total_energy < threshold) continue;

            float vx = 0.0f, vy = 0.0f;
            for (uint32_t c = 0; c < dir_count; c++) {
                if (angle_table[c] < 0.0f) continue;
                float rad = angle_table[c] * PI / 180.0f;
                vx += sinf(rad) * ch_energy[c];
                vy += cosf(rad) * ch_energy[c];
            }

            float azimuth = atan2f(vx, vy) * 180.0f / PI;
            if (azimuth < 0.0f) azimuth += 360.0f;

            float avg = total_energy / (float)(dir_count - 1);
            float distance = 1.0f / (1.0f + sqrtf(avg) * 15.0f);
            if (distance < 0.1f) distance = 0.1f;
            if (distance > 0.95f) distance = 0.95f;

            float mag = sqrtf(vx * vx + vy * vy);
            float confidence = (total_energy > 0.0f) ? (mag / total_energy) : 0.0f;
            if (confidence > 1.0f) confidence = 1.0f;

            SoundEntity_t* entity = &result.entities[result.entity_count];
            entity->azimuth_angle = azimuth;
            entity->distance = distance;
            entity->confidence = confidence;
            entity->signature_match_id = band;
            entity->sound_type = class_result.type;

            bool merged = false;
            for (int e = 0; e < result.entity_count; e++) {
                float diff = result.entities[e].azimuth_angle - azimuth;
                if (diff > 180.0f) diff -= 360.0f;
                if (diff < -180.0f) diff += 360.0f;

                if (fabsf(diff) < separation) {
                    /* Midpoint along the shorter arc, so 350 and 10 give 0, not 180. */
                    float mid = azimuth + diff * 0.5f;
                    if (mid < 0.0f) mid += 360.0f;
                    if (mid >= 360.0f) mid -= 360.0f;
                    result.entities[e].azimuth_angle = mid;
                    if (distance < result.entities[e].distance)
                        result.entities[e].distance = distance;
                    merged = true;
                    break;
                }
            }

            if (!merged) {
                result.entity_count++;
            }
        }
        return result;
    }

    /* ── Stereo Fallback ── */
    for (int band = 0; band < NUM_BANDS && result.entity_count < 10; band++) {
        float energy_l = 0.0f, energy_r = 0.0f;
        uint32_t step = (band_end[band] - band_start[band]);
        if (step < 1) step = 1;

        for (uint32_t bin = band_start[band]; bin < band_end[band]; bin += (step > 1 ? step : 1)) {
            energy_l += dft_bin_energy(left, n, bin, FFT_SIZE);
            energy_r += dft_bin_energy(right, n, bin, FFT_SIZE);
        }

        float total = energy_l + energy_r;
        if (total < threshold) continue;

        float pan = (total > 0.0f) ? (energy_r - energy_l) / total : 0.0f;
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

        bool merged = false;
        for (int e = 0; e < result.entity_count; e++) {
            float diff = result.entities[e].azimuth_angle - azimuth;
            if (diff > 180.0f) diff -= 360.0f;
            if (diff < -180.0f) diff += 360.0f;

            if (fabsf(diff) < separation) {
                /* Midpoint along the shorter arc, so 350 and 10 give 0, not 180. */
                float mid = azimuth + diff * 0.5f;
                if (mid < 0.0f) mid += 360.0f;
                if (mid >= 360.0f) mid -= 360.0f;
                result.entities[e].azimuth_angle = mid;
                if (distance < result.entities[e].distance)
                    result.entities[e].distance = distance;
                merged = true;
                break;
            }
        }
        if (!merged) result.entity_count++;
    }

    return result;
}

int OD_DSP_LoadSignature(int id, const char* file_path) {
    printf("[OD Core Linux] Loading DSP Signature ID %d from %s...\n", id, file_path);
    return 1;
}
