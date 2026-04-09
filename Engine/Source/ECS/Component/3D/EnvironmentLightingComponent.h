#pragma once

#include "Core/Common/StateTransition.h"
#include "Core/Reflection/Reflection.h"

#include <array>
#include <optional>

#include "ECS/Component.h"
#include "Render/Core/Texture.h"
#include "Resource/AssetManager.h"

namespace ya
{

enum class EEnvironmentLightingSourceType : uint8_t
{
    SceneSkybox = 0,
    CubeFaces,
    Cylindrical,
};

enum class EEnvironmentLightingResolveState : uint8_t
{
    Empty = 0,
    Dirty,
    ResolvingSource,
    PreprocessingEnvironment,
    PreprocessingIrradiance,
    Ready,
    Failed,
};

template <>
struct StateTraits<EEnvironmentLightingResolveState>
{
    static constexpr bool hasFailedState    = true;
    static constexpr bool hasTerminalStates = true;

    static constexpr EEnvironmentLightingResolveState failedState()
    {
        return EEnvironmentLightingResolveState::Failed;
    }

    static constexpr bool isTerminal(EEnvironmentLightingResolveState state)
    {
        return state == EEnvironmentLightingResolveState::Empty ||
               state == EEnvironmentLightingResolveState::Ready ||
               state == EEnvironmentLightingResolveState::Failed;
    }
};

struct EnvironmentLightingComponent : public IComponent
{
    struct PendingBatchLoadState
    {
        AssetManager::TextureBatchMemoryHandle batchHandle = 0;
    };

    struct PendingOffscreenProcessState
    {
        stdptr<Texture> sourceTexture  = nullptr;
        stdptr<Texture> outputTexture  = nullptr;
        bool            bFlipVertical  = false;
        bool            bTaskQueued    = false;
        bool            bTaskFinished  = false;
        bool            bTaskSucceeded = false;
        bool            bCancelled     = false;
    };

    struct CubemapSource
    {
        YA_REFLECT_BEGIN(CubemapSource)
        YA_REFLECT_FIELD(files)
        YA_REFLECT_FIELD(flipVertical)
        YA_REFLECT_END()

        std::array<std::string, CubeFace_Count> files{};
        bool                                    flipVertical = false;

        bool hasAllFaces() const;
    };

    struct CylindricalSource
    {
        YA_REFLECT_BEGIN(CylindricalSource)
        YA_REFLECT_FIELD(filepath)
        YA_REFLECT_FIELD(flipVertical)
        YA_REFLECT_END()

        std::string filepath;
        bool        flipVertical = false;

        bool hasFile() const;
    };

    YA_REFLECT_BEGIN(EnvironmentLightingComponent, IComponent)
    YA_REFLECT_FIELD(sourceType)
    YA_REFLECT_FIELD(cubemapSource)
    YA_REFLECT_FIELD(cylindricalSource)
    YA_REFLECT_FIELD(irradianceFaceSize)
    YA_REFLECT_END()

    EEnvironmentLightingSourceType               sourceType          = EEnvironmentLightingSourceType::SceneSkybox;
    CubemapSource                                cubemapSource;
    CylindricalSource                            cylindricalSource;
    uint32_t                                     irradianceFaceSize  = 32;

    stdptr<Texture>                  cubemapTexture    = nullptr;
    stdptr<Texture>                  irradianceTexture = nullptr;
    EEnvironmentLightingResolveState resolveState      = EEnvironmentLightingResolveState::Dirty;

    void setFace(ECubeFace face, const std::string& path);
    void setCubemapSource(const CubeMapCreateInfo& createInfo);
    void setCylindricalSource(const std::string& filepath);
    bool hasSource() const;
    bool usesSceneSkybox() const;
    bool hasCubemapSource() const;
    bool hasCylindricalSource() const;
    void invalidate();
    bool isLoading() const;
    bool hasRenderableCubemap() const;
    bool hasIrradianceMap() const;
    uint32_t getResolvedIrradianceFaceSize() const;
    void onPostSerialize() override;
};

} // namespace ya

YA_REFLECT_ENUM_BEGIN(ya::EEnvironmentLightingSourceType)
YA_REFLECT_ENUM_VALUE(SceneSkybox)
YA_REFLECT_ENUM_VALUE(CubeFaces)
YA_REFLECT_ENUM_VALUE(Cylindrical)
YA_REFLECT_ENUM_END()

YA_REFLECT_ENUM_BEGIN(ya::EEnvironmentLightingResolveState)
YA_REFLECT_ENUM_VALUE(Empty)
YA_REFLECT_ENUM_VALUE(Dirty)
YA_REFLECT_ENUM_VALUE(ResolvingSource)
YA_REFLECT_ENUM_VALUE(PreprocessingEnvironment)
YA_REFLECT_ENUM_VALUE(PreprocessingIrradiance)
YA_REFLECT_ENUM_VALUE(Ready)
YA_REFLECT_ENUM_VALUE(Failed)
YA_REFLECT_ENUM_END()