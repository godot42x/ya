#pragma once

#include <memory>
#include <string>
#include <unordered_map>



#include "Render/Core/Texture.h"
#include "Render/Model.h"
// #include "Render/Texture.h"

namespace Assimp
{
class Importer;
}

namespace ya
{

class AssetManager
{
  private:

    // Cache for loaded models
    std::unordered_map<std::string, std::shared_ptr<Model>>   modelCache;
    std::unordered_map<std::string, std::shared_ptr<Texture>> _textureViews;
    std::unordered_map<std::string, std::string>              _name2path;
    // Assimp importer
    Assimp::Importer *_importer = nullptr;

  public:
    static AssetManager *get();
    void                 cleanup();

    AssetManager();
    ~AssetManager()
    {
        YA_CORE_INFO("AssetManager cleanup");
        cleanup();
    }

    std::shared_ptr<Model> loadModel(const std::string &filepath, std::shared_ptr<CommandBuffer> commandBuffer);
    std::shared_ptr<Model> getModel(const std::string &filepath) const;
    bool                   isModelLoaded(const std::string &filepath) const;

    std::shared_ptr<Texture> loadTexture(const std::string &filepath);
    std::shared_ptr<Texture> getTextureByPath(const std::string &filepath) const { return isTextureLoaded(filepath) ? _textureViews.find(filepath)->second : nullptr; }
    std::shared_ptr<Texture> loadTexture(const std::string &name, const std::string &filepath);
    std::shared_ptr<Texture> getTextureByName(const std::string &name) const { return isTextureLoaded(name) ? _textureViews.find(name)->second : nullptr; }
    bool                     isTextureLoaded(const std::string &filepath) const { return _textureViews.find(filepath) != _textureViews.end(); }
};

} // namespace ya