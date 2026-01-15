#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "Core/ResourceRegistry.h"
#include "Render/Core/Texture.h"
#include "Render/Model.h"

namespace Assimp
{
struct Importer;
}

namespace ya
{

namespace EResource
{
enum T
{
    None = 0,
    Texture,
    Model,

};


};

struct Resource
{
};



class AssetManager : public IResourceCache
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
    
    // IResourceCache interface
    void clearCache() override;
    const char* getCacheName() const override { return "AssetManager"; }

    AssetManager();
    ~AssetManager()
    {
        YA_CORE_INFO("AssetManager destructor");
    }

    std::shared_ptr<Model> loadModel(const std::string &filepath);
    std::shared_ptr<Model> loadModel(const std::string &name, const std::string &filepath);

    /**
     * @brief Load model with explicit coordinate system override
     * @param filepath Path to model file
     * @param coordSystem Coordinate system to use (overrides auto-detection)
     * @return Loaded model
     */
    std::shared_ptr<Model> loadModel(const std::string &filepath, CoordinateSystem coordSystem);

    std::shared_ptr<Model> getModel(const std::string &filepath) const;
    bool                   isModelLoaded(const std::string &filepath) const;

    std::shared_ptr<Texture> loadTexture(const std::string &filepath);
    std::shared_ptr<Texture> getTextureByPath(const std::string &filepath) const { return isTextureLoaded(filepath) ? _textureViews.find(filepath)->second : nullptr; }
    std::shared_ptr<Texture> loadTexture(const std::string &name, const std::string &filepath);
    std::shared_ptr<Texture> getTextureByName(const std::string &name) const
    {
        if (isTextureLoaded(name))
        {
            return _textureViews.find(name)->second;
        }
        return nullptr;
    }
    bool isTextureLoaded(const std::string &filepath) const { return _textureViews.find(filepath) != _textureViews.end(); }

    void registerTexture(const std::string &name, const stdptr<Texture> &texture)
    {
        auto it = _textureViews.find(name);
        if (it != _textureViews.end()) {
            YA_CORE_WARN("Texture with name '{}' already registered. Overwriting.", name);
        }
        _textureViews.insert({name, texture});
    }
};

} // namespace ya