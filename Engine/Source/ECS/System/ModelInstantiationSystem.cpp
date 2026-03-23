#include "ModelInstantiationSystem.h"

#include "Runtime/App/App.h"
#include "Scene/Node.h"
#include "Scene/SceneManager.h"

#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/ModelComponent.h"
#include "ECS/Entity.h"

#include "Render/Material/MaterialFactory.h"
#include "Render/Material/PhongMaterial.h"
#include "Render/Model.h"

#include <format>

namespace ya
{

void ModelInstantiationSystem::onUpdate(float dt)
{
    (void)dt;

    auto  sceneManager = App::get()->getSceneManager();
    auto* scene        = sceneManager->getActiveScene();
    if (!scene) {
        return;
    }

    instantiatePendingModels(scene);
}

void ModelInstantiationSystem::instantiatePendingModels(Scene* scene)
{
    auto& registry = scene->getRegistry();

    registry.view<ModelComponent>().each([&](auto entityHandle, ModelComponent& modelComponent) {
        if (modelComponent.isResolved() || !modelComponent.hasModelSource()) {
            return;
        }

        Entity* entity = scene->getEntityByEnttID(entityHandle);
        if (!entity) {
            return;
        }

        try {
            instantiateModel(scene, entity, modelComponent);
        }
        catch (const std::exception& e) {
            YA_CORE_ERROR("ModelInstantiationSystem: Failed to instantiate model component: {}", e.what());
            modelComponent._bResolved = true;
        }
        catch (...) {
            YA_CORE_ERROR("ModelInstantiationSystem: Failed to instantiate model component");
            modelComponent._bResolved = true;
        }
    });
}

void ModelInstantiationSystem::instantiateModel(Scene* scene, Entity* entity, ModelComponent& modelComp)
{
    cleanupChildEntities(scene, modelComp);

    if (!modelComp._modelRef.isLoaded()) {
        if (!modelComp._modelRef.resolve()) {
            YA_CORE_WARN("ModelInstantiationSystem: Failed to load model '{}'",
                         modelComp._modelRef.getPath());
            return;
        }
    }

    Model* model = modelComp.getModel();
    if (!model || model->getMeshCount() == 0) {
        YA_CORE_WARN("ModelInstantiationSystem: Model '{}' has no meshes",
                     modelComp._modelRef.getPath());
        modelComp._bResolved = true;
        return;
    }

    if (!modelComp._autoCreateChildEntities) {
        modelComp._bResolved = true;
        return;
    }

    Node* parentNode = scene->getNodeByEntity(entity);
    YA_CORE_ASSERT(parentNode != nullptr, "Parent entity has no Node");

    buildSharedMaterials(model, modelComp);

    for (uint32_t i = 0; i < model->getMeshCount(); ++i) {
        Node* childNode = createMeshNode(scene, entity, model, i, modelComp);
        if (!childNode) {
            continue;
        }

        childNode->setParent(parentNode);
        modelComp._childNodes.push_back(childNode);
    }

    YA_CORE_INFO("ModelInstantiationSystem: Created {} child nodes with {} shared materials for model '{}'",
                 model->getMeshCount(),
                 modelComp._cachedMaterials.size(),
                 modelComp._modelRef.getPath());

    modelComp._bResolved = true;
}

Node* ModelInstantiationSystem::createMeshNode(Scene*          scene,
                                               Entity*         parentEntity,
                                               Model*          model,
                                               uint32_t        meshIndex,
                                               ModelComponent& modelComp)
{
    std::string meshName = model->getMesh(meshIndex)->getName();
    if (meshName.empty()) {
        meshName = std::format("Mesh_{}", meshIndex);
    }
    std::string nodeName = std::format("{}_{}", parentEntity->getName(), meshName);

    Node* parentNode = scene->getNodeByEntity(parentEntity);

    Node* childNode = scene->createNode3D(nodeName, parentNode);
    if (!childNode) {
        YA_CORE_ERROR("ModelInstantiationSystem: Failed to create child node '{}'", nodeName);
        return nullptr;
    }

    auto*   childNode3D = dynamic_cast<Node3D*>(childNode);
    Entity* childEntity = childNode3D ? childNode3D->getEntity() : nullptr;
    if (!childEntity) {
        YA_CORE_ERROR("ModelInstantiationSystem: Child node has no entity");
        return nullptr;
    }

    auto* meshComp = childEntity->addComponent<MeshComponent>();
    meshComp->setFromModel(model->getFilepath(), meshIndex, model->getMesh(meshIndex).get());

    auto* matComp = childEntity->addComponent<PhongMaterialComponent>();
    configureMeshMaterial(*matComp, model, meshIndex, modelComp);

    return childNode;
}

void ModelInstantiationSystem::buildSharedMaterials(Model* model, ModelComponent& modelComp)
{
    if (!modelComp._useEmbeddedMaterials || !model) {
        return;
    }

    const auto& embeddedMaterials = model->getEmbeddedMaterials();
    for (size_t matIndex = 0; matIndex < embeddedMaterials.size(); ++matIndex) {
        std::string matLabel = model->getName() + "_Mat_" + std::to_string(matIndex);
        auto*       litMat   = MaterialFactory::get()->createMaterial<PhongMaterial>(matLabel);
        if (!litMat) {
            continue;
        }

        modelComp._cachedMaterials[static_cast<int32_t>(matIndex)] = litMat;
    }
}

void ModelInstantiationSystem::configureMeshMaterial(PhongMaterialComponent& matComp,
                                                     Model*                  model,
                                                     uint32_t                meshIndex,
                                                     ModelComponent&         modelComp)
{
    if (!modelComp._useEmbeddedMaterials) {
        return;
    }

    int32_t matIndex = model->getMaterialIndex(meshIndex);
    auto    it       = modelComp._cachedMaterials.find(matIndex);
    if (it != modelComp._cachedMaterials.end() && it->second != nullptr) {
        const MaterialData* matData = model->getMaterialForMesh(meshIndex);
        if (matData) {
            matComp.importFromDescriptorWithSharedMaterial(*matData, it->second);
        }
        else {
            matComp.setSharedMaterial(it->second);
        }
        return;
    }

    const MaterialData* matData = model->getMaterialForMesh(meshIndex);
    initMaterialFromEmbedded(matComp, matData, model->getDirectory());
}

void ModelInstantiationSystem::initMaterialFromEmbedded(PhongMaterialComponent& matComp,
                                                        const MaterialData*     matData,
                                                        const std::string&      modelDirectory)
{
    (void)modelDirectory;
    if (!matData) {
        return;
    }

    matComp.importFromDescriptor(*matData);
}

void ModelInstantiationSystem::cleanupChildEntities(Scene* scene, ModelComponent& modelComp)
{
    for (auto& [matIndex, material] : modelComp._cachedMaterials) {
        (void)matIndex;
        if (material) {
            MaterialFactory::get()->destroyMaterial(material);
        }
    }
    modelComp._cachedMaterials.clear();

    for (Node* childNode : modelComp._childNodes) {
        if (childNode) {
            scene->destroyNode(childNode);
        }
    }
    modelComp._childNodes.clear();
}

} // namespace ya