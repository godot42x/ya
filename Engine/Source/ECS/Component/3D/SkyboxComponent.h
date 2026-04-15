#pragma once

#include "Core/Common/StateTransition.h"
#include "Core/Reflection/Reflection.h"

#include <array>

#include "ECS/Component.h"
#include "Render/Core/Texture.h"

namespace ya
{

enum class ESkyboxSourceType : uint8_t
{
    CubeFaces = 0,
    Cylindrical,
};

enum class ESkyboxResolveState : uint8_t
{
    Empty = 0,
    Dirty,
    ResolvingSource,
    Preprocessing,
    Ready,
    Failed,
};

template <>
struct StateTraits<ESkyboxResolveState>
{
    static constexpr bool hasFailedState    = true;
    static constexpr bool hasTerminalStates = true;

    static constexpr ESkyboxResolveState failedState() { return ESkyboxResolveState::Failed; }

    static constexpr bool isTerminal(ESkyboxResolveState state)
    {
        return state == ESkyboxResolveState::Empty ||
               state == ESkyboxResolveState::Ready ||
               state == ESkyboxResolveState::Failed;
    }
};

struct SkyboxComponent : public IComponent
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

    YA_REFLECT_BEGIN(SkyboxComponent, IComponent)
    YA_REFLECT_FIELD(sourceType)
    YA_REFLECT_FIELD(cubemapSource)
    YA_REFLECT_FIELD(cylindricalSource)
    YA_REFLECT_END()

    ESkyboxSourceType sourceType = ESkyboxSourceType::CubeFaces;
    CubemapSource     cubemapSource;
    CylindricalSource cylindricalSource;
    uint64_t          authoringVersion = 1;

    ESkyboxResolveState resolveState = ESkyboxResolveState::Dirty;

    void setFace(ECubeFace face, const std::string& path);
    void setCubemapSource(const CubeMapCreateInfo& createInfo);
    void setCylindricalSource(const std::string& filepath);
    bool hasSource() const;
    bool hasCubemapSource() const;
    bool hasCylindricalSource() const;
    void        invalidate();
    bool        isLoading() const;
    void        onPostSerialize() override;
    bool        isValid() { return resolveState == ESkyboxResolveState::Ready; }
};

} // namespace ya

YA_REFLECT_ENUM_BEGIN(ya::ESkyboxSourceType)
YA_REFLECT_ENUM_VALUE(CubeFaces)
YA_REFLECT_ENUM_VALUE(Cylindrical)
YA_REFLECT_ENUM_END()
