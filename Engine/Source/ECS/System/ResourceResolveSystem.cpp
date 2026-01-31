#include "ResourceResolveSystem.h"
#include "Core/App/App.h"
#include "Core/AssetManager.h"
#include "ECS/Component/2D/UIComponent.h"
#include "Scene/Node.h"
#include "Scene/SceneManager.h"


#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/ModelComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/Entity.h"
#include "Render/Material/MaterialFactory.h"
#include "Render/Model.h"
#include "Resource/TextureLibrary.h"



namespace ya
{

void ResourceResolveSystem::onUpdate(float dt)
{
    // YA_PROFILE_FUNCTION_LOG();
    auto  sceneManager = App::get()->getSceneManager();
    auto *scene        = sceneManager->getActiveScene();
    if (!scene) return;

    auto &registry = scene->getRegistry();

    // 1. Resolve ModelComponents (creates child entities)
    registry.view<ModelComponent>().each([&](auto entityHandle, ModelComponent &modelComponent) {
        if (!modelComponent.isResolved() && modelComponent.hasModelSource()) {
            Entity *entity = scene->getEntityByEnttID(entityHandle);
            if (entity) {
                try {
                    resolveModelComponent(scene, entity, modelComponent);
                }
                catch (const std::exception &e) {
                    YA_CORE_ERROR("ResourceResolveSystem: Failed to resolve model component: {}", e.what());
                    // Mark as resolved to avoid retrying
                    modelComponent._bResolved = true;
                }
                catch (...) {
                    YA_CORE_ERROR("ResourceResolveSystem: Failed to resolve model component");
                    // Mark as resolved to avoid retrying
                    modelComponent._bResolved = true;
                }
            }
        }
    });

    // 2. Resolve MeshComponents (primitives or mesh from Model)
    registry.view<MeshComponent>().each([&](auto entity, MeshComponent &meshComponent) {
        if (!meshComponent.isResolved() && meshComponent.hasMeshSource()) {
            meshComponent.resolve();
        }
    });

    // 3. Resolve PhongMaterialComponents
    registry.view<PhongMaterialComponent>().each([&](auto entity, PhongMaterialComponent &materialComponent) {
        if (!materialComponent.isResolved()) {
            materialComponent.resolve();
        }
    });

    registry.view<UIComponent>().each([&](auto entity, UIComponent &uiComponent) {
        if (!uiComponent.view.textureRef.isLoaded() &&
            uiComponent.view.textureRef.hasPath()) {
            uiComponent.view.textureRef.resolve();
        }
    });

    // Add more component types here as needed:
    // - PBRMaterialComponent
    // - SkeletalMeshComponent
    // - etc.
}

void ResourceResolveSystem::resolveModelComponent(Scene *scene, Entity *entity, ModelComponent &modelComp)
{
    // Clean up existing child entities if re-resolving
    cleanupChildEntities(scene, modelComp);

    // Load the Model asset
    if (!modelComp._modelRef.isLoaded()) {
        if (!modelComp._modelRef.resolve()) {
            YA_CORE_WARN("ResourceResolveSystem: Failed to load model '{}'",
                         modelComp._modelRef.getPath());
            return;
        }
    }

    Model *model = modelComp.getModel();
    if (!model || model->getMeshCount() == 0) {
        YA_CORE_WARN("ResourceResolveSystem: Model '{}' has no meshes",
                     modelComp._modelRef.getPath());
        modelComp._bResolved = true;
        return;
    }

    // If auto-create is disabled, just mark as resolved
    if (!modelComp._autoCreateChildEntities) {
        modelComp._bResolved = true;
        return;
    }

    // Get parent node for hierarchy
    Node *parentNode = scene->getNodeByEntity(entity);
    YA_CORE_ASSERT(parentNode != nullptr, "Parent entity has no Node");

    // Pre-create shared materials for each unique embedded material
    // This ensures meshes with the same material index share the same runtime material
    if (modelComp._useEmbeddedMaterials) {
        const auto &embeddedMaterials = model->getEmbeddedMaterials();
        for (size_t matIndex = 0; matIndex < embeddedMaterials.size(); ++matIndex) {
            // Create material with model name + material index as label
            std::string matLabel = model->getName() + "_Mat_" + std::to_string(matIndex);
            auto       *litMat   = MaterialFactory::get()->createMaterial<PhongMaterial>(matLabel);

            if (litMat) {
                // Initialize material from embedded data
                const auto &embeddedMat = embeddedMaterials[matIndex];
                initSharedMaterial(litMat, &embeddedMat, model->getDirectory());
                modelComp._cachedMaterials[static_cast<int32_t>(matIndex)] = litMat;
                YA_CORE_TRACE("ResourceResolveSystem: Created shared material '{}' for model '{}'",
                              matLabel,
                              model->getName());
            }
        }
    }

    // Create child nodes for each mesh
    for (uint32_t i = 0; i < model->getMeshCount(); ++i) {
        Node *childNode = createMeshNode(
            scene,
            entity,
            model,
            i,
            modelComp);

        if (childNode) {
            // Establish parent-child relationship
            childNode->setParent(parentNode);
            modelComp._childNodes.push_back(childNode);
        }
    }

    YA_CORE_INFO("ResourceResolveSystem: Created {} child nodes with {} shared materials for model '{}'",
                 model->getMeshCount(),
                 modelComp._cachedMaterials.size(),
                 modelComp._modelRef.getPath());

    modelComp._bResolved = true;
}

Node *ResourceResolveSystem::createMeshNode(
    Scene          *scene,
    Entity         *parentEntity,
    Model          *model,
    uint32_t        meshIndex,
    ModelComponent &modelComp)
{
    // Generate name for child node
    std::string meshName = model->getMesh(meshIndex)->getName();
    if (meshName.empty()) {
        meshName = std::format("Mesh_{}", meshIndex);
    }
    std::string nodeName = std::format("{}_{}", parentEntity->getName(), meshName);

    // Get parent node for hierarchy
    Node *parentNode = scene->getNodeByEntity(parentEntity);

    // Create new node with parent relationship
    Node *childNode = scene->createNode3D(nodeName, parentNode);
    if (!childNode) {
        YA_CORE_ERROR("ResourceResolveSystem: Failed to create child node '{}'", nodeName);
        return nullptr;
    }

    // Cast to Node3D to access Entity
    auto   *childNode3D = dynamic_cast<Node3D *>(childNode);
    Entity *childEntity = childNode3D ? childNode3D->getEntity() : nullptr;
    if (!childEntity) {
        YA_CORE_ERROR("ResourceResolveSystem: Child node has no entity");
        return nullptr;
    }

    // ★ 子节点使用默认的局部变换（相对于父节点）
    // TransformComponent 已经在 createNode 时自动创建，默认值为单位变换
    // 不需要复制父节点的变换，否则会导致双重偏移

    // Add MeshComponent
    auto *meshComp = childEntity->addComponent<MeshComponent>();
    meshComp->setFromModel(
        model->getFilepath(),
        meshIndex,
        model->getMesh(meshIndex).get());

    // Add PhongMaterialComponent
    if (auto *matComp = childEntity->addComponent<PhongMaterialComponent>()) {

        if (modelComp._useEmbeddedMaterials) {
            // Get material index for this mesh
            int32_t matIndex = model->getMaterialIndex(meshIndex);

            // Try to use shared material from cache
            auto it = modelComp._cachedMaterials.find(matIndex);
            if (it != modelComp._cachedMaterials.end() && it->second != nullptr) {
                // Use shared material
                matComp->setSharedMaterial(it->second);
                matComp->_bResolved = true; // Already resolved
                YA_CORE_TRACE("ResourceResolveSystem: Mesh {} using shared material index {}", meshIndex, matIndex);
            }
            else {
                // No cached material, initialize from embedded (will create own material on resolve)
                const EmbeddedMaterial *embeddedMat = model->getMaterialForMesh(meshIndex);
                initMaterialFromEmbedded(*matComp, embeddedMat, model->getDirectory());
            }
        }
    }
    // If no embedded material or useEmbeddedMaterials is false,
    // matComp uses default values (which is fine)

    return childNode;
}

void ResourceResolveSystem::initSharedMaterial(
    PhongMaterial          *material,
    const EmbeddedMaterial *embeddedMat,
    const std::string      &modelDirectory)
{
    if (!material || !embeddedMat) {
        return;
    }

    // Set material parameters
    auto &params     = material->getParamsMut();
    params.ambient   = embeddedMat->ambient;
    params.diffuse   = glm::vec3(embeddedMat->baseColor);
    params.specular  = embeddedMat->specular;
    params.shininess = embeddedMat->shininess;
    material->setParamDirty();

    // Helper to resolve texture path
    auto resolvePath = [&modelDirectory](const std::string &texPath) -> std::string {
        if (texPath.empty()) {
            return "";
        }
        if (texPath.find(':') != std::string::npos || texPath[0] == '/') {
            return texPath;
        }
        return modelDirectory + texPath;
    };

    // Load and set textures
    auto defaultSampler = TextureLibrary::get().getDefaultSampler();

    // Diffuse texture
    if (!embeddedMat->diffuseTexturePath.empty()) {
        std::string path    = resolvePath(embeddedMat->diffuseTexturePath);
        auto        texture = AssetManager::get()->loadTexture(path);
        if (texture) {
            TextureView tv;
            tv.texture = texture;
            tv.sampler = defaultSampler;
            material->setTextureView(PhongMaterial::DiffuseTexture, tv);
        }
    }

    // Specular texture
    if (!embeddedMat->specularTexturePath.empty()) {
        std::string path    = resolvePath(embeddedMat->specularTexturePath);
        auto        texture = AssetManager::get()->loadTexture(path);
        if (texture) {
            TextureView tv;
            tv.texture = texture;
            tv.sampler = defaultSampler;
            material->setTextureView(PhongMaterial::SpecularTexture, tv);
        }
    }
}

void ResourceResolveSystem::initMaterialFromEmbedded(
    PhongMaterialComponent &matComp,
    const EmbeddedMaterial *embeddedMat,
    const std::string      &modelDirectory)
{
    if (!embeddedMat) {
        return; // Use default material
    }
    matComp.createDefaultMaterial();
    auto &params     = matComp.getMaterial()->getParamsMut();
    params.ambient   = embeddedMat->ambient;
    params.diffuse   = glm::vec3(embeddedMat->baseColor);
    params.specular  = embeddedMat->specular;
    params.shininess = embeddedMat->shininess;
    matComp.getMaterial()->setParamDirty();

    // Set texture paths (resolve relative to model directory)
    auto resolvePath = [&modelDirectory](const std::string &texPath) -> std::string {
        if (texPath.empty()) {
            return "";
        }
        // If already absolute, use as-is
        if (texPath.find(':') != std::string::npos || texPath[0] == '/') {
            return texPath;
        }
        // Make relative to model directory
        return modelDirectory + texPath;
    };

    // Diffuse texture
    if (!embeddedMat->diffuseTexturePath.empty()) {
        std::string path = resolvePath(embeddedMat->diffuseTexturePath);
        matComp.setTextureSlot(PhongMaterial::DiffuseTexture, path);
    }

    // Specular texture
    if (!embeddedMat->specularTexturePath.empty()) {
        std::string path = resolvePath(embeddedMat->specularTexturePath);
        matComp.setTextureSlot(PhongMaterial::SpecularTexture, path);
    }

    // Mark as needing resolve
    matComp.invalidate();
}

void ResourceResolveSystem::cleanupChildEntities(Scene *scene, ModelComponent &modelComp)
{
    // Destroy cached shared materials
    for (auto &[matIndex, material] : modelComp._cachedMaterials) {
        if (material) {
            MaterialFactory::get()->destroyMaterial(material);
        }
    }
    modelComp._cachedMaterials.clear();

    // Destroy child nodes
    for (Node *childNode : modelComp._childNodes) {
        if (childNode) {
            scene->destroyNode(childNode);
        }
    }
    modelComp._childNodes.clear();
}

} // namespace ya
