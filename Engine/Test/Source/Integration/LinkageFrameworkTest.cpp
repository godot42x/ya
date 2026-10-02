#include "ECS/Linkage/LinkageFramework.h"
#include "Render/Adapters/Companion/CompanionManager.h"
#include "Render/Adapters/Companion/RenderCompanionSpecs.h"
#include "Render/Adapters/Material/MaterialRenderLinkageRule.h"

#include "ECS/Component/2D/BillboardComponent.h"
#include "ECS/Component/Material/PBRMaterialComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "ECS/Component/RenderComponent.h"
#include "ECS/Systems/Components/CameraComponent.h"
#include "GameRuntime/Render/SceneCameraQuery.h"
#include "ECS/Systems/Components/DirectionalLightComponent.h"
#include "ECS/Systems/Components/PointLightComponent.h"
#include "Scene/Core/Scene.h"
#include "Scene/Runtime/SceneManager.h"
#include "Scene3D/ManagedChildComponent.h"
#include "Scene3D/TransformComponent.h"
#include "ECS/Systems/TransformSystem.h"

#include <gtest/gtest.h>

#include <functional>
#include <utility>
#include <vector>

namespace ya
{
namespace
{

class FrameTaskCapture
{
  public:
    std::vector<std::function<void()>> tasks;

    void operator()(std::function<void()> task)
    {
        tasks.push_back(std::move(task));
    }

    void drain()
    {
        // Tasks may schedule more work while running; drain until stable.
        while (!tasks.empty()) {
            auto pending = std::move(tasks);
            tasks.clear();
            for (auto& task : pending) {
                task();
            }
        }
    }

    size_t size() const
    {
        return tasks.size();
    }
};

/// Scenes register themselves with the (global) lifecycle host at
/// construction; bind the test's SceneManager the same way the app does so
/// isSceneValid() works, and restore afterwards.
class SceneLifecycleHostScope
{
  public:
    explicit SceneLifecycleHostScope(ISceneLifecycleHost* host)
    {
        Scene::setLifecycleHost(host);
    }

