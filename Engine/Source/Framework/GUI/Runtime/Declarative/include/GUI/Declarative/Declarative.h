#pragma once

#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/WidgetTree.h"

#include <functional>
#include <concepts>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <optional>
#include <utility>
#include <vector>

namespace ya::ui
{

enum class EWidgetKind : uint8_t
{
    Column,
    Row,
    Panel,
    Text,
    Button,
    TextField,
};

/// Typed payload for panel-only properties. This is the first compatibility
/// migration away from the flat UIDescription god struct; old fields remain
/// temporarily so existing descriptions stay source-compatible.
struct UIPanelDescription
{
    std::optional<glm::vec4> color;
    std::optional<float> cornerRadius;
};

struct UITextDescription
{
    std::optional<std::string> text;
    std::optional<uint32_t> fontSize;
    std::optional<glm::vec4> color;
};

struct UIButtonDescription
{
    std::optional<std::string> text;
    std::optional<std::function<void()>> onClick;
};

struct UITextFieldDescription
{
    std::optional<std::string> text;
    std::optional<uint32_t> fontSize;
};

struct UIDescription;

struct UICommonDescription
{
    std::optional<std::string> key;
    std::optional<std::string> displayName;
    std::vector<UIDescription> children;
    std::optional<glm::vec2> position;
    std::optional<glm::vec2> size;
    std::optional<bool> enabled;
    std::optional<EWidgetFocusPolicy> focusPolicy;
};

/// React-style declarative description node. The render function returns a tree
/// of these; the reconciler turns it into retained UIElement widgets.
struct UIDescription
{
    EWidgetKind            kind = EWidgetKind::Panel;
    UICommonDescription    common;
    std::string            key;
    std::string            displayName;
    std::vector<UIDescription> children;

    glm::vec2 _position = {0.0f, 0.0f};
    bool      _bHasPosition = false;
    glm::vec2 _size = {0.0f, 0.0f};
    bool      _bHasSize = false;
    bool      _bEnabled = true;
    bool      _bHasEnabled = false;
    EWidgetFocusPolicy _focusPolicy = EWidgetFocusPolicy::None;
    bool      _bHasFocusPolicy = false;
    glm::vec4 _color = {1.0f, 1.0f, 1.0f, 1.0f};
    bool      _bHasColor = false;
    uint32_t  _fontSize = 0;
    bool      _bHasFontSize = false;
    std::string _text;
    bool        _bHasText = false;
    float       _spacing = 0.0f;
    bool        _bHasSpacing = false;
    glm::vec2   _padding = {0.0f, 0.0f};
    bool        _bHasPadding = false;
    bool        _bClipChildren = false;
    bool        _bHasClipChildren = false;
    bool        _bStretchLastChild = false;
    bool        _bHasStretchLastChild = false;
    bool        _bHasOnClick = false;
    std::function<void()> _onClick;
    std::optional<UIPanelDescription> _panel;
    std::optional<UITextDescription> _textDescription;
    std::optional<UIButtonDescription> _button;
    std::optional<UITextFieldDescription> _textField;

    UIDescription() = default;
    UIDescription(EWidgetKind kind, std::string key, std::string displayName = {})
        : kind(kind)
        , key(std::move(key))
        , displayName(displayName.empty() ? this->key : std::move(displayName))
    {
        syncCommonIdentity();
    }

    void syncCommonIdentity()
    {
        common.key = key;
        common.displayName = displayName;
        common.children = children;
    }

    void syncCommonLayout()
    {
        common.position = _bHasPosition ? std::optional<glm::vec2>(_position) : std::nullopt;
        common.size = _bHasSize ? std::optional<glm::vec2>(_size) : std::nullopt;
        common.enabled = _bHasEnabled ? std::optional<bool>(_bEnabled) : std::nullopt;
        common.focusPolicy = _bHasFocusPolicy ? std::optional<EWidgetFocusPolicy>(_focusPolicy) : std::nullopt;
    }
};

template<typename TFactory>
concept UIDescriptionFactory = requires(TFactory&& factory)
{
    UIDescription(std::invoke(std::forward<TFactory>(factory)));
};

template<typename TDerived>
class TUIBuilderBase
{
  public:
    TUIBuilderBase(EWidgetKind kind, std::string key, std::string displayName = {})
        : _node(kind, std::move(key), std::move(displayName))
    {
    }

