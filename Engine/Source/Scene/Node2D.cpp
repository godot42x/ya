#include "Scene/Node2D.h"

#include "Core/Log.h"
#include "Core/Reflection/ReflectionSerializer.h"
#include "Render/2D/Render2D.h"
#include "Resource/Font/FontManager.h"
#include "reflects-core/lib.h"

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <vector>

namespace ya
{

namespace
{

std::string shortTypeName(std::string_view typeName)
{
    const size_t pos = typeName.find_last_of(':');
    return pos == std::string_view::npos ? std::string(typeName) : std::string(typeName.substr(pos + 1));
}

using Node2DFactory = std::function<std::shared_ptr<Node2D>(const std::string& name)>;

/// Reflection-driven Node2D factory map: every registered subclass of Node2D
/// discovered through ClassRegistry::parentToChildren automatically becomes a
/// creatable UI node type. No hardcoded if-chain per node type.
const std::unordered_map<std::string, Node2DFactory>& getNode2DFactories()
{
    static const std::unordered_map<std::string, Node2DFactory> factories = [] {
        std::unordered_map<std::string, Node2DFactory> map;
        auto& registry = ClassRegistry::instance();

        std::vector<type_index_t> pending{ya::type_index_v<Node2D>};
        while (!pending.empty()) {
            const type_index_t typeId = pending.back();
            pending.pop_back();

            const auto childrenIt = registry.parentToChildren.find(typeId);
            if (childrenIt == registry.parentToChildren.end()) {
                continue;
            }
            for (const type_index_t childTypeId : childrenIt->second) {
                if (Class* cls = registry.getClass(childTypeId)) {
                    const std::string shortName = shortTypeName(cls->name);
                    if (!shortName.empty() && !map.contains(shortName)) {
                        map.emplace(shortName, [cls](const std::string& name) -> std::shared_ptr<Node2D> {
                            void* raw = cls->createInstance(); // default ctor registered via reflection
                            auto* node = static_cast<Node2D*>(raw);
                            node->setName(name);
                            return std::shared_ptr<Node2D>(node, [cls](Node2D* ptr) { cls->destroyInstance(ptr); });
                        });
                    }
                }
                pending.push_back(childTypeId);
            }
        }
        return map;
    }();
    return factories;
}

/// Map a canvas-logical point to render-target pixels (top-left origin, Y down).
glm::vec2 toScreenPxPos(const UIPaintContext& ctx, const glm::vec2& point)
{
    return (point * ctx.canvas.zoom + ctx.canvas.pan) * ctx.uiScale;
}

/// Map a canvas-logical extent to render-target pixels.
glm::vec2 toScreenPxSize(const UIPaintContext& ctx, const glm::vec2& extent)
{
    return extent * ctx.canvas.zoom * ctx.uiScale;
}

} // namespace

glm::vec2 Node2D::getScreenPosition() const
{
    glm::vec2 pos = _position;
    for (const Node* parent = getParent(); parent != nullptr; parent = parent->getParent()) {
        if (parent->is2D()) {
            pos += static_cast<const Node2D*>(parent)->_position;
        }
    }
    return pos;
}

bool Node2D::hitTest(const glm::vec2& screenPoint) const
{
    if (!isHitTestableSelf()) {
        return false;
    }
    const glm::vec2 pos = getScreenPosition();
    return screenPoint.x >= pos.x && screenPoint.x <= pos.x + _size.x &&
           screenPoint.y >= pos.y && screenPoint.y <= pos.y + _size.y;
}

bool Node2D::hitTestLayoutRect(const glm::vec2& canvasPoint) const
{
    return canvasPoint.x >= _layoutRect.pos.x && canvasPoint.x <= _layoutRect.pos.x + _layoutRect.extent.x &&
           canvasPoint.y >= _layoutRect.pos.y && canvasPoint.y <= _layoutRect.pos.y + _layoutRect.extent.y;
}

bool Node2D::isVisibleInTree() const
{
    for (const Node* node = this; node != nullptr; node = node->getParent()) {
        if (node->is2D() && !static_cast<const Node2D*>(node)->isVisibleForRender()) {
            return false;
        }
    }
    return true;
}

bool Node2D::isHitTestableInTree() const
{
    if (!isHitTestableSubtree()) {
        return false;
    }
    for (const Node* node = getParent(); node != nullptr; node = node->getParent()) {
        if (node->is2D()) {
            const auto* node2D = static_cast<const Node2D*>(node);
            // Hidden / Collapsed cull rendering and hits; SelfHitTestInvisible
            // culls hits only. HitTestInvisible ancestors do not block children.
            if (!node2D->isVisibleForRender() || !node2D->isHitTestableSubtree()) {
                return false;
            }
        }
    }
    return true;
}

// === Layout ===

Rect2D Node2D::computeAnchorRect(const Rect2D& parentRect) const
{
    const glm::vec2 anchorMin = glm::clamp(_anchorMin, 0.0f, 1.0f);
    const glm::vec2 anchorMax = glm::clamp(_anchorMax, 0.0f, 1.0f);
    const glm::vec2 rectMin   = parentRect.pos + parentRect.extent * anchorMin + _position;

    // Per-axis: an axis with an anchor span stretches to the parent; otherwise
    // the axis keeps _size (default {0,0} anchors = legacy absolute layout).
    const glm::vec2 span = (anchorMax - anchorMin) * parentRect.extent;
    glm::vec2       size = _size;
    if (span.x != 0.0f) {
        size.x = span.x;
    }
    if (span.y != 0.0f) {
        size.y = span.y;
    }
    return Rect2D{.pos = rectMin, .extent = size};
}

void Node2D::layout(const Rect2D& parentRect)
{
    _layoutRect = computeAnchorRect(parentRect);
    layoutChildren(_layoutRect);
}

void Node2D::layoutAssigned(const Rect2D& rect)
{
    _layoutRect = rect;
    layoutChildren(_layoutRect);
}

void Node2D::layoutChildren(const Rect2D& layoutRect)
{
    for (Node* child : getChildrenInPaintOrder()) {
        if (child->is2D()) {
            auto* child2D = static_cast<Node2D*>(child);
            if (child2D->participatesInLayout()) {
                child2D->layout(layoutRect);
            }
        }
        else {
            layoutTransparent(child, layoutRect);
        }
    }
}

void Node2D::layoutTransparent(Node* node, const Rect2D& anchorRect)
{
    for (Node* child : node->getChildren()) {
        if (child->is2D()) {
            static_cast<Node2D*>(child)->layout(anchorRect);
        }
        else {
            layoutTransparent(child, anchorRect);
        }
    }
}

glm::vec2 Node2D::computeDesiredSize() const
{
    return _size;
}

std::vector<Node*> Node2D::getChildrenInPaintOrder() const
{
    std::vector<Node*> children = getChildren();
    std::stable_sort(children.begin(), children.end(), [](const Node* a, const Node* b) {
        const int zA = a->is2D() ? static_cast<const Node2D*>(a)->_zOrder : 0;
        const int zB = b->is2D() ? static_cast<const Node2D*>(b)->_zOrder : 0;
        return zA < zB;
    });
    return children;
}

// === Paint ===

void Node2D::paint(const UIPaintContext& ctx)
{
    if (!isVisibleForRender()) {
        return;
    }
    paintSelf(ctx);
    paintChildren(ctx);
}

void Node2D::paintChildren(const UIPaintContext& ctx)
{
    for (Node* child : getChildrenInPaintOrder()) {
        if (child->is2D()) {
            static_cast<Node2D*>(child)->paint(ctx);
        }
        else {
            paintTransparent(child, ctx);
        }
    }
}

void Node2D::paintTransparent(Node* node, const UIPaintContext& ctx)
{
    for (Node* child : node->getChildren()) {
        if (child->is2D()) {
            static_cast<Node2D*>(child)->paint(ctx);
        }
        else {
            paintTransparent(child, ctx);
        }
    }
}

// === Events ===

bool Node2D::handleInputEvent(const Event& event, const UIEventContext& ctx)
{
    (void)event;
    (void)ctx;
    return false; // Passive: panels/canvas/text never consume events.
}

// === UICanvasNode ===

void UICanvasNode::layout(const Rect2D& parentRect)
{
    layoutAssigned(parentRect); // Canvas roots fill the assigned area.
}

// === UIPanelNode ===

void UIPanelNode::paintSelf(const UIPaintContext& ctx)
{
    Texture* texture = _image.isLoaded() ? _image.getShared().get() : nullptr;
    Render2D::makeSprite(glm::vec3(toScreenPxPos(ctx, _layoutRect.pos), 0.0f),
                         toScreenPxSize(ctx, _layoutRect.extent),
                         texture,
                         _color);
}

// === UITextNode ===

void UITextNode::paintSelf(const UIPaintContext& ctx)
{
    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, _fontSize);
    if (!font) {
        return;
    }

