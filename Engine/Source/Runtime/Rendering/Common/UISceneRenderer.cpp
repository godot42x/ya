#include "Runtime/Rendering/Common/UISceneRenderer.h"

#include "Render/2D/Render2D.h"
#include "Resource/Font/FontManager.h"
#include "Scene/Node2D.h"

#include <algorithm>

namespace ya
{

namespace
{

void collectNode2D(Node* node, std::vector<Node2D*>& out)
{
    if (!node) {
        return;
    }
    for (Node* child : node->getChildren()) {
        if (auto* node2D = dynamic_cast<Node2D*>(child)) {
            out.push_back(node2D);
        }
        collectNode2D(child, out);
    }
}

std::vector<Node2D*> collectSorted(Node* sceneRoot)
{
    std::vector<Node2D*> nodes;
    collectNode2D(sceneRoot, nodes);
    // Stable sort: lower zOrder first (drawn earlier), DFS order preserved for
    // equal zOrder, so later siblings draw on top of earlier ones.
    std::stable_sort(nodes.begin(), nodes.end(), [](const Node2D* a, const Node2D* b) {
        return a->_zOrder < b->_zOrder;
    });
    return nodes;
}

glm::vec2 transformCanvasPoint(const glm::vec2& point, const UICanvasTransform& canvas)
{
    return point * canvas.zoom + canvas.pan;
}

glm::vec2 transformCanvasSize(const glm::vec2& size, const UICanvasTransform& canvas)
{
    return size * canvas.zoom;
}

void drawNode2D(Node2D* node, const glm::vec2& uiScale, const UICanvasTransform& canvas)
{
    if (!node || !node->_visible) {
        return;
    }

    const glm::vec2 pos  = transformCanvasPoint(node->getScreenPosition(), canvas) * uiScale;
    const glm::vec2 size = transformCanvasSize(node->_size, canvas) * uiScale;
    const glm::vec3 screenPos(pos, 0.0f);

    if (auto* panel = dynamic_cast<UIPanelNode*>(node)) {
        Texture* texture = panel->_image.isLoaded() ? panel->_image.getShared().get() : nullptr;
        Render2D::makeSprite(screenPos, size, texture, panel->_color);
        return;
    }

    if (auto* text = dynamic_cast<UITextNode*>(node)) {
        auto font = FontManager::get()->getFont(DEFAULT_RUNTIME_FONT_NAME, text->_fontSize);
        if (!font) {
            return;
        }
        glm::vec2 drawPos = pos;
        const float textWidth = font->measureText(text->_text);
        const float textScale = canvas.zoom * uiScale.x;
        if (text->_hAlign == EUIAlignH::Center) {
            drawPos.x += (size.x - textWidth * textScale) * 0.5f;
        }
        else if (text->_hAlign == EUIAlignH::Right) {
            drawPos.x += size.x - textWidth * textScale;
        }
        // VAlign: approximate with the font line height for v1.
        if (text->_vAlign == EUIAlignV::Center) {
            drawPos.y += (size.y - font->lineHeight * canvas.zoom * uiScale.y) * 0.5f;
        }
        else if (text->_vAlign == EUIAlignV::Bottom) {
            drawPos.y += size.y - font->lineHeight * canvas.zoom * uiScale.y;
        }
        Render2D::makeText(text->_text, glm::vec3(drawPos, 0.0f), text->_color, font.get());
        return;
    }

    if (auto* button = dynamic_cast<UIButtonNode*>(node)) {
        const glm::vec4 color = button->_bPressed
                                    ? button->_pressedColor
                                    : (button->_bHovered ? button->_hoveredColor : button->_normalColor);
        Render2D::makeSprite(screenPos, size, nullptr, color);
        return;
    }
}

} // namespace

void UISceneRenderer::render(Node* sceneRoot, const glm::vec2& uiScale, const UICanvasTransform& canvas)
{
    const auto nodes = collectSorted(sceneRoot);
    for (Node2D* node : nodes) {
        drawNode2D(node, uiScale, canvas);
    }
}

bool UISceneRenderer::handleEvent(const Event& event, const UIAppCtx& ctx, Node* sceneRoot)
{
    if (!sceneRoot || !ctx.bInViewport) {
        return false;
    }

    const EEvent::T eventType = event.getEventType();
    if (eventType != EEvent::MouseButtonPressed &&
        eventType != EEvent::MouseButtonReleased &&
        eventType != EEvent::MouseMoved) {
        return false;
    }

    auto nodes = collectSorted(sceneRoot);
    if (nodes.empty()) {
        return false;
    }

    const glm::vec2 point = ctx.lastMousePos - ctx.viewportRect.pos;

    if (eventType == EEvent::MouseMoved) {
        for (Node2D* node : nodes) {
            if (auto* button = dynamic_cast<UIButtonNode*>(node)) {
                button->_bHovered = false;
            }
        }
    }

    // Topmost first (draw order reversed). Only interactive nodes (buttons, v1)
    // consume events; panels/canvas/text are passive so a full-screen canvas
    // does not block gameplay clicks.
    for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
        Node2D* node = *it;
        if (!node->_visible || !node->hitTest(point)) {
            continue;
        }

        if (auto* button = dynamic_cast<UIButtonNode*>(node)) {
            if (eventType == EEvent::MouseButtonPressed) {
                button->_bPressed = true;
            }
            else if (eventType == EEvent::MouseButtonReleased) {
                if (button->_bPressed) {
                    button->_bPressed = false;
                    if (button->hitTest(point)) {
                        YA_CORE_INFO("UIButton '{}' clicked", button->getName());
                        if (button->_onClick) {
                            button->_onClick();
                        }
                    }
                }
            }
            else if (eventType == EEvent::MouseMoved) {
                button->_bHovered = true;
            }
            return true;
        }
    }

    return false;
}

} // namespace ya
