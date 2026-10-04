#include "classifier_windows.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

#define PI 3.14159265358979323846f


static Preset_t active_preset = { "none", 0 };
static SpectralFeatures_t prev_features = {0};


static const char* type_names[] = {
    "Unknown",
    "Footstep",
    "AR",
    "SMG",
    "SR",
    "DMR",
    "Grenade",
    "Smoke",
    "Vehicle",
};

void OD_Classifier_Init(void) {
    memset(&prev_features, 0, sizeof(prev_features));
    active_preset.name = "none";
    active_preset.enabled = 0;
    printf("[Classifier] Initialized\n");
}

void OD_Classifier_SetPreset(const char* preset_name) {
    if (strcmp(preset_name, "pubg") == 0 || strcmp(preset_name, "PUBG") == 0) {
        active_preset.name = "PUBG";
        active_preset.enabled = 1;
        printf("[Classifier] Preset set to PUBG (rule-based)\n");
    } else if (strcmp(preset_name, "none") == 0) {
        active_preset.name = "none";
        active_preset.enabled = 0;
        printf("[Classifier] Classification disabled\n");
    } else {
        printf("[Classifier] Unknown preset: %s\n", preset_name);
    }
}

int OD_Classifier_IsEnabled(void) {
    return active_preset.enabled;
}

const char* OD_Classifier_TypeName(SoundType_t type) {
    if (type >= 0 && type < SOUND_TYPE_COUNT)
        return type_names[type];
    return "Unknown";
}


