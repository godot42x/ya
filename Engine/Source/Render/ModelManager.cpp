#include "ModelManager.h"
#include "../Core/Log.h"

void ModelManager::init()
{
}

std::shared_ptr<Model> ModelManager::loadModel(const std::string &filePath, std::shared_ptr<CommandBuffer> commandBuffer)
{
    // Check if we've already loaded this model
    if (m_models.find(filePath) != m_models.end()) {
        return m_models[filePath];
    }

    // Create and load the new model
    auto model = std::make_shared<Model>();
    if (model->loadFromOBJ(filePath, commandBuffer)) {
        m_models[filePath] = model;
        return model;
    }

    NE_CORE_ERROR("Failed to load model: {}", filePath);
    return nullptr;
}

bool ModelManager::hasModel(const std::string &filePath) const
{
    return m_models.find(filePath) != m_models.end();
}

void ModelManager::clear()
{
    m_models.clear();
}
