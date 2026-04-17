#pragma once

#include "Core/Common/StateTransition.h"
#include "Core/Reflection/Reflection.h"

#include <array>

#include "ECS/Component.h"
#include "Render/Core/Texture.h"

namespace ya
{

enum class EEnvironmentLightingSourceType : uint8_t
{
    SceneSkybox = 0,
    CubeFaces,
    Cylindrical,
};

enum class EEnvironmentLightingSourceResolveState : uint8_t
{
    Empty = 0,
    Dirty,
    ResolvingSource,
    BuildingEnvironmentCubemap,
    Ready,
    Failed,
};

template <>
struct StateTraits<EEnvironmentLightingSourceResolveState>
{
    static constexpr bool hasFailedState    = true;
    static constexpr bool hasTerminalStates = true;

    static constexpr EEnvironmentLightingSourceResolveState failedState()
    {
        return EEnvironmentLightingSourceResolveState::Failed;
    }

    static constexpr bool isTerminal(EEnvironmentLightingSourceResolveState state)
    {
        return state == EEnvironmentLightingSourceResolveState::Empty ||
               state == EEnvironmentLightingSourceResolveState::Ready ||
               state == EEnvironmentLightingSourceResolveState::Failed;
    }
};

enum class EEnvironmentLightingIrradianceResolveState : uint8_t
{
    Empty = 0,
    Disabled,
    Dirty,
    Building,
    Ready,
    Failed,
};

template <>
struct StateTraits<EEnvironmentLightingIrradianceResolveState>
{
    static constexpr bool hasFailedState    = true;
    static constexpr bool hasTerminalStates = true;

    static constexpr EEnvironmentLightingIrradianceResolveState failedState()
    {
        return EEnvironmentLightingIrradianceResolveState::Failed;
    }

    static constexpr bool isTerminal(EEnvironmentLightingIrradianceResolveState state)
    {
        return state == EEnvironmentLightingIrradianceResolveState::Empty ||
               state == EEnvironmentLightingIrradianceResolveState::Disabled ||
               state == EEnvironmentLightingIrradianceResolveState::Ready ||
               state == EEnvironmentLightingIrradianceResolveState::Failed;
    }
};

enum class EEnvironmentLightingPrefilterResolveState : uint8_t
{
    Empty = 0,
    Disabled,
    Dirty,
    Building,
    Ready,
    Failed,
};

template <>
struct StateTraits<EEnvironmentLightingPrefilterResolveState>
{
    static constexpr bool hasFailedState    = true;
    static constexpr bool hasTerminalStates = true;

    static constexpr EEnvironmentLightingPrefilterResolveState failedState()
    {
        return EEnvironmentLightingPrefilterResolveState::Failed;
    }

    static constexpr bool isTerminal(EEnvironmentLightingPrefilterResolveState state)
    {
        return state == EEnvironmentLightingPrefilterResolveState::Empty ||
               state == EEnvironmentLightingPrefilterResolveState::Disabled ||
               state == EEnvironmentLightingPrefilterResolveState::Ready ||
               state == EEnvironmentLightingPrefilterResolveState::Failed;
    }
};

struct EnvironmentLightingComponent : public IComponent
{
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
    YA_REFLECT_FIELD(bEnableIrradiance)
    YA_REFLECT_FIELD(bEnablePrefilter)
    YA_REFLECT_FIELD(irradianceFaceSize)
    YA_REFLECT_END()

    EEnvironmentLightingSourceType sourceType = EEnvironmentLightingSourceType::SceneSkybox;
    CubemapSource                  cubemapSource;
    CylindricalSource              cylindricalSource;
    bool                           bEnableIrradiance = true;
    bool                           bEnablePrefilter  = true;
    uint32_t                       irradianceFaceSize = 32;
    uint64_t                       authoringVersion   = 1;

    EEnvironmentLightingSourceResolveState     sourceState     = EEnvironmentLightingSourceResolveState::Dirty;
    EEnvironmentLightingIrradianceResolveState irradianceState = EEnvironmentLightingIrradianceResolveState::Dirty;
    EEnvironmentLightingPrefilterResolveState  prefilterState  = EEnvironmentLightingPrefilterResolveState::Dirty;

    void setFace(ECubeFace face, const std::string& path);
    void setCubemapSource(const CubeMapCreateInfo& createInfo);
    void setCylindricalSource(const std::string& filepath);

    bool hasSource() const
    {
        if (usesSceneSkybox()) {
            return true;
        }
        if (sourceType == EEnvironmentLightingSourceType::CubeFaces) {
            return hasCubemapSource();
        }
        return hasCylindricalSource();
    }

    bool usesSceneSkybox() const;
    bool hasCubemapSource() const
    {
        return sourceType == EEnvironmentLightingSourceType::CubeFaces && cubemapSource.hasAllFaces();
    }
    bool     hasCylindricalSource() const;
    void     invalidate();
    bool     isLoading() const;
    bool     hasReadySource() const;
    bool     hasReadyIrradiance() const;
    bool     hasReadyPrefilter() const;
    uint32_t getResolvedIrradianceFaceSize() const;
    void     onPostSerialize() override;
};

} // namespace ya

YA_REFLECT_ENUM_BEGIN(ya::EEnvironmentLightingSourceType)
YA_REFLECT_ENUM_VALUE(SceneSkybox)
YA_REFLECT_ENUM_VALUE(CubeFaces)
YA_REFLECT_ENUM_VALUE(Cylindrical)
YA_REFLECT_ENUM_END()

YA_REFLECT_ENUM_BEGIN(ya::EEnvironmentLightingSourceResolveState)
YA_REFLECT_ENUM_VALUE(Empty)
YA_REFLECT_ENUM_VALUE(Dirty)
YA_REFLECT_ENUM_VALUE(ResolvingSource)
YA_REFLECT_ENUM_VALUE(BuildingEnvironmentCubemap)
YA_REFLECT_ENUM_VALUE(Ready)
YA_REFLECT_ENUM_VALUE(Failed)
YA_REFLECT_ENUM_END()

YA_REFLECT_ENUM_BEGIN(ya::EEnvironmentLightingIrradianceResolveState)
YA_REFLECT_ENUM_VALUE(Empty)
YA_REFLECT_ENUM_VALUE(Disabled)
YA_REFLECT_ENUM_VALUE(Dirty)
YA_REFLECT_ENUM_VALUE(Building)
YA_REFLECT_ENUM_VALUE(Ready)
YA_REFLECT_ENUM_VALUE(Failed)
YA_REFLECT_ENUM_END()

YA_REFLECT_ENUM_BEGIN(ya::EEnvironmentLightingPrefilterResolveState)
YA_REFLECT_ENUM_VALUE(Empty)
YA_REFLECT_ENUM_VALUE(Disabled)
YA_REFLECT_ENUM_VALUE(Dirty)
YA_REFLECT_ENUM_VALUE(Building)
YA_REFLECT_ENUM_VALUE(Ready)
YA_REFLECT_ENUM_VALUE(Failed)
YA_REFLECT_ENUM_END()