// SceneManager destroy-broadcast lifecycle regression guards. The PIE stop
// crash (runtime stop from the editor) came from a listener dropping the
// scene's last external reference WHILE onSceneDestroy was still being
// delivered: the Scene was destroyed mid-broadcast and later listeners
// (linkage rules disconnecting entt signals) dereferenced a freed
// Scene/registry. SceneManager must keep the Scene alive for the whole
// broadcast.

#include "Scene/Runtime/SceneManager.h"

#include "Scene/Core/Scene.h"

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

} // namespace ya