    [[nodiscard]] operator UIDescription() const & { return _node; }
    [[nodiscard]] operator UIDescription() &&
    {
        return std::move(_node);
    }

    [[nodiscard]] UIDescription build() const & { return _node; }

    [[nodiscard]] UIDescription build() &&
    {
        return std::move(_node);
    }

    [[nodiscard]] TDerived& setPosition(const glm::vec2& value) &
    {
        _node._position = value;
        _node._bHasPosition = true;
        _node.syncCommonLayout();
        return derived();
    }

    [[nodiscard]] TDerived&& setPosition(const glm::vec2& value) &&
    {
        _node._position = value;
        _node._bHasPosition = true;
        _node.syncCommonLayout();
        return std::move(derived());
    }

    [[nodiscard]] TDerived& setSize(const glm::vec2& value) &
    {
        _node._size = value;
        _node._bHasSize = true;
        _node.syncCommonLayout();
        return derived();
    }

    [[nodiscard]] TDerived&& setSize(const glm::vec2& value) &&
    {
        _node._size = value;
        _node._bHasSize = true;
        _node.syncCommonLayout();
        return std::move(derived());
    }

    [[nodiscard]] TDerived& setEnabled(bool value) &
    {
        _node._bEnabled = value;
        _node._bHasEnabled = true;
        _node.syncCommonLayout();
        return derived();
    }

    [[nodiscard]] TDerived&& setEnabled(bool value) &&
    {
        _node._bEnabled = value;
        _node._bHasEnabled = true;
        _node.syncCommonLayout();
        return std::move(derived());
    }

    [[nodiscard]] TDerived& setFocusPolicy(EWidgetFocusPolicy value) &
    {
        _node._focusPolicy = value;
        _node._bHasFocusPolicy = true;
        _node.syncCommonLayout();
        return derived();
    }

    [[nodiscard]] TDerived&& setFocusPolicy(EWidgetFocusPolicy value) &&
    {
        _node._focusPolicy = value;
        _node._bHasFocusPolicy = true;
        _node.syncCommonLayout();
        return std::move(derived());
    }

  protected:
    UIDescription _node;

  private:
    [[nodiscard]] TDerived& derived() & { return static_cast<TDerived&>(*this); }
    [[nodiscard]] TDerived&& derived() && { return static_cast<TDerived&&>(*this); }
};

template<typename TDerived>
class TUIChildrenBuilder : public TUIBuilderBase<TDerived>
{
  public:
    using TUIBuilderBase<TDerived>::TUIBuilderBase;

  private:
    void appendChild(UIDescription node)
    {
        this->_node.children.push_back(std::move(node));
        this->_node.syncCommonIdentity();
    }

  public:

    [[nodiscard]] TDerived& child(UIDescription node) &
    {
        appendChild(std::move(node));
        return static_cast<TDerived&>(*this);
    }

    [[nodiscard]] TDerived&& child(UIDescription node) &&
    {
        appendChild(std::move(node));
        return static_cast<TDerived&&>(*this);
    }

    /// Static DSL composition: children are already-built descriptions.
    template<typename... TNodes>
        requires(std::convertible_to<TNodes, UIDescription> && ...)
    [[nodiscard]] TDerived& children(TNodes&&... nodes) &
    {
        (appendChild(UIDescription(std::forward<TNodes>(nodes))), ...);
        return static_cast<TDerived&>(*this);
    }

    template<typename... TNodes>
        requires(std::convertible_to<TNodes, UIDescription> && ...)
    [[nodiscard]] TDerived&& children(TNodes&&... nodes) &&
    {
        (appendChild(UIDescription(std::forward<TNodes>(nodes))), ...);
        return static_cast<TDerived&&>(*this);
    }