    const glm::vec2 pos  = toScreenPxPos(ctx, _layoutRect.pos);
    const glm::vec2 size = toScreenPxSize(ctx, _layoutRect.extent);
    glm::vec2       drawPos = pos;

    const float textWidth  = font->measureText(_text);
    const float textScaleX = ctx.canvas.zoom * ctx.uiScale.x;
    const float textScaleY = ctx.canvas.zoom * ctx.uiScale.y;
    if (_hAlign == EUIAlignH::Center) {
        drawPos.x += (size.x - textWidth * textScaleX) * 0.5f;
    }
    else if (_hAlign == EUIAlignH::Right) {
        drawPos.x += size.x - textWidth * textScaleX;
    }
    if (_vAlign == EUIAlignV::Center) {
        drawPos.y += (size.y - font->lineHeight * textScaleY) * 0.5f;
    }
    else if (_vAlign == EUIAlignV::Bottom) {
        drawPos.y += size.y - font->lineHeight * textScaleY;
    }
    Render2D::makeText(_text, glm::vec3(drawPos, 0.0f), _color, font.get());
}

glm::vec2 UITextNode::computeDesiredSize() const
{
    if (!_bAutoSize) {
        return _size;
    }
    auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, _fontSize);
    if (!font) {
        return _size;
    }
    return {font->measureText(_text), font->lineHeight};
}

