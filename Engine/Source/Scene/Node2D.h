#pragma once

#include "Core/Common/AssetRef.h"
#include "Core/Reflection/Reflection.h"
#include "Scene/Node.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ya
{

// ============================================================================
// Node2D - screen-space 2D/UI node in the unified scene tree
// ============================================================================
//
// Design philosophy (Godot-style):
// - Node2D lives in the SAME scene tree as Node3D (Scene::_rootNode).
// - It is a pure tree node: NO ECS entity, NO components. Ownership lives in
//   Scene::_entityLessNodes.
// - Transform semantics: 2D position accumulates along ancestor Node2D chains
//   only; Node3D ancestors contribute no 2D transform (Godot Control semantics).
// ============================================================================

enum class EUIAlignH : uint8_t
{
    Left,
    Center,
    Right,
};

enum class EUIAlignV : uint8_t
{
    Top,
    Center,
    Bottom,
};

struct ENGINE_API Node2D : public Node
{
    YA_REFLECT_BEGIN(Node2D, Node)
    YA_REFLECT_FIELD(_position)
    YA_REFLECT_FIELD(_size)
    YA_REFLECT_FIELD(_visible)
    YA_REFLECT_FIELD(_zOrder)
    YA_REFLECT_END()

    glm::vec2 _position = {0.0f, 0.0f}; // Relative to the ancestor Node2D chain
    glm::vec2 _size     = {100.0f, 50.0f};
    bool      _visible  = true;
    int       _zOrder   = 0;

    explicit Node2D(std::string name = "Node2D") : Node(std::move(name), nullptr) {}
    ~Node2D() override = default;

    // === Type identity (used by serialization / factory) ===
    [[nodiscard]] virtual type_index_t getTypeIndex() const { return ya::type_index_v<Node2D>; }
    [[nodiscard]] virtual const char*  getUITypeName() const { return "Node2D"; }

    // === Screen-space helpers (top-left origin, Y down) ===
    /// Accumulated position along the ancestor Node2D chain.
    [[nodiscard]] glm::vec2 getScreenPosition() const;
    /// Own-rect hit test in screen space; children are handled by the walker.
    [[nodiscard]] bool hitTest(const glm::vec2& screenPoint) const;

    // === Reflection field serialization ===
    [[nodiscard]] nlohmann::json serializeFields() const;
    void                        deserializeFields(const nlohmann::json& fields);
};

/// Canvas root: fills the viewport. Children are laid out in its space.
struct ENGINE_API UICanvasNode : public Node2D
{
    YA_REFLECT_BEGIN(UICanvasNode, Node2D)
    YA_REFLECT_END()

    explicit UICanvasNode(std::string name = "Canvas") : Node2D(std::move(name)) {}

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UICanvasNode>; }
    [[nodiscard]] const char*  getUITypeName() const override { return "UICanvasNode"; }
};

/// Flat panel: solid color and/or image, optional 9-slice border.
struct ENGINE_API UIPanelNode : public Node2D
{
    YA_REFLECT_BEGIN(UIPanelNode, Node2D)
    YA_REFLECT_FIELD(_color)
    YA_REFLECT_FIELD(_image)
    YA_REFLECT_FIELD(_bNineSlice)
    YA_REFLECT_FIELD(_nineSliceBorder)
    YA_REFLECT_END()

    glm::vec4 _color           = {0.2f, 0.2f, 0.2f, 0.8f};
    TextureRef _image;
    bool       _bNineSlice     = false;
    glm::vec4  _nineSliceBorder = {8.0f, 8.0f, 8.0f, 8.0f}; // l, t, r, b in pixels

    explicit UIPanelNode(std::string name = "Panel") : Node2D(std::move(name)) {}

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIPanelNode>; }
    [[nodiscard]] const char*  getUITypeName() const override { return "UIPanelNode"; }
};

/// Text element rendered through the font atlas.
struct ENGINE_API UITextNode : public Node2D
{
    YA_REFLECT_BEGIN(UITextNode, Node2D)
    YA_REFLECT_FIELD(_text)
    YA_REFLECT_FIELD(_fontSize)
    YA_REFLECT_FIELD(_color)
    YA_REFLECT_FIELD(_hAlign)
    YA_REFLECT_FIELD(_vAlign)
    YA_REFLECT_END()

    std::string   _text     = "Text";
    uint32_t      _fontSize = 16;
    glm::vec4     _color    = {1.0f, 1.0f, 1.0f, 1.0f};
    EUIAlignH     _hAlign   = EUIAlignH::Left;
    EUIAlignV     _vAlign   = EUIAlignV::Top;

    explicit UITextNode(std::string name = "Text") : Node2D(std::move(name)) {}

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UITextNode>; }
    [[nodiscard]] const char*  getUITypeName() const override { return "UITextNode"; }
};

/// Button: panel style with hover/pressed states. Click callback is runtime-only
/// (not serialized); hit testing is driven by the UI walker.
struct ENGINE_API UIButtonNode : public Node2D
{
    YA_REFLECT_BEGIN(UIButtonNode, Node2D)
    YA_REFLECT_FIELD(_normalColor)
    YA_REFLECT_FIELD(_hoveredColor)
    YA_REFLECT_FIELD(_pressedColor)
    YA_REFLECT_END()

    glm::vec4 _normalColor  = {0.8f, 0.8f, 0.8f, 1.0f};
    glm::vec4 _hoveredColor = {0.6f, 0.6f, 0.6f, 1.0f};
    glm::vec4 _pressedColor = {0.4f, 0.4f, 0.4f, 1.0f};

    // Runtime-only state (not serialized)
    bool                   _bHovered = false;
    bool                   _bPressed = false;
    std::function<void()>  _onClick;

    explicit UIButtonNode(std::string name = "Button") : Node2D(std::move(name)) {}

    [[nodiscard]] type_index_t getTypeIndex() const override { return ya::type_index_v<UIButtonNode>; }
    [[nodiscard]] const char*  getUITypeName() const override { return "UIButtonNode"; }
};

/// Create a Node2D subclass by its UI type name (used by scene deserialization
/// and PIE clone). Returns nullptr for unknown types.
std::shared_ptr<Node2D> createNode2DByTypeName(const std::string& typeName, const std::string& name);

/// Every registered Node2D subclass, as short type names, sorted. Driven by
/// ClassRegistry so new node types appear without touching this file.
std::vector<std::string> getRegisteredUINodeTypeNames();

} // namespace ya