    /// Functional composition extension. The factory is evaluated immediately
    /// into the stable static description DSL; it is not stored in UIDescription.
    template<UIDescriptionFactory TFactory>
    [[nodiscard]] TDerived& compose(TFactory&& factory) &
    {
        appendChild(std::invoke(std::forward<TFactory>(factory)));
        return static_cast<TDerived&>(*this);
    }

    template<UIDescriptionFactory TFactory>
    [[nodiscard]] TDerived&& compose(TFactory&& factory) &&
    {
        appendChild(std::invoke(std::forward<TFactory>(factory)));
        return static_cast<TDerived&&>(*this);
    }

    template<UIDescriptionFactory... TFactories>
    [[nodiscard]] TDerived& composeChildren(TFactories&&... factories) &
    {
        (appendChild(std::invoke(std::forward<TFactories>(factories))), ...);
        return static_cast<TDerived&>(*this);
    }

    template<UIDescriptionFactory... TFactories>
    [[nodiscard]] TDerived&& composeChildren(TFactories&&... factories) &&
    {
        (appendChild(std::invoke(std::forward<TFactories>(factories))), ...);
        return static_cast<TDerived&&>(*this);
    }

    /// Compatibility alias for the former factory-based API. New code should
    /// use compose() so the static DSL and functional extension stay distinct.
    template<UIDescriptionFactory TFactory>
    [[nodiscard]] TDerived& content(TFactory&& factory) &
    {
        return compose(std::forward<TFactory>(factory));
    }

    template<UIDescriptionFactory TFactory>
    [[nodiscard]] TDerived&& content(TFactory&& factory) &&
    {
        return std::move(compose(std::forward<TFactory>(factory)));
    }

    template<UIDescriptionFactory TFactory>
    [[nodiscard]] TDerived& when(bool condition, TFactory&& factory) &
    {
        if (condition) {
            appendChild(std::invoke(std::forward<TFactory>(factory)));
        }
        return static_cast<TDerived&>(*this);
    }

    template<UIDescriptionFactory TFactory>
    [[nodiscard]] TDerived&& when(bool condition, TFactory&& factory) &&
    {
        if (condition) {
            appendChild(std::invoke(std::forward<TFactory>(factory)));
        }
        return static_cast<TDerived&&>(*this);
    }
};

class UIContainerBuilder final : public TUIChildrenBuilder<UIContainerBuilder>
{
  public:
    UIContainerBuilder(EWidgetKind kind, std::string key, std::string displayName = {})
        : TUIChildrenBuilder(kind, std::move(key), std::move(displayName))
    {
    }

    [[nodiscard]] UIContainerBuilder& setSpacing(float value) &
    {
        _node._spacing = value;
        _node._bHasSpacing = true;
        return *this;
    }

    [[nodiscard]] UIContainerBuilder&& setSpacing(float value) &&
    {
        _node._spacing = value;
        _node._bHasSpacing = true;
        return std::move(*this);
    }

    [[nodiscard]] UIContainerBuilder& setPadding(const glm::vec2& value) &
    {
        _node._padding = value;
        _node._bHasPadding = true;
        return *this;
    }

    [[nodiscard]] UIContainerBuilder&& setPadding(const glm::vec2& value) &&
    {
        _node._padding = value;
        _node._bHasPadding = true;
        return std::move(*this);
    }

    [[nodiscard]] UIContainerBuilder& setClipChildren(bool value) &
    {
        _node._bClipChildren = value;
        _node._bHasClipChildren = true;
        return *this;
    }

    [[nodiscard]] UIContainerBuilder&& setClipChildren(bool value) &&
    {
        _node._bClipChildren = value;
        _node._bHasClipChildren = true;
        return std::move(*this);
    }

    [[nodiscard]] UIContainerBuilder& setStretchLastChild(bool value) &
    {
        _node._bStretchLastChild = value;
        _node._bHasStretchLastChild = true;
        return *this;
    }

