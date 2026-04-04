#pragma once

#include "Core/Reflection/Reflection.h"

#include <array>

#include "ECS/Component.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/Texture.h"
#include "Resource/AssetManager.h"

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

struct SkyboxComponent : public IComponent
{
    struct PendingBatchLoadState
    {
        AssetManager::TextureBatchMemoryHandle batchHandle = 0;
    };

    struct PendingOffscreenProcessState
    {
        stdptr<Texture> sourceTexture = nullptr;
        stdptr<Texture> outputTexture = nullptr;
        bool            bFlipVertical = false;
        bool            bTaskQueued   = false;
        bool            bTaskFinished = false;
        bool            bTaskSucceeded = false;
        bool            bCancelled    = false;
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

    YA_REFLECT_BEGIN(SkyboxComponent, IComponent)
    YA_REFLECT_FIELD(sourceType)
    YA_REFLECT_FIELD(cubemapSource)
    YA_REFLECT_FIELD(cylindricalSource)
    YA_REFLECT_END()

    ESkyboxSourceType                      sourceType          = ESkyboxSourceType::CubeFaces;
    CubemapSource                          cubemapSource;
    CylindricalSource                      cylindricalSource;
    stdptr<Texture>                        cubemapTexture      = nullptr;
    stdptr<Texture>                        sourcePreviewTexture = nullptr;
    std::array<stdptr<IImageView>, CubeFace_Count> cubemapFacePreviewViews{};
    DescriptorSetHandle                    cubeMapDS           = nullptr;
    std::shared_ptr<PendingBatchLoadState> _pendingBatchLoad;
    std::shared_ptr<PendingOffscreenProcessState> _pendingOffscreenProcess;

    ESkyboxResolveState resolveState     = ESkyboxResolveState::Dirty;
    bool                bDescriptorDirty = true;

    void setFace(ECubeFace face, const std::string& path);
    void setCubemapSource(const CubeMapCreateInfo& createInfo);
    void setCylindricalSource(const std::string& filepath);
    bool hasSource() const;
    bool hasCubemapSource() const;
    bool hasCylindricalSource() const;
    bool resolve();
    bool hasRenderableCubemap() const;
    void rebuildCubemapPreviewViews();
    void clearCubemapPreviewViews();
    IImageView* getCubemapFacePreviewView(uint32_t faceIndex) const;
    void invalidate();
    bool isLoading() const;
    void onPostSerialize() override;
};

} // namespace ya

YA_REFLECT_ENUM_BEGIN(ya::ESkyboxSourceType)
YA_REFLECT_ENUM_VALUE(CubeFaces)
YA_REFLECT_ENUM_VALUE(Cylindrical)
YA_REFLECT_ENUM_END()