// === UIButtonNode ===

void UIButtonNode::paintSelf(const UIPaintContext& ctx)
{
    const glm::vec4 color = _bPressed
                                ? _pressedColor
                                : (_bHovered ? _hoveredColor : _normalColor);
    Render2D::makeSprite(glm::vec3(toScreenPxPos(ctx, _layoutRect.pos), 0.0f),
                         toScreenPxSize(ctx, _layoutRect.extent),
                         nullptr,
                         color);
}

bool UIButtonNode::handleInputEvent(const Event& event, const UIEventContext& ctx)
{
    if (!hitTestLayoutRect(ctx.canvasPoint)) {
        return false;
    }

    switch (event.getEventType()) {
    case EEvent::MouseButtonPressed:
        _bPressed = true;
        return true;
    case EEvent::MouseButtonReleased:
        if (_bPressed) {
            _bPressed = false;
            if (hitTestLayoutRect(ctx.canvasPoint)) {
                YA_CORE_INFO("UIButton '{}' clicked", getName());
                if (_onClick) {
                    _onClick();
                }
            }
        }
        return true;
    case EEvent::MouseMoved:
        _bHovered = true;
        return true;
    default:
        return false;
    }
}

// === UIContainerNode ===

void UIContainerNode::layout(const Rect2D& parentRect)
{
    _layoutRect = computeAnchorRect(parentRect);

    const float pad = _padding;
    Rect2D content = _layoutRect;
    content.pos += glm::vec2(pad, pad);
    content.extent -= glm::vec2(pad * 2.0f, pad * 2.0f);
    arrangeChildren(content);
}

void UIContainerNode::arrangeChildren(const Rect2D& contentRect)
{
    const bool bHorizontal = _direction == EUIBoxLayout::Horizontal;
    float      cursor      = bHorizontal ? contentRect.pos.x : contentRect.pos.y;

    for (Node* child : getChildrenInPaintOrder()) {
        if (child->is2D()) {
            auto* child2D = static_cast<Node2D*>(child);
            if (!child2D->participatesInLayout()) {
                continue;
            }
            const glm::vec2 desired = child2D->computeDesiredSize();
            Rect2D childRect;
            if (bHorizontal) {
                childRect = Rect2D{
                    .pos    = {cursor, contentRect.pos.y},
                    .extent = {desired.x, contentRect.extent.y},
                };
                cursor += desired.x + _spacing;
            }
            else {
                childRect = Rect2D{
                    .pos    = {contentRect.pos.x, cursor},
                    .extent = {contentRect.extent.x, desired.y},
                };
                cursor += desired.y + _spacing;
            }
            child2D->layoutAssigned(childRect);
        }
        else {
            layoutTransparent(child, _layoutRect);
        }
    }
}

