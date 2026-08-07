#pragma once

#include <cstdint>

namespace ya
{

struct PostProcessingState
{
    enum class EGrayscaleMode : uint32_t
    {
        None = 0,
        Average,
        Weighted,
    };

    enum class EKernelMode : uint32_t
    {
        None = 0,
        Sharpen,
        Blur,
        EdgeDetection,
    };

    enum class EToneMappingCurve : uint32_t
    {
        ACES = 0,
        Uncharted2,
    };

    bool              bEnableInversion       = false;
    EGrayscaleMode    grayscaleMode          = EGrayscaleMode::None;
    EKernelMode       kernelMode             = EKernelMode::None;
    bool              bEnableToneMapping     = true;
    EToneMappingCurve toneMappingCurve       = EToneMappingCurve::ACES;
    float             exposure               = 0.6f;
    bool              bEnableGammaCorrection = true;
    float             gamma                  = 2.2f;
    bool              bEnableRandomGrain     = false;
    float             randomGrainStrength    = 0.05f;
    float             kernelTexelOffset      = 1.0f / 300.0f;

    bool              bEnableBloom           = false;
    float             bloomThreshold         = 1.0f;
    float             bloomSoftKnee          = 0.25f;
    float             bloomExtractIntensity  = 1.0f;
    uint32_t          bloomBlurPasses        = 5;
    float             bloomStrength          = 0.8f;
};

} // namespace ya
