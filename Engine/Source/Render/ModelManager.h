#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "Model.h"

class Render;
struct CommandBuffer;

class ModelManager
{
  public:
    ModelManager()  = default;
    ~ModelManager() = default;

    void                   init();
    std::shared_ptr<Model> loadModel(const std::string &filePath, std::shared_ptr<CommandBuffer> commandBuffer);
    bool                   hasModel(const std::string &filePath) const;
    void                   clear();

  private:
    std::unordered_map<std::string, std::shared_ptr<Model>> m_models;
    Render                                                 *m_render = nullptr;
};
