#pragma once

#include "Foundation/Core/Api.h"

#include <string>

namespace ya
{

struct App;
struct Scene;
struct SceneManager;
struct Entity;

class YA_HOST_API AppSceneServices
{
  private:
    App* _app = nullptr;

  public:
    AppSceneServices() = default;
    explicit AppSceneServices(App* app)
        : _app(app)
    {
    }

    void bind(App& app) { _app = &app; }

    [[nodiscard]] SceneManager* getSceneManager() const;
    [[nodiscard]] Scene*        getActiveScene() const;
    [[nodiscard]] bool          hasScene() const;

    bool loadScene(const std::string& path);
    bool unloadScene();
    bool saveScene(const std::string& path);

    void refreshSceneDerivedState(Scene* scene);
    void refreshActiveSceneDerivedState();

    Entity* getPrimaryCamera() const;
};

} // namespace ya