static float band_energy(const float* samples, uint32_t n, float freq_low, float freq_high, uint32_t sample_rate) {
    float energy = 0.0f;
    uint32_t bin_low = (uint32_t)(freq_low * n / sample_rate);
    uint32_t bin_high = (uint32_t)(freq_high * n / sample_rate);
    if (bin_low < 1) bin_low = 1;
    if (bin_high > n / 2) bin_high = n / 2;

    
    uint32_t step = (bin_high - bin_low);
    if (step > 8) step = step / 8;
    if (step < 1) step = 1;

    /* Goertzel: same |X(bin)|^2 / n as a direct DFT, without per-sample trig. */
    for (uint32_t bin = bin_low; bin < bin_high; bin += step) {
        double coeff = 2.0 * cos(2.0 * 3.14159265358979323846 * (double)bin / (double)n);
        double s1 = 0.0, s2 = 0.0;
        for (uint32_t i = 0; i < n; i++) {
            double s0 = (double)samples[i] + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
        if (power > 0.0) energy += (float)(power / (double)n);
    }
    return energy;
}

SpectralFeatures_t OD_Classifier_ExtractFeatures(const float* left, const float* right,
                                                   uint32_t num_samples, uint32_t sample_rate) {
    SpectralFeatures_t f;
    memset(&f, 0, sizeof(f));

    if (!left || !right || num_samples == 0) return f;

    
    uint32_t n = num_samples;
    if (n > 512) n = 512;

    float mono[512];
    for (uint32_t i = 0; i < n; i++) {
        mono[i] = (left[i] + right[i]) * 0.5f;
    }

    
    float sum_sq = 0.0f;
    for (uint32_t i = 0; i < n; i++) {
        sum_sq += mono[i] * mono[i];
    }
    f.energy = sqrtf(sum_sq / n);

    
    float e_low  = band_energy(mono, n, 20.0f, 300.0f, sample_rate);    
    float e_mid  = band_energy(mono, n, 300.0f, 4000.0f, sample_rate);  
    float e_high = band_energy(mono, n, 4000.0f, 12000.0f, sample_rate); 
    float e_total = e_low + e_mid + e_high;

    if (e_total > 0.0001f) {
        f.low_freq_ratio  = e_low / e_total;
        f.mid_freq_ratio  = e_mid / e_total;
        f.high_freq_ratio = e_high / e_total;
    }

    
    float weighted_sum = e_low * 150.0f + e_mid * 2000.0f + e_high * 8000.0f;
    if (e_total > 0.0001f) {
        f.spectral_centroid = weighted_sum / e_total;
    }

    
    float var = e_low * (150.0f - f.spectral_centroid) * (150.0f - f.spectral_centroid)
              + e_mid * (2000.0f - f.spectral_centroid) * (2000.0f - f.spectral_centroid)
              + e_high * (8000.0f - f.spectral_centroid) * (8000.0f - f.spectral_centroid);
    if (e_total > 0.0001f) {
        f.spectral_spread = sqrtf(var / e_total);
    }

    
    f.transient = f.energy - prev_features.energy;
    if (f.transient < 0) f.transient = 0;

    
    uint32_t crossings = 0;
    for (uint32_t i = 1; i < n; i++) {
        if ((mono[i] >= 0 && mono[i-1] < 0) || (mono[i] < 0 && mono[i-1] >= 0))
            crossings++;
    }
    f.zero_crossing_rate = (float)crossings / (float)n;

    prev_features = f;
    return f;
}

ClassResult_t OD_Classifier_Classify(const SpectralFeatures_t* f) {
    ClassResult_t result = { SOUND_UNKNOWN, 0.0f };

    if (!active_preset.enabled || !f || f->energy < 0.001f) {
        return result;
    }

    
    SoundType_t best_type = SOUND_UNKNOWN;
    float best_score = 0.0f;

    
    
    
    

    
    
    
    {
        float score = 0.0f;
        if (f->transient > 0.12f) score += 0.3f;      
        if (f->energy > 0.2f) score += 0.3f;           
        if (f->low_freq_ratio > 0.3f) score += 0.2f;
        if (f->spectral_spread > 1800.0f) score += 0.2f;
        if (score > best_score) { best_score = score; best_type = SOUND_SR; }
    }

    
    
    {
        float score = 0.0f;
        if (f->transient > 0.04f && f->transient <= 0.15f) score += 0.3f;
        if (f->energy > 0.08f && f->energy <= 0.3f) score += 0.2f;
        if (f->mid_freq_ratio > 0.3f) score += 0.2f;
        if (f->high_freq_ratio > 0.12f) score += 0.15f;
        if (f->zero_crossing_rate > 0.18f) score += 0.15f;
        if (score > best_score) { best_score = score; best_type = SOUND_AR; }
    }

    
    
    {
        float score = 0.0f;
        if (f->transient > 0.03f && f->transient <= 0.08f) score += 0.25f;
        if (f->energy > 0.05f && f->energy <= 0.15f) score += 0.2f;
        if (f->high_freq_ratio > 0.25f) score += 0.25f;    
        if (f->low_freq_ratio < 0.25f) score += 0.15f;      
        if (f->zero_crossing_rate > 0.3f) score += 0.15f;   
        if (score > best_score) { best_score = score; best_type = SOUND_SMG; }
    }

    
    
    {
        float score = 0.0f;
        if (f->transient > 0.08f && f->transient <= 0.15f) score += 0.3f;
        if (f->energy > 0.15f && f->energy <= 0.3f) score += 0.25f;
        if (f->mid_freq_ratio > 0.35f) score += 0.2f;
        if (f->low_freq_ratio > 0.2f && f->low_freq_ratio < 0.4f) score += 0.15f;
        if (f->spectral_spread > 1500.0f && f->spectral_spread < 2500.0f) score += 0.1f;
        if (score > best_score) { best_score = score; best_type = SOUND_DMR; }
    }

    
    
    {
        float score = 0.0f;
        if (f->transient > 0.2f) score += 0.35f;        
        if (f->energy > 0.4f) score += 0.25f;           
        if (f->low_freq_ratio > 0.5f) score += 0.25f;   
        if (f->high_freq_ratio < 0.15f) score += 0.15f;  
        if (score > best_score) { best_score = score; best_type = SOUND_GRENADE; }
    }

    
    
    {
        float score = 0.0f;
        if (f->transient < 0.02f) score += 0.2f;         
        if (f->energy > 0.01f && f->energy < 0.08f) score += 0.2f; 
        if (f->high_freq_ratio > 0.4f) score += 0.3f;     
        if (f->zero_crossing_rate > 0.4f) score += 0.3f;   
        if (score > best_score) { best_score = score; best_type = SOUND_SMOKE; }
    }

    
    
    {
        float score = 0.0f;
        if (f->transient < 0.03f) score += 0.25f;        
        if (f->energy > 0.03f && f->energy < 0.2f) score += 0.2f;
        if (f->low_freq_ratio > 0.35f) score += 0.25f;   
        if (f->mid_freq_ratio > 0.3f) score += 0.15f;    
        if (f->zero_crossing_rate < 0.25f) score += 0.15f; 
        if (score > best_score) { best_score = score; best_type = SOUND_VEHICLE; }
    }

    
    
    {
        float score = 0.0f;
        if (f->transient > 0.01f && f->transient < 0.05f) score += 0.3f; 
        if (f->energy > 0.005f && f->energy < 0.06f) score += 0.25f;     
        if (f->low_freq_ratio > 0.4f) score += 0.25f;     
        if (f->high_freq_ratio < 0.2f) score += 0.2f;      
        if (score > best_score) { best_score = score; best_type = SOUND_FOOTSTEP; }
    }

    
    if (best_score >= 0.4f) {
        result.type = best_type;
        result.confidence = best_score;
    }

    return result;
}
