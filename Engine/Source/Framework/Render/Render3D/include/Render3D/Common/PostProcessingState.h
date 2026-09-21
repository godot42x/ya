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

    /// Display compose for an input that is ALREADY display-encoded (a View's
    /// display image). Every stage here is a look-changing stage, and this image
    /// has already been through them, so any flag left on grades it twice: an
    /// ACES curve plus a second gamma is exactly what a windowed present used to
    /// apply on top of the View's own finalize.
    [[nodiscard]] static PostProcessingState passThrough()
    {
        PostProcessingState state;
        state.bEnableToneMapping     = false;
        state.bEnableGammaCorrection = false;
        return state;
    }

    /// Grading off, display encoding kept. A View's color attachment is linear
    /// HDR, so the image still has to be encoded before anything can present
    /// it: switching postprocessing off removes how the image *looks*, it does
    /// not remove the pass that makes it a display image. Gamma is the encoding
    /// and stays as configured; everything that shapes the look goes neutral.
    [[nodiscard]] PostProcessingState withoutGrading() const
    {
        PostProcessingState state = *this;
        state.bEnableInversion    = false;
        state.grayscaleMode       = EGrayscaleMode::None;
        state.kernelMode          = EKernelMode::None;
        state.bEnableToneMapping  = false;
        state.bEnableRandomGrain  = false;
        state.bEnableBloom        = false;
        return state;
    }
};

} // namespace ya