    [[nodiscard]] UIContainerBuilder&& setStretchLastChild(bool value) &&
    {
        _node._bStretchLastChild = value;
        _node._bHasStretchLastChild = true;
        return std::move(*this);
    }
};

class UIPanelBuilder final : public TUIChildrenBuilder<UIPanelBuilder>
{
  public:
    UIPanelBuilder(std::string key, std::string displayName = {})
        : TUIChildrenBuilder(EWidgetKind::Panel, std::move(key), std::move(displayName))
    {
        _node._panel.emplace();
    }

    [[nodiscard]] UIPanelBuilder& setColor(const glm::vec4& value) &
    {
        _node._color = value;
        _node._bHasColor = true;
        _node._panel->color = value;
        return *this;
    }

    [[nodiscard]] UIPanelBuilder&& setColor(const glm::vec4& value) &&
    {
        _node._color = value;
        _node._bHasColor = true;
        _node._panel->color = value;
        return std::move(*this);
    }

    [[nodiscard]] UIPanelBuilder& setCornerRadius(float value) &
    {
        _node._panel->cornerRadius = value;
        return *this;
    }

    [[nodiscard]] UIPanelBuilder&& setCornerRadius(float value) &&
    {
        _node._panel->cornerRadius = value;
        return std::move(*this);
    }
};

class UITextBuilder final : public TUIBuilderBase<UITextBuilder>
{
  public:
    UITextBuilder(std::string key, std::string displayName = {})
        : TUIBuilderBase(EWidgetKind::Text, std::move(key), std::move(displayName))
    {
        _node._textDescription.emplace();
    }

    [[nodiscard]] UITextBuilder& setColor(const glm::vec4& value) &
    {
        _node._color = value;
        _node._bHasColor = true;
        _node._textDescription->color = value;
        return *this;
    }

    [[nodiscard]] UITextBuilder&& setColor(const glm::vec4& value) &&
    {
        _node._color = value;
        _node._bHasColor = true;
        _node._textDescription->color = value;
        return std::move(*this);
    }

    [[nodiscard]] UITextBuilder& setFontSize(uint32_t value) &
    {
        _node._fontSize = value;
        _node._bHasFontSize = true;
        _node._textDescription->fontSize = value;
        return *this;
    }

    [[nodiscard]] UITextBuilder&& setFontSize(uint32_t value) &&
    {
        _node._fontSize = value;
        _node._bHasFontSize = true;
        _node._textDescription->fontSize = value;
        return std::move(*this);
    }

    [[nodiscard]] UITextBuilder& setText(std::string value) &
    {
        _node._text = std::move(value);
        _node._bHasText = true;
        _node._textDescription->text = _node._text;
        return *this;
    }

    [[nodiscard]] UITextBuilder&& setText(std::string value) &&
    {
        _node._text = std::move(value);
        _node._bHasText = true;
        _node._textDescription->text = _node._text;
        return std::move(*this);
    }
};

class UIButtonBuilder final : public TUIChildrenBuilder<UIButtonBuilder>
{
  public:
    UIButtonBuilder(std::string key, std::string displayName = {})
        : TUIChildrenBuilder(EWidgetKind::Button, std::move(key), std::move(displayName))
    {
        _node._button.emplace();
    }

    [[nodiscard]] UIButtonBuilder& setText(std::string value) &
    {
        _node._text = std::move(value);
        _node._bHasText = true;
        _node._button->text = _node._text;
        return *this;
    }

    [[nodiscard]] UIButtonBuilder&& setText(std::string value) &&
    {
        _node._text = std::move(value);
        _node._bHasText = true;
        _node._button->text = _node._text;
        return std::move(*this);
    }

    [[nodiscard]] UIButtonBuilder& onClick(std::function<void()> callback) &
    {
        _node._onClick = std::move(callback);
        _node._bHasOnClick = true;
        _node._button->onClick = _node._onClick;
        return *this;
    }

