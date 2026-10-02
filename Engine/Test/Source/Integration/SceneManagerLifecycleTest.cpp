// SceneManager destroy-broadcast lifecycle regression guards. The PIE stop
// crash (runtime stop from the editor) came from a listener dropping the
// scene's last external reference WHILE onSceneDestroy was still being
// delivered: the Scene was destroyed mid-broadcast and later listeners
// (linkage rules disconnecting entt signals) dereferenced a freed
// Scene/registry. SceneManager must keep the Scene alive for the whole
// broadcast.

#include "Scene/Runtime/SceneManager.h"

#include "AppModuleTestAccess.h"
#include "GameEditor/EditorPlaySession.h"
#include "GameRuntime/App.h"
#include "Scene/Core/Scene.h"

#include "Core/System/VirtualFileSystem.h"

#include <gtest/gtest.h>

namespace ya
{

TEST(SceneManagerLifecycleTest, DestroyBroadcastKeepsSceneAliveForLaterListeners)
{
    SceneManager manager;

    // The scene's ONLY external reference is this local (the PIE play scene
    // is no longer the active scene when it is destroyed). A custom deleter
    // observes exactly when the Scene object is destroyed.
    bool bSceneDestroyed = false;
    stdptr<Scene> scene{new Scene("PlayScene"),
                        [&](Scene* p) {
                            bSceneDestroyed = true;
                            delete p;
                        }};

    bool  bFirstRan  = false;
    bool  bLaterRan  = false;
    Scene* seenByLater = nullptr;
    std::string nameByLater;

    // Registered first (like AppLifecycle): drops the last external reference
    // while the broadcast is still running (like EditorPlaySession clearing
    // its play scene from onSceneDestroyed).
    manager.onSceneDestroy.addLambda(&manager, [&](Scene*) {
        bFirstRan = true;
        scene.reset();
    });

    // Registered later (like the linkage framework): must still receive a
    // LIVE scene; reading its name proves the object was not freed.
    manager.onSceneDestroy.addLambda(&manager, [&](Scene* s) {
        bLaterRan   = true;
        EXPECT_FALSE(bSceneDestroyed) << "Scene destroyed mid-broadcast; later listeners got a dangling pointer";
        if (bSceneDestroyed) {
            return; // touching s would be use-after-free
        }
        seenByLater = s;
        nameByLater = s->getName();
    });

    manager.destroyScene(scene);

    EXPECT_TRUE(bFirstRan);
    EXPECT_TRUE(bLaterRan);
    ASSERT_NE(seenByLater, nullptr);
    EXPECT_EQ(nameByLater, "PlayScene");
    // The caller's reference was dropped mid-broadcast; the manager's
    // keep-alive released it only after every listener ran.
    EXPECT_EQ(scene, nullptr);
    EXPECT_TRUE(bSceneDestroyed);
}

TEST(SceneManagerLifecycleTest, DestroyBroadcastReachesListenersOnActiveSceneUnload)
{
    SceneManager manager;
    auto scene = std::make_shared<Scene>("ActiveScene");
    manager.activateScene(scene);

    bool bDestroyNotified = false;
    manager.onSceneDestroy.addLambda(&manager, [&](Scene* s) {
        bDestroyNotified = (s != nullptr && s->getName() == "ActiveScene");
    });

    EXPECT_TRUE(manager.unloadScene());
    EXPECT_TRUE(bDestroyNotified);
    EXPECT_FALSE(manager.hasScene());
}

namespace
{

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

TEST(SceneManagerLifecycleTest, DroppingReplacedSceneAnnouncesDestroyOnce)
{
    SceneManager manager;
    SceneLifecycleHostScope host(&manager);

    auto sceneA = std::make_shared<Scene>("SceneA");
    auto sceneB = std::make_shared<Scene>("SceneB");
    ASSERT_TRUE(manager.activateScene(sceneA));
    ASSERT_TRUE(manager.activateScene(sceneB));

    int destroyCount = 0;
    int observer = 0;
    manager.onSceneDestroy.addLambda(&observer, [&](Scene* scene) {
        if (scene && scene->getName() == "SceneA") {
            ++destroyCount;
            EXPECT_EQ(scene->getName(), "SceneA");
        }
    });

    sceneA.reset();
    EXPECT_EQ(destroyCount, 1);
    EXPECT_EQ(manager.getActiveScene(), sceneB.get());
    EXPECT_EQ(manager.getSceneByRegistry(&sceneB->getRegistry()), sceneB.get());
}

TEST(SceneManagerLifecycleTest, ExplicitDestroyDoesNotBroadcastAgainFromDestructor)
{
    SceneManager manager;
    SceneLifecycleHostScope host(&manager);

    auto scene = std::make_shared<Scene>("Once");
    ASSERT_TRUE(manager.activateScene(scene));

    int destroyCount = 0;
    int observer = 0;
    manager.onSceneDestroy.addLambda(&observer, [&](Scene*) { ++destroyCount; });

    EXPECT_TRUE(manager.destroyScene(scene));
    EXPECT_EQ(scene, nullptr);
    EXPECT_EQ(destroyCount, 1);
    EXPECT_FALSE(manager.hasScene());
}

TEST(SceneManagerLifecycleTest, ManagerDestroyedBeforeSceneAnnouncesDestroyOnce)
{
    auto scene = std::make_shared<Scene>("Outlives");
    int  destroyCount = 0;
    int  observer = 0;
    {
        SceneManager manager;
        SceneLifecycleHostScope host(&manager);
        ASSERT_TRUE(manager.activateScene(scene));
        manager.onSceneDestroy.addLambda(&observer, [&](Scene* dying) {
            if (dying == scene.get()) {
                ++destroyCount;
                EXPECT_EQ(dying->getName(), "Outlives");
            }
        });
    }

    EXPECT_EQ(Scene::getLifecycleHost(), nullptr);
    EXPECT_EQ(destroyCount, 1);
    scene.reset();
    EXPECT_EQ(destroyCount, 1);
}

TEST(SceneManagerLifecycleTest, PlayTransferStopRestoresAuthoringScene)
{
    VirtualFileSystem::init();

    App app;
    // The scene callbacks close over the session. The manager's destructor
    // broadcasts onSceneDestroy for scenes still initialized, so the session
    // has to outlive the manager.
    EditorPlaySession session;
    int observer = 0;
    int transferredDestroyCount = 0;
    uint64_t transferredId = 0;
    auto sceneManager = std::make_unique<SceneManager>();
    AppModuleTestAccess::setSceneManager(app, sceneManager.get());
    SceneLifecycleHostScope host(sceneManager.get());
    AppModuleTestAccess::setAppState(app, AppState::Stopped);

    auto authoring = makeShared<Scene>("Authoring");
    ASSERT_TRUE(sceneManager->activateScene(authoring));
    sceneManager->onSceneDestroy.addLambda(&observer, [&](Scene* scene) {
        session.onSceneDestroyed(scene);
        if (scene && scene->getName() == "Transferred") {
            ++transferredDestroyCount;
            transferredId = scene->getInstanceId();
        }
    });
    sceneManager->onSceneActivated.addLambda(&observer, [&](Scene* scene) {
        session.onSceneActivated(app, scene);
    });

    ASSERT_TRUE(session.begin(app, AppState::Runtime));
    AppModuleTestAccess::setAppState(app, AppState::Runtime);
    EXPECT_NE(sceneManager->getActiveScene(), authoring.get());

    // Door transfer: unload the play scene, activate the destination. The
    // session must adopt the destination so stop can destroy it.
    ASSERT_TRUE(sceneManager->unloadScene());
    ASSERT_TRUE(sceneManager->activateScene(makeShared<Scene>("Transferred")));
    ASSERT_NE(sceneManager->getActiveScene(), nullptr);
    EXPECT_EQ(sceneManager->getActiveScene()->getName(), "Transferred");
    EXPECT_EQ(session.getAuthoringScene(), authoring.get());

    // end() runs from onBeforeAppStateChange, while play is still the state.
    session.end(app);

    EXPECT_EQ(sceneManager->getActiveScene(), authoring.get());
    EXPECT_EQ(transferredDestroyCount, 1);
    EXPECT_NE(transferredId, 0u);
    EXPECT_EQ(Scene::findByInstanceId(transferredId), nullptr);
    EXPECT_EQ(sceneManager->getSceneByRegistry(&authoring->getRegistry()), authoring.get());

    AppModuleTestAccess::setSceneManager(app, nullptr);
}

} // namespace ya
