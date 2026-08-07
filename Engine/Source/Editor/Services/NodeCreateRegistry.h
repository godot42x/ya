#pragma once

#include <functional>
#include <string>
#include <vector>

namespace ya
{

struct Node;
struct Scene;

namespace editor
{

/// A named node-creation capability. 2D entries are generated from the
/// reflection registry (every Node2D subclass); 3D presets (primitive +
/// component combinations) are registered once here instead of being
/// hardcoded in the hierarchy panel.
struct NodeCreateEntry
{
    std::string category; // "3D Object" / "Light" / "2D"
    std::string displayName;
    std::string doc;
    /// Creates the node (and any entity/components) under `parent`. Returns
    /// the created node or nullptr on failure.
    std::function<Node*(Scene& scene, const std::string& name, Node* parent)> factory;
};

/// Central create-menu source of truth. The hierarchy panel and the CLI
/// (`scene.create_preset` / `node.create`) both query this registry, so a new
/// entry appears in the editor UI and over RPC at the same time.
class NodeCreateRegistry
{
  public:
    static NodeCreateRegistry& get();

    /// Register a hand-written 3D preset (component combinations cannot be
    /// derived from reflection alone).
    void registerPreset(std::string category,
                        std::string displayName,
                        std::string doc,
                        std::function<Node*(Scene&, const std::string&, Node*)> factory);

    /// Registered 3D presets, grouped by category (stable registration order).
    [[nodiscard]] const std::vector<NodeCreateEntry>& presets() const { return _presets; }

    /// Node2D entries auto-collected from the reflection registry.
    [[nodiscard]] std::vector<NodeCreateEntry> uiEntries() const;

    /// Create a registered preset by display name.
    Node* createPreset(const std::string& displayName, Scene& scene, const std::string& name, Node* parent) const;

  private:
    std::vector<NodeCreateEntry> _presets;
};

} // namespace editor

} // namespace ya
