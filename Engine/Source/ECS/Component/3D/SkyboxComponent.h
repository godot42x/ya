#pragma once



#include "Core/Reflection/Reflection.h"


#include "Core/Base.h"

#include <array>

#include "ECS/Component.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/TextureFactory.h"

namespace ya
{


enum class ESkyboxResolveState : uint8_t
{
    Empty = 0,
    Dirty,
    Resolving,
    Ready,
    Failed,
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

    YA_REFLECT_BEGIN(SkyboxComponent, IComponent)
    YA_REFLECT_FIELD(source)
    YA_REFLECT_END()


    CubemapSource       source;
    stdptr<Texture>     cubemapTexture = nullptr;
    DescriptorSetHandle cubeMapDS      = nullptr;

    ESkyboxResolveState resolveState     = ESkyboxResolveState::Dirty;
    bool                bDescriptorDirty = true;

    void setFace(ECubeFace face, const std::string& path);
    void setCubemapSource(const CubeMapCreateInfo& createInfo);
    bool hasCubemapSource() const;
    bool resolve();
    bool hasRenderableCubemap() const;
    void invalidate();
    bool isLoading() const;
    void onPostSerialize() override;
};


} // namespace ya