void UIContainerNode::paint(const UIPaintContext& ctx)
{
    if (!isVisibleForRender()) {
        return;
    }
    paintSelf(ctx);
    if (_bClipChildren) {
        Render2D::pushClipRect(Rect2D{
            .pos    = toScreenPxPos(ctx, _layoutRect.pos),
            .extent = toScreenPxSize(ctx, _layoutRect.extent),
        });
    }
    paintChildren(ctx);
    if (_bClipChildren) {
        Render2D::popClipRect();
    }
}

glm::vec2 UIContainerNode::computeDesiredSize() const
{
    const bool bHorizontal = _direction == EUIBoxLayout::Horizontal;
    float      content     = 0.0f;
    float      cross       = 0.0f;
    size_t     count       = 0;
    for (Node* child : getChildren()) {
        if (!child->is2D()) {
            continue;
        }
        auto* child2D = static_cast<Node2D*>(child);
        if (!child2D->participatesInLayout()) {
            continue;
        }
        const glm::vec2 desired = child2D->computeDesiredSize();
        if (bHorizontal) {
            content += desired.x;
            cross = std::max(cross, desired.y);
        }
        else {
            content += desired.y;
            cross = std::max(cross, desired.x);
        }
        ++count;
    }
    if (count > 0) {
        content += static_cast<float>(count - 1) * _spacing;
    }
    return {content + _padding * 2.0f, cross + _padding * 2.0f};
}

// === Serialization ===

nlohmann::json Node2D::serializeFields() const
{
    auto* cls = ClassRegistry::instance().getClass(getTypeIndex());
    if (!cls) {
        return nlohmann::json();
    }
    return ReflectionSerializer::serializeByRuntimeReflection(this, getTypeIndex(), cls->getName());
}

void Node2D::deserializeFields(const nlohmann::json& fields)
{
    auto* cls = ClassRegistry::instance().getClass(getTypeIndex());
    if (!cls) {
        return;
    }
    ReflectionSerializer::deserializeByRuntimeReflection(this, getTypeIndex(), fields, cls->getName());
}

std::shared_ptr<Node2D> createNode2DByTypeName(const std::string& typeName, const std::string& name)
{
    const auto& factories = getNode2DFactories();
    const auto  it        = factories.find(shortTypeName(typeName));
    return it == factories.end() ? nullptr : it->second(name);
}

std::vector<std::string> getRegisteredUINodeTypeNames()
{
    std::vector<std::string> names;
    for (const auto& [shortName, factory] : getNode2DFactories()) {
        (void)factory;
        names.push_back(shortName);
    }
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace ya

// Enum reflection for serialization (must register at global scope)
YA_REFLECT_ENUM_BEGIN(ya::EUIAlignH)
YA_REFLECT_ENUM_VALUE(Left)
YA_REFLECT_ENUM_VALUE(Center)
YA_REFLECT_ENUM_VALUE(Right)
YA_REFLECT_ENUM_END()

YA_REFLECT_ENUM_BEGIN(ya::EUIAlignV)
YA_REFLECT_ENUM_VALUE(Top)
YA_REFLECT_ENUM_VALUE(Center)
YA_REFLECT_ENUM_VALUE(Bottom)
YA_REFLECT_ENUM_END()

YA_REFLECT_ENUM_BEGIN(ya::EUIHitFilter)
YA_REFLECT_ENUM_VALUE(Pass)
YA_REFLECT_ENUM_VALUE(Stop)
YA_REFLECT_ENUM_END()

YA_REFLECT_ENUM_BEGIN(ya::EUIVisibility)
YA_REFLECT_ENUM_VALUE(Visible)
YA_REFLECT_ENUM_VALUE(Hidden)
YA_REFLECT_ENUM_VALUE(Collapsed)
YA_REFLECT_ENUM_VALUE(HitTestInvisible)
YA_REFLECT_ENUM_VALUE(SelfHitTestInvisible)
YA_REFLECT_ENUM_END()

YA_REFLECT_ENUM_BEGIN(ya::EUIBoxLayout)
YA_REFLECT_ENUM_VALUE(Horizontal)
YA_REFLECT_ENUM_VALUE(Vertical)
YA_REFLECT_ENUM_END()
