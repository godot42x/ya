#pragma once

#include "Core/Common/Types.h"

namespace ya
{

struct App;
struct Scene;
enum class AppState;

class EditorPlaySession
{
    stdptr<Scene> _authoringScene;
    stdptr<Scene> _playScene;

  public:
    [[nodiscard]] bool begin(App& app, AppState nextState);
    void end(App& app);
    void shutdown(App& app);
    void onSceneActivated(App& app, Scene* scene);
    void onSceneDestroyed(Scene* scene);

    [[nodiscard]] Scene* getAuthoringScene() const { return _authoringScene.get(); }
};

} // namespace ya
