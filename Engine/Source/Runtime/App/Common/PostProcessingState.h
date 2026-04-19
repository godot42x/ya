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
    bool              bEnableToneMapping     = false;
    EToneMappingCurve toneMappingCurve       = EToneMappingCurve::ACES;
    bool              bEnableGammaCorrection = true;
    float             gamma                  = 2.2f;
    bool              bEnableRandomGrain     = false;
    float             randomGrainStrength    = 0.05f;
    float             kernelTexelOffset      = 1.0f / 300.0f;
};

} // namespace ya