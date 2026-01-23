#include "ResourceResolveSystem.h"
#include "Core/App/App.h"
#include "Core/AssetManager.h"
#include "Scene/Node.h"
#include "Scene/SceneManager.h"


#include "ECS/Component/Material/LitMaterialComponent.h"
#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/ModelComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/Entity.h"
#include "Render/Model.h"


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
                resolveModelComponent(scene, entity, modelComponent);
            }
        }
    });

    // 2. Resolve MeshComponents (primitives or mesh from Model)
    registry.view<MeshComponent>().each([&](auto entity, MeshComponent &meshComponent) {
        if (!meshComponent.isResolved() && meshComponent.hasMeshSource()) {
            meshComponent.resolve();
        }
    });

    // 3. Resolve LitMaterialComponents
    registry.view<LitMaterialComponent>().each([&](auto entity, LitMaterialComponent &materialComponent) {
        if (!materialComponent.isResolved()) {
            materialComponent.resolve();
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
    // if (!parentNode) {
    //     YA_CORE_WARN("ResourceResolveSystem: Parent entity has no Node, creating one");
    //     parentNode = scene->createNodeForEntity(entity);
    // }

    // Create child nodes for each mesh
    for (uint32_t i = 0; i < model->getMeshCount(); ++i) {
        Node *childNode = createMeshNode(
            scene,
            entity,
            model,
            i,
            modelComp._useEmbeddedMaterials);

        if (childNode) {
            // Establish parent-child relationship
            childNode->setParent(parentNode);
            modelComp._childNodes.push_back(childNode);
        }
    }

    YA_CORE_INFO("ResourceResolveSystem: Created {} child nodes for model '{}'",
                 model->getMeshCount(),
                 modelComp._modelRef.getPath());

    modelComp._bResolved = true;
}

Node *ResourceResolveSystem::createMeshNode(
    Scene   *scene,
    Entity  *parentEntity,
    Model   *model,
    uint32_t meshIndex,
    bool     useEmbeddedMaterial)
{
    // Generate name for child node
    std::string meshName = model->getMesh(meshIndex)->getName();
    if (meshName.empty()) {
        meshName = "Mesh_" + std::to_string(meshIndex);
    }
    std::string nodeName = parentEntity->getName() + "_" + meshName;

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

    // Add LitMaterialComponent
    auto *matComp = childEntity->addComponent<LitMaterialComponent>();

    if (useEmbeddedMaterial) {
        const EmbeddedMaterial *embeddedMat = model->getMaterialForMesh(meshIndex);
        initMaterialFromEmbedded(*matComp, embeddedMat, model->getDirectory());
    }
    // If no embedded material or useEmbeddedMaterial is false,
    // matComp uses default values (which is fine)

    return childNode;
}

void ResourceResolveSystem::initMaterialFromEmbedded(
    LitMaterialComponent   &matComp,
    const EmbeddedMaterial *embeddedMat,
    const std::string      &modelDirectory)
{
    if (!embeddedMat) {
        return; // Use default material
    }

    // Set material parameters
    matComp._params.ambient   = embeddedMat->ambient;
    matComp._params.diffuse   = glm::vec3(embeddedMat->baseColor);
    matComp._params.specular  = embeddedMat->specular;
    matComp._params.shininess = embeddedMat->shininess;

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
        matComp.setTextureSlot(LitMaterial::DiffuseTexture, path);
    }

    // Specular texture
    if (!embeddedMat->specularTexturePath.empty()) {
        std::string path = resolvePath(embeddedMat->specularTexturePath);
        matComp.setTextureSlot(LitMaterial::SpecularTexture, path);
    }

    // Mark as needing resolve
    matComp.invalidate();
}

void ResourceResolveSystem::cleanupChildEntities(Scene *scene, ModelComponent &modelComp)
{
    for (Node *childNode : modelComp._childNodes) {
        if (childNode) {
            scene->destroyNode(childNode);
        }
    }
    modelComp._childNodes.clear();
}

} // namespace ya