    ~SceneLifecycleHostScope()
    {
        Scene::setLifecycleHost(nullptr);
    }
};

} // namespace

namespace
{

/// The render-side companion declarations the Host composition root makes:
/// the camera draws a body, point/directional lights draw an icon, all as
/// generated editor companions.
void addCompanionRule(LinkageFramework& framework)
{
    auto manager = std::make_shared<CompanionManager>(&framework);
    manager->declareHost<CameraComponent>(makeCameraCompanionSpec(CameraCompanionPolicy{}));
    manager->declareHost<PointLightComponent>(makePointLightCompanionSpec(LightBillboardConfig{}));
    manager->declareHost<DirectionalLightComponent>(makeDirectionalLightCompanionSpec(LightBillboardConfig{}));
    framework.addRule(manager);
}

} // namespace

// Regression: rules must disconnect their entt signal connections when the
// scene is destroyed (onSceneUnload, fired before the registry dies). A
// connected rule would otherwise receive teardown on_destroy events and, if
// the framework/rule was already destroyed (Host shutdown before scene
// teardown), call into freed memory.
TEST(LinkageFrameworkTest, RulesDisconnectOnSceneUnload)
{
    SceneManager sceneManager;
    SceneLifecycleHostScope lifecycleHost(&sceneManager);
    FrameTaskCapture sink;
    LinkageFramework framework;
    framework.setSceneManager(&sceneManager);
    framework.setFrameTaskSink(std::ref(sink));
    framework.addRule(std::make_shared<MaterialRenderLinkageRule>(&framework));
    addCompanionRule(framework);
    framework.init();

    stdptr<Scene> scene = std::make_shared<Scene>("LinkageScene");
    auto*         node  = scene->createNode3D("Body");
    node->getEntity()->addComponent<PBRMaterialComponent>();
    node->getEntity()->addComponent<PointLightComponent>();
    ASSERT_TRUE(sceneManager.activateScene(scene));

    auto& registry = scene->getRegistry();
    // Connected after scene init (sweep also schedules deferred linkage).
    ASSERT_FALSE(registry.on_construct<PBRMaterialComponent>().empty());
    ASSERT_FALSE(registry.on_construct<PointLightComponent>().empty());
    ASSERT_FALSE(registry.on_construct<CameraComponent>().empty());
    ASSERT_FALSE(registry.on_update<TransformComponent>().empty());
    sink.drain();
    ASSERT_TRUE(node->getEntity()->hasComponent<RenderComponent>());

    // Observe the disconnect from inside onSceneDestroy: by the time the
    // framework's handler ran, every rule signal must be gone from the
    // registry that is about to die.
    bool disconnected = false;
    int  observer;
    sceneManager.onSceneDestroy.addLambda(&observer, [&disconnected, scene](Scene* dying) {
        if (dying != scene.get()) {
            return;
        }
        auto& reg = dying->getRegistry();
        disconnected =
            reg.on_construct<PBRMaterialComponent>().empty() &&
            reg.on_construct<PhongMaterialComponent>().empty() &&
            reg.on_construct<UnlitMaterialComponent>().empty() &&
            reg.on_construct<SimpleMaterialComponent>().empty() &&
            reg.on_construct<PointLightComponent>().empty() &&
            reg.on_construct<DirectionalLightComponent>().empty() &&
            reg.on_construct<CameraComponent>().empty() &&
            reg.on_update<TransformComponent>().empty();
    });

    const size_t tasksBeforeDestroy = sink.size();
    // Destroy through the manager's active-scene path: this is the same
    // reference-alias flow the app uses at quit (unloadScene -> destroyScene
    // on _activeScene). Regression: onSceneDestroy must be broadcast BEFORE
    // the scene's last reference is released.
    ASSERT_TRUE(sceneManager.unloadScene());
    ASSERT_TRUE(disconnected) << "rule signals must be disconnected before registry teardown";
    // Teardown must not schedule any linkage work.
    ASSERT_EQ(sink.size(), tasksBeforeDestroy);

    framework.shutdown();
}

// Regression: a rule destroyed while its scene is still alive must
// disconnect first, so destroying the scene afterwards cannot call into the
// freed rule (this is the Host shutdown ordering: systems die before scenes).
TEST(LinkageFrameworkTest, RuleDestroyedBeforeSceneTeardownIsSafe)
{
    SceneManager sceneManager;
    SceneLifecycleHostScope lifecycleHost(&sceneManager);
    FrameTaskCapture sink;
    LinkageFramework framework;
    framework.setSceneManager(&sceneManager);
    framework.setFrameTaskSink(std::ref(sink));
    framework.addRule(std::make_shared<MaterialRenderLinkageRule>(&framework));
    framework.init();

    stdptr<Scene> scene = std::make_shared<Scene>("LinkageScene");
    auto*         node  = scene->createNode3D("Body");
    node->getEntity()->addComponent<PBRMaterialComponent>();
    ASSERT_TRUE(sceneManager.activateScene(scene));
    sink.drain();

    // Framework shutdown destroys the rules; the scene stays alive.
    framework.shutdown();

    // Scene teardown afterwards must not dereference the destroyed rule.
    EXPECT_NO_FATAL_FAILURE(sceneManager.destroyScene(scene));
}

TEST(LinkageFrameworkTest, CameraComponentGetsGeneratedBodyCompanion)
{
    SceneManager sceneManager;
    SceneLifecycleHostScope lifecycleHost(&sceneManager);
    FrameTaskCapture sink;
    LinkageFramework framework;
    framework.setSceneManager(&sceneManager);
    framework.setFrameTaskSink(std::ref(sink));
    framework.addRule(std::make_shared<MaterialRenderLinkageRule>(&framework));
    addCompanionRule(framework);
    framework.init();

    // No declarative rule knows about CameraComponent yet, so the body has to
    // come from the manager's initial sweep rather than from a construct event.
    stdptr<Scene> scene = std::make_shared<Scene>("CameraCompanionScene");
    auto*         node  = scene->createNode3D("Cam");
    node->getEntity()->addComponent<CameraComponent>();
    ASSERT_TRUE(sceneManager.activateScene(scene));
    sink.drain();

    Entity* camera = node->getEntity();

    // The body is its own entity under the camera node: the camera keeps its
    // mesh and material slots free for author content.
    EXPECT_FALSE(camera->hasComponent<StaticMeshComponent>());

    Entity* body = CompanionManager::findCompanion(*scene, *camera);
    ASSERT_NE(body, nullptr);
    EXPECT_NE(body, camera);
    EXPECT_EQ(CompanionManager::hostOf(*body), camera);
    EXPECT_EQ(CompanionManager::kindOf(*body), ECompanionKind::EditorGizmo);
    EXPECT_FALSE(CompanionManager::isAuthorEditable(*body));
    EXPECT_EQ(CompanionManager::packClassOf(*body), EAssetPackClass::EditorOnly);
    EXPECT_EQ(CompanionManager::featureMaskOf(*body), toMask(ERenderFeature::Gizmo));
    EXPECT_EQ(CompanionManager::hostEntityIdOf(*body), static_cast<uint32_t>(camera->getHandle()));
    EXPECT_TRUE(CompanionManager::isGeneratedCompanion(*body));
    EXPECT_TRUE(body->hasComponent<ManagedChildComponent>());

    // The body is an ordinary mesh source pointing at engine content. Nothing
    // about that path reaches scene data, because the companion itself is
    // derived and never serialized.
    ASSERT_TRUE(body->hasComponent<StaticMeshComponent>());
    auto* bodyMesh = body->getComponent<StaticMeshComponent>();
    EXPECT_EQ(bodyMesh->_mesh._sourceModelPath, CameraCompanionPolicy{}.meshPath);
    EXPECT_EQ(bodyMesh->_mesh._meshIndex, CameraCompanionPolicy{}.meshIndex);
    EXPECT_EQ(bodyMesh->_mesh._primitiveGeometry, EPrimitiveGeometry::None);
    // Shaded, not tinted flat: a solid with no lighting reads as a silhouette.
    ASSERT_TRUE(body->hasComponent<PhongMaterialComponent>());
    const PhongMaterialComponent* bodyMaterial = body->getComponent<PhongMaterialComponent>();
    EXPECT_EQ(bodyMaterial->_params.diffuse, CameraCompanionPolicy{}.diffuse);
    EXPECT_FALSE(body->hasComponent<UnlitMaterialComponent>());

    scene->removeComponent<CameraComponent>(camera->getHandle());
    sink.drain();
    EXPECT_EQ(CompanionManager::findCompanion(*scene, *camera), nullptr);

    framework.shutdown();
}

TEST(LinkageFrameworkTest, CameraCompanionLeavesHostMeshAlone)
{
    SceneManager sceneManager;
    SceneLifecycleHostScope lifecycleHost(&sceneManager);
    FrameTaskCapture sink;
    LinkageFramework framework;
    framework.setSceneManager(&sceneManager);
    framework.setFrameTaskSink(std::ref(sink));
    addCompanionRule(framework);
    framework.init();

    stdptr<Scene> scene = std::make_shared<Scene>("CameraUserMeshScene");
    auto*         node  = scene->createNode3D("Cam");
    auto*         mesh  = node->getEntity()->addComponent<StaticMeshComponent>();
    ASSERT_NE(mesh, nullptr);
    mesh->setPrimitiveGeometry(EPrimitiveGeometry::Cube);
    node->getEntity()->addComponent<CameraComponent>();
    ASSERT_TRUE(sceneManager.activateScene(scene));
    sink.drain();

    auto* entity = node->getEntity();
    ASSERT_TRUE(entity->hasComponent<StaticMeshComponent>());
    mesh = entity->getComponent<StaticMeshComponent>();
    EXPECT_EQ(mesh->_mesh._primitiveGeometry, EPrimitiveGeometry::Cube);
    EXPECT_TRUE(mesh->_mesh._sourceModelPath.empty());

    // The author's mesh and the generated body coexist on one camera.
    Entity* body = CompanionManager::findCompanion(*scene, *entity);
    ASSERT_NE(body, nullptr);
    EXPECT_NE(body, entity);
    ASSERT_TRUE(body->hasComponent<StaticMeshComponent>());
    EXPECT_EQ(body->getComponent<StaticMeshComponent>()->_mesh._sourceModelPath,
              CameraCompanionPolicy{}.meshPath);

    scene->removeComponent<CameraComponent>(entity->getHandle());
    sink.drain();
    EXPECT_TRUE(entity->hasComponent<StaticMeshComponent>());
    EXPECT_EQ(CompanionManager::findCompanion(*scene, *entity), nullptr);

    framework.shutdown();
}

// The companion is a child node, so it is supposed to follow the host for free
// through the node hierarchy -- no per-frame copying. That only holds if the
// generated node really is parented under the host; otherwise the body would be
// drawn at the world origin, which reads as "the camera has no model" whenever
// the camera is not at the origin.
TEST(LinkageFrameworkTest, CameraBodyFollowsItsHostThroughTheHierarchy)
{
    SceneManager sceneManager;
    SceneLifecycleHostScope lifecycleHost(&sceneManager);
    FrameTaskCapture sink;
    LinkageFramework framework;
    framework.setSceneManager(&sceneManager);
    framework.setFrameTaskSink(std::ref(sink));
    addCompanionRule(framework);
    framework.init();

    const glm::vec3 cameraPos{3.0f, 4.0f, 5.0f};

    stdptr<Scene> scene = std::make_shared<Scene>("CompanionFollowScene");
    auto*         node  = scene->createNode3D("Cam");
    ASSERT_NE(node, nullptr);
    auto* cameraTc = node->getEntity()->getComponent<TransformComponent>();
    ASSERT_NE(cameraTc, nullptr);
    cameraTc->setPosition(cameraPos);
    node->getEntity()->addComponent<CameraComponent>();
    ASSERT_TRUE(sceneManager.activateScene(scene));
    sink.drain();

    Entity* body = CompanionManager::findCompanion(*scene, *node->getEntity());
    ASSERT_NE(body, nullptr);

    // The companion must be parented under the host node, not orphaned.
    Node* bodyNode = scene->getNodeByEntity(body);
    ASSERT_NE(bodyNode, nullptr);
    ASSERT_NE(bodyNode->getParent(), nullptr);
    EXPECT_EQ(bodyNode->getParent()->getEntity(), node->getEntity());

    auto* bodyTc = body->getComponent<TransformComponent>();
    ASSERT_NE(bodyTc, nullptr);

    TransformSystem::computeLocalMatrix(cameraTc);
    TransformSystem::computeWorldMatrix(cameraTc);
    TransformSystem::computeLocalMatrix(bodyTc);
    TransformSystem::computeWorldMatrix(bodyTc);

    const glm::vec3 bodyWorld = glm::vec3(bodyTc->getWorldMatrix()[3]);
    EXPECT_NEAR(bodyWorld.x, cameraPos.x, 1e-4f);
    EXPECT_NEAR(bodyWorld.y, cameraPos.y, 1e-4f);
    EXPECT_NEAR(bodyWorld.z, cameraPos.z, 1e-4f);

    framework.shutdown();
}

// The camera's own view is what the preview inset and the FOV wireframe are
// built from, while the body is a mesh placed by the entity's world matrix.
// Both must describe the same camera, so a host transform inherited from a
// parent has to reach the view exactly like it reaches the mesh.
TEST(LinkageFrameworkTest, CameraViewAgreesWithItsBodyUnderAParentTransform)
{
    SceneManager sceneManager;
    SceneLifecycleHostScope lifecycleHost(&sceneManager);
    FrameTaskCapture sink;
    LinkageFramework framework;
    framework.setSceneManager(&sceneManager);
    framework.setFrameTaskSink(std::ref(sink));
    addCompanionRule(framework);
    framework.init();

    stdptr<Scene> scene = std::make_shared<Scene>("CameraViewPlacementScene");

    // The parent carries both a translation and a rotation, so a view built
    // from the camera's local transform lands somewhere else entirely.
    auto* pivot = scene->createNode3D("Pivot");
    ASSERT_NE(pivot, nullptr);
    auto* pivotTc = pivot->getEntity()->getComponent<TransformComponent>();
    ASSERT_NE(pivotTc, nullptr);
    pivotTc->setPosition({10.0f, 0.0f, 0.0f});
    pivotTc->setRotation({0.0f, 90.0f, 0.0f});

    auto* cameraNode = scene->createNode3D("Cam", pivot);
    ASSERT_NE(cameraNode, nullptr);
    auto* cameraTc = cameraNode->getEntity()->getComponent<TransformComponent>();
    ASSERT_NE(cameraTc, nullptr);
    cameraTc->setRotation({0.0f, 30.0f, 0.0f});

    auto* camera = cameraNode->getEntity()->addComponent<CameraComponent>();
    ASSERT_NE(camera, nullptr);

    ASSERT_TRUE(sceneManager.activateScene(scene));
    sink.drain();

    Entity* body = CompanionManager::findCompanion(*scene, *cameraNode->getEntity());
    ASSERT_NE(body, nullptr);
    auto* bodyTc = body->getComponent<TransformComponent>();
    ASSERT_NE(bodyTc, nullptr);

    TransformSystem::computeWorldMatrix(cameraTc);
    TransformSystem::computeWorldMatrix(bodyTc);

    const glm::mat4 world        = cameraTc->getWorldMatrix();
    const glm::vec3 worldEye     = glm::vec3(world[3]);
    const glm::vec3 worldForward = cameraTc->getForward();

    // The mesh is placed by the world matrix; that is the camera's real pose.
    const glm::vec3 bodyEye     = glm::vec3(bodyTc->getWorldMatrix()[3]);
    const glm::vec3 bodyForward = bodyTc->getForward();
    EXPECT_NEAR(glm::length(bodyEye - worldEye), 0.0f, 1e-4f);
    EXPECT_NEAR(glm::dot(glm::normalize(bodyForward), glm::normalize(worldForward)), 1.0f, 1e-4f);

    // The view the preview and the wireframe use has to be the same pose.
    const glm::mat4 inverseView = glm::inverse(cameraView(*cameraNode->getEntity()));
    const glm::vec3 viewEye     = glm::vec3(inverseView[3]);
    const glm::vec3 viewForward = -glm::vec3(inverseView[2]);
    EXPECT_NEAR(glm::length(viewEye - worldEye), 0.0f, 1e-4f);
    EXPECT_NEAR(glm::dot(glm::normalize(viewForward), glm::normalize(worldForward)), 1.0f, 1e-4f);

    framework.shutdown();
}

// A cloned scene skips generated entities (companions are not authored
// content), so the body has to be rebuilt for the camera in the copy rather
// than carried over. This is the clone half of the companion boundary.
TEST(LinkageFrameworkTest, ClonedCameraRebuildsItsOwnBodyCompanion)
{
    SceneManager sceneManager;
    SceneLifecycleHostScope lifecycleHost(&sceneManager);
    FrameTaskCapture sink;
    LinkageFramework framework;
    framework.setSceneManager(&sceneManager);
    framework.setFrameTaskSink(std::ref(sink));
    addCompanionRule(framework);
    framework.init();

    stdptr<Scene> scene = std::make_shared<Scene>("CloneSource");
    auto*         node  = scene->createNode3D("Cam");
    node->getEntity()->addComponent<CameraComponent>();
    ASSERT_TRUE(sceneManager.activateScene(scene));
    sink.drain();
    ASSERT_NE(CompanionManager::findCompanion(*scene, *node->getEntity()), nullptr);

    stdptr<Scene> clone = scene->clone();
    ASSERT_NE(clone, nullptr);

    Entity* clonedCamera = nullptr;
    for (Node* child : clone->getRootNode()->getChildren()) {
        Entity* candidate = child ? child->getEntity() : nullptr;
        if (candidate && candidate->isValid() && candidate->hasComponent<CameraComponent>()) {
            clonedCamera = candidate;
            break;
        }
    }
    ASSERT_NE(clonedCamera, nullptr);

    // No companion travelled with the copy: it is derived state.
    EXPECT_FALSE(clonedCamera->hasComponent<StaticMeshComponent>());

    ASSERT_TRUE(sceneManager.activateScene(clone));
    sink.drain();

    Entity* clonedBody = CompanionManager::findCompanion(*clone, *clonedCamera);
    ASSERT_NE(clonedBody, nullptr);
    EXPECT_EQ(CompanionManager::hostOf(*clonedBody), clonedCamera);
    ASSERT_TRUE(clonedBody->hasComponent<StaticMeshComponent>());
    EXPECT_EQ(clonedBody->getComponent<StaticMeshComponent>()->_mesh._sourceModelPath,
              CameraCompanionPolicy{}.meshPath);

    framework.shutdown();
}

// Scene files written before icons moved onto companions carry a
// BillboardComponent on the light entity. The declaration adopts that legacy
// placement: the stale host copy goes away so it cannot keep drawing as
// content, and the icon lives only on the companion.
TEST(LinkageFrameworkTest, LightCompanionAdoptsLegacyHostBillboard)
{
    SceneManager sceneManager;
    SceneLifecycleHostScope lifecycleHost(&sceneManager);
    FrameTaskCapture sink;
    LinkageFramework framework;
    framework.setSceneManager(&sceneManager);
    framework.setFrameTaskSink(std::ref(sink));
    addCompanionRule(framework);
    framework.init();

    stdptr<Scene> scene = std::make_shared<Scene>("LegacyBillboardScene");
    auto*         node  = scene->createNode3D("Light");
    node->getEntity()->addComponent<PointLightComponent>();
    ASSERT_NE(node->getEntity()->addComponent<BillboardComponent>(), nullptr);
    ASSERT_TRUE(sceneManager.activateScene(scene));
    sink.drain();

    Entity* light = node->getEntity();
    EXPECT_FALSE(light->hasComponent<BillboardComponent>());

    Entity* icon = CompanionManager::findCompanion(*scene, *light);
    ASSERT_NE(icon, nullptr);
    ASSERT_TRUE(icon->hasComponent<BillboardComponent>());
    EXPECT_EQ(icon->getComponent<BillboardComponent>()->features, toMask(ERenderFeature::Gizmo));

    framework.shutdown();
}

// Deferred tasks scheduled before shutdown must no-op once the framework is
// gone, even if they are still queued on the host frame-task sink.
TEST(LinkageFrameworkTest, DeferredTasksCancelledAfterShutdown)
{
    FrameTaskCapture sink;
    LinkageFramework framework;
    framework.setFrameTaskSink(std::ref(sink));

    int ran = 0;
    framework.scheduleDeferred(nullptr, [&ran]() { ++ran; });
    ASSERT_EQ(sink.size(), 1u);

    framework.shutdown();
    sink.drain();
    ASSERT_EQ(ran, 0);
}

// Deferred tasks for a scene that has been destroyed must be skipped by the
// scene-validity guard.
TEST(LinkageFrameworkTest, DeferredTaskSkippedForDestroyedScene)
{
    SceneManager sceneManager;
    SceneLifecycleHostScope lifecycleHost(&sceneManager);
    FrameTaskCapture sink;
    LinkageFramework framework;
    framework.setSceneManager(&sceneManager);
    framework.setFrameTaskSink(std::ref(sink));

    stdptr<Scene> scene = std::make_shared<Scene>("LinkageScene");
    ASSERT_TRUE(sceneManager.activateScene(scene));

    int ran = 0;
    framework.scheduleDeferred(scene.get(), [&ran]() { ++ran; });
    sceneManager.destroyScene(scene);

    sink.drain();
    ASSERT_EQ(ran, 0);

    framework.shutdown();
}

// A generated companion is a child node, so its world matrix is derived from
// the host's. That only holds while every writer of the host transform also
// invalidates the child: the inspector, undo/redo and scene loads write the
// reflected fields directly instead of going through the setters, so the dirty
// propagation has to hold at the point where world matrices are computed.
// Without it the body keeps its old world matrix while the frustum wireframe --
// which reads the authored transform -- moves, i.e. "the model and the
// wireframe do not line up".
TEST(LinkageFrameworkTest, CameraBodyFollowsItsHostAfterTheCompanionExists)
{
    SceneManager sceneManager;
    SceneLifecycleHostScope lifecycleHost(&sceneManager);
    FrameTaskCapture sink;
    LinkageFramework framework;
    framework.setSceneManager(&sceneManager);
    framework.setFrameTaskSink(std::ref(sink));
    addCompanionRule(framework);
    framework.init();

    stdptr<Scene> scene = std::make_shared<Scene>("CompanionMoveScene");
    auto*         node  = scene->createNode3D("Cam");
    ASSERT_NE(node, nullptr);
    auto* hostTc = node->getEntity()->getComponent<TransformComponent>();
    ASSERT_NE(hostTc, nullptr);
    node->getEntity()->addComponent<CameraComponent>();
    ASSERT_TRUE(sceneManager.activateScene(scene));
    sink.drain();

    Entity* body = CompanionManager::findCompanion(*scene, *node->getEntity());
    ASSERT_NE(body, nullptr);
    auto* bodyTc = body->getComponent<TransformComponent>();
    ASSERT_NE(bodyTc, nullptr);

    // TransformSystem is the only thing that updates world matrices, so run it
    // the way the app does (one system over the scene tree).
    TransformSystem transforms;
    transforms.setSceneProvider([&scene]() -> Scene* { return scene.get(); });
    transforms.init();

    const auto worldOf = [](TransformComponent* tc) {
        TransformSystem::computeWorldMatrix(tc);
        return tc->getWorldPosition();
    };

    // 1. Setter path (gizmo drag, editor camera write).
    hostTc->setPosition({3.0f, 4.0f, 5.0f});
    transforms.onUpdate(0.0f);
    EXPECT_NEAR(worldOf(bodyTc).x, 3.0f, 1e-4f);
    EXPECT_NEAR(worldOf(bodyTc).y, 4.0f, 1e-4f);
    EXPECT_NEAR(worldOf(bodyTc).z, 5.0f, 1e-4f);

    // 2. Reflected write (inspector / undo / script / scene load): the fields
    //    are written directly and only the component's own flags are marked.
    hostTc->_position = {7.0f, 8.0f, 9.0f};
    hostTc->onPostSerialize();
    transforms.onUpdate(0.0f);
    EXPECT_NEAR(worldOf(bodyTc).x, 7.0f, 1e-4f);
    EXPECT_NEAR(worldOf(bodyTc).y, 8.0f, 1e-4f);
    EXPECT_NEAR(worldOf(bodyTc).z, 9.0f, 1e-4f);

    // 3. Nested: a descendant is derived through the body as well.
    auto* nestedNode = scene->createNode3D("Nested", scene->getNodeByEntity(body));
    ASSERT_NE(nestedNode, nullptr);
    sink.drain();
    auto* nestedTc = nestedNode->getEntity()->getComponent<TransformComponent>();
    ASSERT_NE(nestedTc, nullptr);
    nestedTc->setPosition({1.0f, 0.0f, 0.0f});
    hostTc->setPosition({-2.0f, 0.0f, 0.0f});
    transforms.onUpdate(0.0f);
    EXPECT_NEAR(worldOf(nestedTc).x, -1.0f, 1e-4f);

    // 4. The scene root never gets a dirty callback (it is created directly and
    //    never reparented), so a descendant of any node must be derived from a
    //    recomputed parent matrix rather than from a notification the parent
    //    happened to send.
    Node*  rootNode = scene->getRootNode();
    ASSERT_NE(rootNode, nullptr);
    auto*  rootTc   = rootNode->getEntity()->getComponent<TransformComponent>();
    ASSERT_NE(rootTc, nullptr);
    rootTc->_position = {10.0f, 0.0f, 0.0f};
    rootTc->onPostSerialize();
    transforms.onUpdate(0.0f);
    EXPECT_NEAR(worldOf(nestedTc).x, 9.0f, 1e-4f);

    transforms.shutdown();
    framework.shutdown();
}

// activate A, activate B, drop the last ref to A. A received onSceneInit, so
// its destructor must announce onSceneDestroy before the registry dies.
// Otherwise MaterialRenderLinkageRule keeps the freed registry and crashes
// in its own destructor.
TEST(LinkageFrameworkTest, ReplacedSceneUnloadOnLastRefDrop)
{
    SceneManager sceneManager;
    SceneLifecycleHostScope lifecycleHost(&sceneManager);
    FrameTaskCapture sink;
    LinkageFramework framework;
    framework.setSceneManager(&sceneManager);
    framework.setFrameTaskSink(std::ref(sink));
    framework.addRule(std::make_shared<MaterialRenderLinkageRule>(&framework));
    framework.init();

    auto  sceneA = std::make_shared<Scene>("SceneA");
    auto* node   = sceneA->createNode3D("Body");
    node->getEntity()->addComponent<PBRMaterialComponent>();
    ASSERT_TRUE(sceneManager.activateScene(sceneA));
    sink.drain();
    ASSERT_FALSE(sceneA->getRegistry().on_construct<PBRMaterialComponent>().empty());

    auto sceneB = std::make_shared<Scene>("SceneB");
    ASSERT_TRUE(sceneManager.activateScene(sceneB));
    EXPECT_EQ(sceneManager.getActiveScene(), sceneB.get());
    EXPECT_EQ(sceneManager.getSceneByRegistry(&sceneA->getRegistry()), sceneA.get());

    bool bUnloaded = false;
    int  unloadCount = 0;
    int  observer = 0;
    // shared_ptr::reset nulls its pointer before the deleter runs, so the
    // callback must compare against the raw pointer captured beforehand.
    Scene* rawA = sceneA.get();
    sceneManager.onSceneDestroy.addLambda(&observer, [&](Scene* dying) {
        if (dying != rawA) {
            return;
        }
        ++unloadCount;
        bUnloaded = dying->getRegistry().on_construct<PBRMaterialComponent>().empty();
    });

    sceneA.reset();
    EXPECT_TRUE(bUnloaded);
    EXPECT_EQ(unloadCount, 1);
    EXPECT_EQ(sceneManager.getActiveScene(), sceneB.get());
    EXPECT_EQ(sceneManager.getSceneByRegistry(&sceneB->getRegistry()), sceneB.get());

    framework.shutdown();
}

// The manager dies while the scene is still owned elsewhere. Destroy must be
// announced from the manager (the scene's later destructor sees a null host)
// so the rule drops the registry before anyone frees it.
TEST(LinkageFrameworkTest, SceneManagerDestroyedBeforeSceneDisconnectsRules)
{
    auto scene = std::make_shared<Scene>("OutlivesManager");
    bool bUnloaded = false;
    int  observer = 0;
    {
        LinkageFramework framework;
        {
            SceneManager sceneManager;
            SceneLifecycleHostScope lifecycleHost(&sceneManager);
            framework.setSceneManager(&sceneManager);
            framework.addRule(std::make_shared<MaterialRenderLinkageRule>(&framework));
            framework.init();

            auto* node = scene->createNode3D("Body");
            node->getEntity()->addComponent<PBRMaterialComponent>();
            ASSERT_TRUE(sceneManager.activateScene(scene));

            sceneManager.onSceneDestroy.addLambda(&observer, [&](Scene* dying) {
                if (dying != scene.get()) {
                    return;
                }
                bUnloaded = dying->getRegistry().on_construct<PBRMaterialComponent>().empty();
            });
        }
        EXPECT_TRUE(bUnloaded);
        EXPECT_EQ(Scene::getLifecycleHost(), nullptr);
        // Registry goes away while the rule still exists. The manager's
        // destroy broadcast already disconnected it, so dropping the scene
        // cannot call into the rule.
        scene.reset();
        // The manager is gone. Drop that pointer before shutdown, which still
        // has to unsubscribe the process-wide component-removed bus.
        framework.setSceneManager(nullptr);
        framework.shutdown();
    }
}

} // namespace ya
