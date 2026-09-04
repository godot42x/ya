#include "GameEditor/Panels/ContentBrowserPanel.h"

#include "GameEditor/EditorLayer.h"
#include "Resource/AssetManager.h"
#include "RHI/Backend/TextureLibrary.h"

namespace ya
{

ContentBrowserPanel::ContentBrowserPanel(EditorLayer* owner) : _owner(owner) {}

void ContentBrowserPanel::init()
{
    auto am            = AssetManager::get();
    auto fileTexture   = am->loadTextureSync("file", "Engine/Content/TestTextures/editor/file.png").get();
    auto folderTexture = am->loadTextureSync("folder", "Engine/Content/TestTextures/editor/folder2.png").get();
    auto sampler       = TextureLibrary::get().getDefaultSampler();

    fileIcon   = _owner->getOrCreateImGuiTextureID(fileTexture->getImageView(), sampler);
    folderIcon = _owner->getOrCreateImGuiTextureID(folderTexture->getImageView(), sampler);
}

} // namespace ya
