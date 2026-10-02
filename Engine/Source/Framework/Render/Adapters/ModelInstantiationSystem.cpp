#include "Render/Adapters/ModelInstantiationSystem.h"

#include "Hierarchy/Node.h"
#include "Scene3D/Node3D.h"
#include "Scene/Core/Scene.h"

#include "Core/TypeIndex.h"
#include "ECS/SceneBus.h"

#include "Scene3D/ManagedChildComponent.h"

#include "ECS/Component/Material/PBRMaterialComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/Mesh/SkinnedMeshComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "ECS/Component/ModelComponent.h"
#include "ECS/Systems/SkeletonAnimatorComponent.h"
#include "ECS/Entity.h"

#include "Render3D/Material/MaterialFactory.h"
#include "Render3D/ResourceResolveProbe.h"
#include "Render3D/Material/PBRMaterial.h"
#include "Render3D/Material/PhongMaterial.h"
#include "Resource/Model.h"

#include <format>
#include <vector>

namespace ya
{

namespace
{

template <typename MaterialComponentType>
void applyImportedTextureVFlip(MaterialComponentType& matComp, const ModelComponent& modelComp)
{
    if (!modelComp._flipImportedTextureV) {
        return;
    }

    using SlotEnum = typename MaterialComponentType::slot_enum_t;
    for (uint32_t index = 0; index < static_cast<uint32_t>(SlotEnum::Count); ++index) {
        auto* slot = matComp.getTextureSlot(static_cast<SlotEnum>(index));
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

    if (matData->type.empty() &&
        (matData->hasParam(MatParam::Metallic) ||
         matData->hasParam(MatParam::Roughness) ||
         matData->hasTexture(MatTexture::Metallic) ||
         matData->hasTexture(MatTexture::Roughness) ||
         matData->hasTexture(MatTexture::MetallicRoughness) ||
         matData->hasTexture(MatTexture::AO))) {
        return EModelMaterialType::PBR;
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

/**
 * Collect the topmost managed child entities under `node`.
 *
 * Only the top of each branch is reported: Scene::destroyEntity destroys the
 * whole subtree, and reporting descendants as well would leave the caller with
 * pointers that are already freed.
 */
void collectManagedChildEntities(Node* node, entt::registry& registry, std::vector<Entity*>& out)
{
    if (!node) {
        return;
    }

    for (Node* child : node->getChildren()) {
        if (!child) {
            continue;
        }

        Entity* childEntity = child->getEntity();
        if (childEntity && registry.all_of<ManagedChildComponent>(childEntity->getHandle())) {
            out.push_back(childEntity);
            continue;
        }

        collectManagedChildEntities(child, registry, out);
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
        applyImportedTextureVFlip<PhongMaterialComponent>(matComp, modelComp);
        return;
    }

    matComp.importFromDescriptor(*matData);
    applyImportedTextureVFlip<PhongMaterialComponent>(matComp, modelComp);
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
        applyImportedTextureVFlip<PBRMaterialComponent>(matComp, modelComp);
        return;
    }

    matComp.importFromDescriptor(*matData);
    applyImportedTextureVFlip<PBRMaterialComponent>(matComp, modelComp);
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

    matComp._baseColor0Slot.textureRef.setPath("");
    matComp._baseColor1Slot.textureRef.setPath("");

    if (matData->hasTexture(MatTexture::Diffuse)) {
        matComp._baseColor0Slot.textureRef.setPath(matData->resolveTexturePath(MatTexture::Diffuse));
    }
    if (matData->hasTexture(MatTexture::Emissive)) {
        matComp._baseColor1Slot.textureRef.setPath(matData->resolveTexturePath(MatTexture::Emissive));
    }
    if (matData->hasParam(MatParam::BaseColor)) {
        matComp._params.baseColor0 = matData->getParam<glm::vec3>(MatParam::BaseColor, glm::vec3(1.0f));
    }
    else if (matData->hasParam(MatParam::Ambient)) {
        matComp._params.baseColor0 = matData->getParam<glm::vec3>(MatParam::Ambient, glm::vec3(1.0f));
    }

    applyImportedTextureVFlip<UnlitMaterialComponent>(matComp, modelComp);
    matComp.invalidate();
}

} // namespace

void ModelInstantiationSystem::init()
{
    if (bBusSubscribed) {
        return;
    }
    SceneBus& bus = SceneBus::get();
    _componentAddedHandle =
        bus.onComponentAdded.addLambda([this](entt::registry& reg, entt::entity entity, ya::type_index_t type) {
            onComponentAdded(reg, entity, type);
        });
    _componentEditedHandle =
        bus.onComponentEdited.addLambda([this](entt::registry& reg, entt::entity entity, ya::type_index_t type) {
            onComponentEdited(reg, entity, type);
        });
    _componentRemovedHandle =
        bus.onComponentRemoved.addLambda([this](entt::registry& reg, entt::entity entity, ya::type_index_t type) {
            onComponentRemoved(reg, entity, type);
        });
    bBusSubscribed = true;
}

void ModelInstantiationSystem::shutdown()
{
    _sceneWork.clear();
    if (bBusSubscribed) {
        SceneBus& bus = SceneBus::get();
        bus.onComponentAdded.remove(_componentAddedHandle);
        bus.onComponentEdited.remove(_componentEditedHandle);
        bus.onComponentRemoved.remove(_componentRemovedHandle);
        _componentAddedHandle = _componentEditedHandle = _componentRemovedHandle = INVALID_HANDLE;
        bBusSubscribed = false;
    }
}

void ModelInstantiationSystem::onUpdate(float dt)
{
    (void)dt;

    if (!_sceneProvider) {
        return;
    }
    Scene* scene = _sceneProvider();
    if (!scene) {
        return;
    }

    // A scene this tick does not name is not one the last tick left behind,
    // so its work goes here.
    dropWorkAbsentFrom(*scene);

    SceneWork& work = _sceneWork[&scene->getRegistry()];
    work.registry   = &scene->getRegistry();
    if (!work.bSeeded) {
        work.bSeeded = true;
        seedSceneWork(work);
    }

    instantiatePendingModels(*scene, work);
}

void ModelInstantiationSystem::setSceneProvider(SceneProvider provider)
{
    _sceneProvider = std::move(provider);
}

void ModelInstantiationSystem::onComponentAdded(entt::registry& registry, entt::entity entity, ya::type_index_t typeIndex)
{
    if (typeIndex != type_index_v<ModelComponent>) {
        return;
    }
    if (SceneWork* work = findWork(&registry)) {
        enqueueModel(*work, entity);
    }
}

void ModelInstantiationSystem::onComponentEdited(entt::registry& registry, entt::entity entity, ya::type_index_t typeIndex)
{
    // Same reaction as creation: an edited component re-instantiates (the
    // pump cleans up the previous managed children first).
    onComponentAdded(registry, entity, typeIndex);
}

void ModelInstantiationSystem::onComponentRemoved(entt::registry& registry, entt::entity entity, ya::type_index_t typeIndex)
{
    if (typeIndex != type_index_v<ModelComponent>) {
        return;
    }
    if (SceneWork* work = findWork(&registry)) {
        dropEntityWork(*work, entity);
    }
}

ModelInstantiationSystem::SceneWork* ModelInstantiationSystem::findWork(const entt::registry* registry)
{
    auto it = _sceneWork.find(registry);
    return it != _sceneWork.end() ? &it->second : nullptr;
}

void ModelInstantiationSystem::dropWork(SceneWork& work)
{
    work.pendingQueue.clear();
    work.pendingSet.clear();
    work.entityWork.clear();
}

void ModelInstantiationSystem::dropEntityWork(SceneWork& work, entt::entity entity)
{
    work.pendingSet.erase(entity);
    std::erase(work.pendingQueue, entity);
    work.entityWork.erase(entity);
}

void ModelInstantiationSystem::dropWorkAbsentFrom(Scene& scene)
{
    const auto* liveRegistry = &scene.getRegistry();
    for (auto it = _sceneWork.begin(); it != _sceneWork.end();) {
        if (it->first == liveRegistry) {
            ++it;
            continue;
        }
        dropWork(it->second);
        it = _sceneWork.erase(it);
    }
}

void ModelInstantiationSystem::seedSceneWork(SceneWork& work)
{
    // Components that existed before this system subscribed to the bus (test
    // fixtures, scenes rebuilt underneath) would otherwise never enqueue.
    noteResourceResolveView();
    for (auto&& [entity, unused] : work.registry->view<ModelComponent>().each()) {
        (void)unused;
        enqueueModel(work, entity);
    }
}

void ModelInstantiationSystem::enqueueModel(SceneWork& work, entt::entity entity)
{
    auto& registry = *work.registry;
    if (!registry.valid(entity) || !registry.all_of<ModelComponent>(entity)) {
        return;
    }
    if (work.pendingSet.insert(entity).second) {
        work.pendingQueue.push_back(entity);
    }
}

void ModelInstantiationSystem::enqueueFromSlotFill(const entt::registry* registry, entt::entity entity)
{
    // The scene may already be destroyed: look the work up by pointer value
    // and touch nothing else when it is gone.
    SceneWork* work = findWork(registry);
    if (!work) {
        return;
    }
    if (work->pendingSet.insert(entity).second) {
        work->pendingQueue.push_back(entity);
    }
}

void ModelInstantiationSystem::instantiatePendingModels(Scene& scene, SceneWork& work)
{
    auto& registry = *work.registry;

    while (!work.pendingQueue.empty()) {
        const auto entity = work.pendingQueue.front();
        work.pendingQueue.pop_front();
        work.pendingSet.erase(entity);
        if (!registry.valid(entity)) {
            work.entityWork.erase(entity);
            continue;
        }

        ModelComponent& modelComponent = registry.get<ModelComponent>(entity);
        if (modelComponent.isResolved() || !modelComponent.hasModelSource()) {
            work.entityWork.erase(entity);
            continue;
        }

        Entity* entityWrapper = scene.getEntityByEnttID(entity);
        if (!entityWrapper) {
            continue;
        }

        // Hold (or re-bind) the model-slot subscription while the model is
        // not Ready: its fill re-enqueues this entity, so no polling here.
        // A path edit rebinds the ref's slot, so the token is rebuilt here.
        work.entityWork.erase(entity);
        if (const AssetHandle<Model>& handle = modelComponent._modelRef._handle; handle) {
            if (handle->state == EAssetSlotState::Loading) {
                work.entityWork[entity] = SlotSubscription{
                    handle,
                    handle->observers.subscribe([this, registry = work.registry, entity]() {
                        // Slot fills land on the game thread; the callback
                        // only enqueues.
                        enqueueFromSlotFill(registry, entity);
                    }),
                };
                continue;
            }
        }

        try {
            instantiateModel(&scene, entityWrapper, modelComponent);
        }
        catch (const std::exception& e) {
            YA_CORE_ERROR("ModelInstantiationSystem: Failed to instantiate model component: {}", e.what());
            modelComponent._bResolved = true;
        }
        catch (...) {
            YA_CORE_ERROR("ModelInstantiationSystem: Failed to instantiate model component");
            modelComponent._bResolved = true;
        }
    }
}

void ModelInstantiationSystem::instantiateModel(Scene* scene, Entity* entity, ModelComponent& modelComp)
{
    if (!modelComp._modelRef.isLoaded()) {
        // Loading is covered by the held slot subscription (the fill
        // re-enqueues); anything else here is terminal until the component
        // is edited or the asset reloaded.
        const auto state = modelComp._modelRef.getResolveState();
        if (state == EAssetResolveState::Loading) {
            return;
        }

        YA_CORE_WARN("ModelInstantiationSystem: Failed to load model '{}' (state={})",
                     modelComp._modelRef.getPath(),
                     static_cast<int>(state));
        return;
    }

    cleanupChildEntities(scene, entity, modelComp);

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

    SkeletonAnimatorComponent* rootAnimator = attachRootSkeletonAnimator(entity, model);

    for (uint32_t i = 0; i < model->getMeshCount(); ++i) {
        Node* childNode = createMeshNode(scene, entity, model, i, modelComp, rootAnimator);
        if (!childNode) {
            continue;
        }

        childNode->setParent(parentNode);
    }

    YA_CORE_INFO("ModelInstantiationSystem: Created {} child nodes with {} shared materials for model '{}'",
                 model->getMeshCount(),
                 modelComp._cachedMaterials.size(),
                 modelComp._modelRef.getPath());

    modelComp._bResolved = true;
}

Node* ModelInstantiationSystem::createMeshNode(Scene*                      scene,
                                               Entity*                     parentEntity,
                                               Model*                      model,
                                               uint32_t                    meshIndex,
                                               ModelComponent&             modelComp,
                                               SkeletonAnimatorComponent*  rootAnimator)
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

    const int32_t skeletonIndex = model->getMeshSkeletonIndex(meshIndex);
    const bool    isSkinnedMesh = skeletonIndex >= 0 && rootAnimator != nullptr;

    if (isSkinnedMesh) {
        auto* skinnedComp = childEntity->addComponent<SkinnedMeshComponent>();
        skinnedComp->setFromModel(model->getFilepath(), meshIndex, model->getMesh(meshIndex).get());
        skinnedComp->_animator = rootAnimator;
    }
    else {
        auto* staticComp = childEntity->addComponent<StaticMeshComponent>();
        staticComp->setFromModel(model->getFilepath(), meshIndex, model->getMesh(meshIndex).get());
    }

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

SkeletonAnimatorComponent* ModelInstantiationSystem::attachRootSkeletonAnimator(Entity* parentEntity, Model* model)
{
    if (!parentEntity || !model || !model->hasSkeleton()) {
        return nullptr;
    }

    if (model->getSkeletonCount() > 1) {
        YA_CORE_WARN("ModelInstantiationSystem: Model '{}' has {} skeletons; only the first is attached. Multi-skeleton support is not implemented yet.",
                     model->getFilepath(),
                     model->getSkeletonCount());
    }

    auto skeleton = model->getSkeletonShared(0);
    if (!skeleton) {
        return nullptr;
    }

    if (skeleton->animations.empty()) {
        return nullptr;
    }

    auto* animator = parentEntity->addComponent<SkeletonAnimatorComponent>();
    animator->setFromModel(model->getFilepath(), /*meshIndex*/ 0, /*skeletonIndex*/ 0, std::move(skeleton));
    return animator;
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

void ModelInstantiationSystem::cleanupChildEntities(Scene* scene, Entity* parentEntity, ModelComponent& modelComp)
{
    for (auto& [matIndex, material] : modelComp._cachedMaterials) {
        (void)matIndex;
        if (material) {
            MaterialFactory::get()->destroyMaterial(material);
        }
    }
    modelComp._cachedMaterials.clear();

    // Discover the instance's children through the scene instead of a ledger on
    // the component: an author may have deleted individual meshes since the last
    // instantiation, and a stored Node* list cannot see that. Scene::destroyEntity
    // cascades, so destroying the topmost managed child of each branch takes the
    // rest of that branch with it.
    if (Node* parentNode = scene->getNodeByEntity(parentEntity)) {
        std::vector<Entity*> managedChildren;
        collectManagedChildEntities(parentNode, scene->getRegistry(), managedChildren);
        for (Entity* childEntity : managedChildren) {
            scene->destroyEntity(childEntity);
        }
    }

    // Remove the root-level animator attached by a previous instantiation, if any.
    // Skinned meshes held only a raw pointer to it, which becomes dangling after
    // their child entities are destroyed above.
    if (parentEntity && parentEntity->hasComponent<SkeletonAnimatorComponent>()) {
        parentEntity->removeComponent<SkeletonAnimatorComponent>();
    }
}

} // namespace ya
