#include "ModelInstantiationSystem.h"

#include "Runtime/App/App.h"
#include "Scene/Node.h"
#include "Scene/SceneManager.h"

#include "ECS/Component/ManagedChildComponent.h"

#include "ECS/Component/Material/PBRMaterialComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/ModelComponent.h"
#include "ECS/Entity.h"

#include "Render/Material/MaterialFactory.h"
#include "Render/Material/PBRMaterial.h"
#include "Render/Material/PhongMaterial.h"
#include "Render/Model.h"

#include <format>

namespace ya
{

namespace
{

template <typename MaterialComponentType, typename ResourceEnum>
void applyImportedTextureVFlip(MaterialComponentType& matComp, const ModelComponent& modelComp)
{
    if (!modelComp._flipImportedTextureV) {
        return;
    }

    for (uint32_t index = 0; index < static_cast<uint32_t>(ResourceEnum::Count); ++index) {
        auto* slot = matComp.getTextureSlot(static_cast<ResourceEnum>(index));
        if (!slot || !slot->hasPath()) {
            continue;
        }

        slot->uvScale.y *= -1.0f;
        slot->uvOffset.y = 1.0f - slot->uvOffset.y;
    }
}

EModelMaterialType resolveMaterialTypeForInstantiation(const ModelComponent& modelComp, const MaterialData* matData)
{
    if (modelComp._materialType != EModelMaterialType::Custom) {
        return modelComp._materialType;
    }

    if (!matData) {
        return EModelMaterialType::Phong;
    }

    if (matData->type == "pbr") {
        return EModelMaterialType::PBR;
    }
    if (matData->type == "unlit") {
        return EModelMaterialType::Unlit;
    }
    return EModelMaterialType::Phong;
}

template <typename MaterialType>
MaterialType* createSharedMaterialForModel(const std::string& label)
{
    return MaterialFactory::get()->createMaterial<MaterialType>(label);
}

template <typename MaterialComponentType>
void assignCustomMaterialPath(MaterialComponentType& matComp, const ModelComponent& modelComp)
{
    if (modelComp.usesCustomMaterial()) {
        matComp._materialPath = modelComp._customMaterialPath;
    }
    else {
        matComp._materialPath.clear();
    }
}

void configurePhongMaterial(PhongMaterialComponent& matComp,
                            Model*                  model,
                            uint32_t                meshIndex,
                            ModelComponent&         modelComp)
{
    assignCustomMaterialPath(matComp, modelComp);

    if (!modelComp._useEmbeddedMaterials) {
        return;
    }

    const auto* matData = model->getMaterialForMesh(meshIndex);
    if (!matData) {
        return;
    }

    const int32_t matIndex = model->getMaterialIndex(meshIndex);
    if (auto it = modelComp._cachedMaterials.find(matIndex); it != modelComp._cachedMaterials.end() && it->second != nullptr) {
        matComp.importFromDescriptorWithSharedMaterial(*matData, static_cast<PhongMaterial*>(it->second));
        applyImportedTextureVFlip<PhongMaterialComponent, PhongMaterial::EResource>(matComp, modelComp);
        return;
    }

    matComp.importFromDescriptor(*matData);
    applyImportedTextureVFlip<PhongMaterialComponent, PhongMaterial::EResource>(matComp, modelComp);
}

void configurePBRMaterial(PBRMaterialComponent& matComp,
                          Model*                model,
                          uint32_t              meshIndex,
                          ModelComponent&       modelComp)
{
    assignCustomMaterialPath(matComp, modelComp);

    if (!modelComp._useEmbeddedMaterials) {
        return;
    }

    const auto* matData = model->getMaterialForMesh(meshIndex);
    if (!matData) {
        return;
    }

    const int32_t matIndex = model->getMaterialIndex(meshIndex);
    if (auto it = modelComp._cachedMaterials.find(matIndex); it != modelComp._cachedMaterials.end() && it->second != nullptr) {
        matComp.importFromDescriptorWithSharedMaterial(*matData, static_cast<PBRMaterial*>(it->second));
        applyImportedTextureVFlip<PBRMaterialComponent, PBRMaterial::EResource>(matComp, modelComp);
        return;
    }

    matComp.importFromDescriptor(*matData);
    applyImportedTextureVFlip<PBRMaterialComponent, PBRMaterial::EResource>(matComp, modelComp);
}

void configureUnlitMaterial(UnlitMaterialComponent& matComp,
                            Model*                  model,
                            uint32_t                meshIndex,
                            ModelComponent&         modelComp)
{
    assignCustomMaterialPath(matComp, modelComp);

    if (!modelComp._useEmbeddedMaterials) {
        return;
    }

    const auto* matData = model->getMaterialForMesh(meshIndex);
    if (!matData) {
        return;
    }

    matComp._baseColor0Slot.textureRef.setPathWithoutNotify("");
    matComp._baseColor1Slot.textureRef.setPathWithoutNotify("");

    if (matData->hasTexture(MatTexture::Diffuse)) {
        matComp._baseColor0Slot.textureRef.setPathWithoutNotify(matData->resolveTexturePath(MatTexture::Diffuse));
    }
    if (matData->hasTexture(MatTexture::Emissive)) {
        matComp._baseColor1Slot.textureRef.setPathWithoutNotify(matData->resolveTexturePath(MatTexture::Emissive));
    }
    if (matData->hasParam(MatParam::BaseColor)) {
        matComp._params.baseColor0 = matData->getParam<glm::vec3>(MatParam::BaseColor, glm::vec3(1.0f));
    }
    else if (matData->hasParam(MatParam::Ambient)) {
        matComp._params.baseColor0 = matData->getParam<glm::vec3>(MatParam::Ambient, glm::vec3(1.0f));
    }

    applyImportedTextureVFlip<UnlitMaterialComponent, UnlitMaterial::EResource>(matComp, modelComp);
    matComp.invalidate();
}

} // namespace

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
        const auto result = modelComp._modelRef.resolve();
        if (result == EAssetResolveResult::Pending) {
            return;
        }

