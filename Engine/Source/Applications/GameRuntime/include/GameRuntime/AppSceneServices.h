#pragma once

#include "Core/Api.h"

#include <optional>
#include <string>

namespace ya
{

struct App;
struct Scene;
struct SceneManager;
struct Entity;

/// Resolves a scene path the way every scene entry point reads it: absolute or
/// cwd-relative as authored, else against the project root.
YA_GAME_RUNTIME_API std::string resolveProjectScenePath(const App& app, const std::string& requestedPath);

class YA_GAME_RUNTIME_API AppSceneServices
{
  private:
    App* _app = nullptr;
    /// One deferred transfer (`world.loadScene`); a frame's last request wins.
    struct FPendingTransfer
    {
        std::string path;
        std::string spawnName;
    };
    std::optional<FPendingTransfer> _pendingTransfer;

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

    /// Open a scene document and leave play. Stays stopped; this is the editor
    /// open-scene path. Automation `scene.load` keeps the caller's run mode.
    bool loadScene(const std::string& path);
    bool unloadScene();
    bool saveScene(const std::string& path);

    void refreshSceneDerivedState(Scene* scene);
    void refreshActiveSceneDerivedState();

    Entity* getPrimaryCamera() const;

    // === Scene transfer during play (rpg R3) ===
    /// Queue a gameplay scene switch (`world.loadScene(path, spawnName)`).
    /// Executed at the frame-end structural flush, never inline: a switch
    /// stops every script, which must not happen while one is running.
    void requestSceneTransfer(const std::string& path, const std::string& spawnName);
    /// Run the queued transfer, if any. Keeps the play session: app state, the
    /// UI host and the persistent script state are untouched; the old scene's
    /// scripts stop, the scene swaps, the spawn places the player.
    void runPendingSceneTransfer();
};

} // namespace ya