    [[nodiscard]] UIButtonBuilder&& onClick(std::function<void()> callback) &&
    {
        _node._onClick = std::move(callback);
        _node._bHasOnClick = true;
        _node._button->onClick = _node._onClick;
        return std::move(*this);
    }
};

class UITextFieldBuilder final : public TUIBuilderBase<UITextFieldBuilder>
{
  public:
    UITextFieldBuilder(std::string key, std::string displayName = {})
        : TUIBuilderBase(EWidgetKind::TextField, std::move(key), std::move(displayName))
    {
        _node._textField.emplace();
    }

    [[nodiscard]] UITextFieldBuilder& setFontSize(uint32_t value) &
    {
        _node._fontSize = value;
        _node._bHasFontSize = true;
        _node._textField->fontSize = value;
        return *this;
    }

    [[nodiscard]] UITextFieldBuilder&& setFontSize(uint32_t value) &&
    {
        _node._fontSize = value;
        _node._bHasFontSize = true;
        _node._textField->fontSize = value;
        return std::move(*this);
    }

    [[nodiscard]] UITextFieldBuilder& setText(std::string value) &
    {
        _node._text = std::move(value);
        _node._bHasText = true;
        _node._textField->text = _node._text;
        return *this;
    }

    [[nodiscard]] UITextFieldBuilder&& setText(std::string value) &&
    {
        _node._text = std::move(value);
        _node._bHasText = true;
        _node._textField->text = _node._text;
        return std::move(*this);
    }
};

[[nodiscard]] UIContainerBuilder column(std::string key, std::string displayName = {});
[[nodiscard]] UIContainerBuilder row(std::string key, std::string displayName = {});
[[nodiscard]] UIPanelBuilder panel(std::string key, std::string displayName = {});
[[nodiscard]] UITextBuilder text(std::string key, std::string displayName = {});
[[nodiscard]] UIButtonBuilder button(std::string key, std::string displayName = {});
[[nodiscard]] UITextFieldBuilder textField(std::string key, std::string displayName = {});

class YA_GUI_API UIReconciler final
{
  public:
    explicit UIReconciler(WidgetTree& tree, WidgetTree::ELayer layer = WidgetTree::ELayer::Content);

    [[nodiscard]] bool validateDescription(const UIDescription& description, std::string* error = nullptr) const;
    [[nodiscard]] UIElementRef reconcile(const UIDescription& description);

  private:
    [[nodiscard]] UIElementRef reconcileNode(UIElement& parent, const UIDescription& node);
    void applyNodeProperties(UIElement& widget, const UIDescription& node) const;
    void reconcileChildren(UIElement& widget, const UIDescription& node);
    [[nodiscard]] static std::string identityKey(const UIDescription& node);
    [[nodiscard]] static bool validateNode(const UIDescription& node, const std::string& path, std::string* error);
    [[nodiscard]] static bool isChildBefore(const UIElement& parent, const UIElement& lhs, const UIElement& rhs);
    [[nodiscard]] static bool isLastChild(const UIElement& parent, const UIElement& child);
    [[nodiscard]] UIElementRef findCompatibleChild(UIElement& parent, const UIDescription& node, std::unordered_set<UIElement*>& used) const;

    WidgetTree&        _tree;
    WidgetTree::ELayer _layer;
};

using UIRenderFunction = std::function<UIDescription()>;

/// Minimal one-way render coordinator. Business/event code mutates external
/// state, calls invalidate(), and the host flushes once after the transaction.
/// It deliberately does not read live widgets during paint/command recording.
class YA_GUI_API UIRenderController final
{
  public:
    explicit UIRenderController(WidgetTree& tree, WidgetTree::ELayer layer = WidgetTree::ELayer::Content);

    void setRenderFunction(UIRenderFunction function);
    void invalidate();
    void beginBatch();
    void endBatch();
    [[nodiscard]] bool flush();
    [[nodiscard]] bool isDirty() const { return _bDirty; }
    [[nodiscard]] uint32_t getBatchDepth() const { return _batchDepth; }

  private:
    WidgetTree&       _tree;
    UIReconciler      _reconciler;
    UIRenderFunction  _renderFunction;
    bool              _bDirty = false;
    uint32_t          _batchDepth = 0;
};

} // namespace ya::ui
