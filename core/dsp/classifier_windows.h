#ifndef OD_CLASSIFIER_WINDOWS_H
#define OD_CLASSIFIER_WINDOWS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum {
    SOUND_UNKNOWN = 0,
    SOUND_FOOTSTEP,
    SOUND_AR,
    SOUND_SMG,
    SOUND_SR,
    SOUND_DMR,
    SOUND_GRENADE,
    SOUND_SMOKE,
    SOUND_VEHICLE,
    SOUND_TYPE_COUNT
} SoundType_t;

typedef struct {
    SoundType_t type;
    float confidence;    
} ClassResult_t;

typedef struct {
    float energy;           
    float spectral_centroid; 
    float spectral_spread;   
    float high_freq_ratio;   
    float low_freq_ratio;    
    float mid_freq_ratio;    
    float transient;         
    float zero_crossing_rate; 
} SpectralFeatures_t;

typedef struct {
    const char* name;       
    int enabled;            
} Preset_t;

__declspec(dllexport) void OD_Classifier_Init(void);
__declspec(dllexport) void OD_Classifier_SetPreset(const char* preset_name);
/* Non-zero when a preset is active; feature extraction is only worth running then. */
__declspec(dllexport) int OD_Classifier_IsEnabled(void);
__declspec(dllexport) SpectralFeatures_t OD_Classifier_ExtractFeatures(const float* left, const float* right, uint32_t num_samples, uint32_t sample_rate);
__declspec(dllexport) ClassResult_t OD_Classifier_Classify(const SpectralFeatures_t* features);
__declspec(dllexport) const char* OD_Classifier_TypeName(SoundType_t type);

#ifdef __cplusplus
}
#endif

#endif 
