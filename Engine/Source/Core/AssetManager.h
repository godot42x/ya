#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>


#include "Render/Core/Texture.h"
#include "Render/Model.h"
// #include "Render/Texture.h"

namespace ya
{

class AssetManager
{
  private:
    static AssetManager *instance;

    // Cache for loaded models
    std::unordered_map<std::string, std::shared_ptr<Model>>   modelCache;
    std::unordered_map<std::string, std::shared_ptr<Texture>> _textures;
    // Assimp importer
    Assimp::Importer importer;

  public:
    static void          init();
    static AssetManager *get() { return instance; }
    void                 cleanup()
    {
        modelCache.clear();
        _textures.clear();
    }

    AssetManager() = default;
    ~AssetManager()
    {
        YA_CORE_INFO("AssetManager cleanup");
        cleanup();
    }

    std::shared_ptr<Model> loadModel(const std::string &filepath, std::shared_ptr<CommandBuffer> commandBuffer);
    std::shared_ptr<Model> getModel(const std::string &filepath) const;
    bool                   isModelLoaded(const std::string &filepath) const;

    std::shared_ptr<Texture> loadTexture(const std::string &filepath);
    std::shared_ptr<Texture> getTexture(const std::string &filepath) const { return isTextureLoaded(filepath) ? _textures.find(filepath)->second : nullptr; }
    bool                     isTextureLoaded(const std::string &filepath) const { return _textures.find(filepath) != _textures.end(); }
};
} // namespace ya