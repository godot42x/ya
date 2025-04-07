#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>


#include "Render/Model.h"
#include "Render/Texture.h"

class AssetManager
{
  private:
    static AssetManager *instance;

    // Cache for loaded models
    std::unordered_map<std::string, std::shared_ptr<Model>>   modelCache;
    std::unordered_map<std::string, std::shared_ptr<Texture>> textureCache;

    // Assimp importer
    Assimp::Importer importer;

  public:
    static void          init();
    static AssetManager *get() { return instance; }

    AssetManager()  = default;
    ~AssetManager() = default;

    // Model loading
    std::shared_ptr<Model> loadModel(const std::string &filepath, std::shared_ptr<CommandBuffer> commandBuffer);

    // Check if a model is already loaded
    bool isModelLoaded(const std::string &filepath) const;

    // Get a loaded model
    std::shared_ptr<Model> getModel(const std::string &filepath) const;
};