        if (result == EAssetResolveResult::Failed) {
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

    // Mark as managed child — serializer will skip this entity (recreated at runtime)
    childEntity->addComponent<ManagedChildComponent>();

    auto* meshComp = childEntity->addComponent<MeshComponent>();
    meshComp->setFromModel(model->getFilepath(), meshIndex, model->getMesh(meshIndex).get());

    const auto*        matData      = model->getMaterialForMesh(meshIndex);
    const auto         materialType = resolveMaterialTypeForInstantiation(modelComp, matData);
    switch (materialType) {
    case EModelMaterialType::Phong: {
        auto* matComp = childEntity->addComponent<PhongMaterialComponent>();
        configurePhongMaterial(*matComp, model, meshIndex, modelComp);
        break;
    }
    case EModelMaterialType::PBR: {
        auto* matComp = childEntity->addComponent<PBRMaterialComponent>();
        configurePBRMaterial(*matComp, model, meshIndex, modelComp);
        break;
    }
    case EModelMaterialType::Unlit: {
        auto* matComp = childEntity->addComponent<UnlitMaterialComponent>();
        configureUnlitMaterial(*matComp, model, meshIndex, modelComp);
        break;
    }
    case EModelMaterialType::Custom: {
        YA_CORE_WARN("ModelInstantiationSystem: Custom material path '{}' is not wired to a material asset loader yet, falling back to embedded type for '{}'",
                     modelComp._customMaterialPath,
                     modelComp._modelRef.getPath());
        auto* matComp = childEntity->addComponent<PhongMaterialComponent>();
        configurePhongMaterial(*matComp, model, meshIndex, modelComp);
        break;
    }
    }

    return childNode;
}

void ModelInstantiationSystem::buildSharedMaterials(Model* model, ModelComponent& modelComp)
{
    if (!modelComp.canUseSharedEmbeddedMaterialCache() || !model) {
        return;
    }

    const auto& embeddedMaterials = model->getEmbeddedMaterials();
    for (size_t matIndex = 0; matIndex < embeddedMaterials.size(); ++matIndex) {
        std::string matLabel = model->getName() + "_Mat_" + std::to_string(matIndex);
        Material*   material = nullptr;
        switch (modelComp._materialType) {
        case EModelMaterialType::Phong: {
            material = createSharedMaterialForModel<PhongMaterial>(matLabel);
            break;
        }
        case EModelMaterialType::PBR: {
            material = createSharedMaterialForModel<PBRMaterial>(matLabel);
            break;
        }
        case EModelMaterialType::Unlit:
        case EModelMaterialType::Custom: {
            break;
        }
        }

        if (!material) {
            continue;
        }

        modelComp._cachedMaterials[static_cast<int32_t>(matIndex)] = material;
    }
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
