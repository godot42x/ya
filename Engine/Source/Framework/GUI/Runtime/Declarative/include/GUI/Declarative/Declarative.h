#pragma once

#include "GUI/Widgets/UIElement.h"
#include "GUI/Widgets/WidgetTree.h"

#include <functional>
#include <string>
#include <unordered_set>
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

/// React-style declarative description node. The render function returns a tree
/// of these; the reconciler turns it into retained UIElement widgets.
struct UIDescription
{
    EWidgetKind            kind = EWidgetKind::Panel;
    std::string            key;
    std::string            displayName;
    std::vector<UIDescription> children;

    glm::vec2 _position = {0.0f, 0.0f};
    bool      _bHasPosition = false;
    glm::vec2 _size = {0.0f, 0.0f};
    bool      _bHasSize = false;
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
    std::function<void()> _onClick;

    UIDescription() = default;
    UIDescription(EWidgetKind kind, std::string key, std::string displayName = {})
        : kind(kind)
        , key(std::move(key))
        , displayName(displayName.empty() ? this->key : std::move(displayName))
    {
    }
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
    [[nodiscard]] operator UIDescription() && { return std::move(_node); }
    [[nodiscard]] UIDescription build() const & { return _node; }
    [[nodiscard]] UIDescription build() && { return std::move(_node); }

    [[nodiscard]] TDerived& setPosition(const glm::vec2& value) &
    {
        _node._position = value;
        _node._bHasPosition = true;
        return derived();
    }

    [[nodiscard]] TDerived&& setPosition(const glm::vec2& value) &&
    {
        _node._position = value;
        _node._bHasPosition = true;
        return std::move(derived());
    }

    [[nodiscard]] TDerived& setSize(const glm::vec2& value) &
    {
        _node._size = value;
        _node._bHasSize = true;
        return derived();
    }

    [[nodiscard]] TDerived&& setSize(const glm::vec2& value) &&
    {
        _node._size = value;
        _node._bHasSize = true;
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

    [[nodiscard]] TDerived& child(UIDescription node) &
    {
        this->_node.children.push_back(std::move(node));
        return static_cast<TDerived&>(*this);
    }

    [[nodiscard]] TDerived&& child(UIDescription node) &&
    {
        this->_node.children.push_back(std::move(node));
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
    }

    [[nodiscard]] UIPanelBuilder& setColor(const glm::vec4& value) &
    {
        _node._color = value;
        _node._bHasColor = true;
        return *this;
    }

    [[nodiscard]] UIPanelBuilder&& setColor(const glm::vec4& value) &&
    {
        _node._color = value;
        _node._bHasColor = true;
        return std::move(*this);
    }
};

class UITextBuilder final : public TUIBuilderBase<UITextBuilder>
{
  public:
    UITextBuilder(std::string key, std::string displayName = {})
        : TUIBuilderBase(EWidgetKind::Text, std::move(key), std::move(displayName))
    {
    }

    [[nodiscard]] UITextBuilder& setColor(const glm::vec4& value) &
    {
        _node._color = value;
        _node._bHasColor = true;
        return *this;
    }

    [[nodiscard]] UITextBuilder&& setColor(const glm::vec4& value) &&
    {
        _node._color = value;
        _node._bHasColor = true;
        return std::move(*this);
    }

    [[nodiscard]] UITextBuilder& setFontSize(uint32_t value) &
    {
        _node._fontSize = value;
        _node._bHasFontSize = true;
        return *this;
    }

    [[nodiscard]] UITextBuilder&& setFontSize(uint32_t value) &&
    {
        _node._fontSize = value;
        _node._bHasFontSize = true;
        return std::move(*this);
    }

    [[nodiscard]] UITextBuilder& setText(std::string value) &
    {
        _node._text = std::move(value);
        _node._bHasText = true;
        return *this;
    }

    [[nodiscard]] UITextBuilder&& setText(std::string value) &&
    {
        _node._text = std::move(value);
        _node._bHasText = true;
        return std::move(*this);
    }
};

class UIButtonBuilder final : public TUIChildrenBuilder<UIButtonBuilder>
{
  public:
    UIButtonBuilder(std::string key, std::string displayName = {})
        : TUIChildrenBuilder(EWidgetKind::Button, std::move(key), std::move(displayName))
    {
    }

    [[nodiscard]] UIButtonBuilder& setText(std::string value) &
    {
        _node._text = std::move(value);
        _node._bHasText = true;
        return *this;
    }

    [[nodiscard]] UIButtonBuilder&& setText(std::string value) &&
    {
        _node._text = std::move(value);
        _node._bHasText = true;
        return std::move(*this);
    }

    [[nodiscard]] UIButtonBuilder& onClick(std::function<void()> callback) &
    {
        _node._onClick = std::move(callback);
        return *this;
    }

    [[nodiscard]] UIButtonBuilder&& onClick(std::function<void()> callback) &&
    {
        _node._onClick = std::move(callback);
        return std::move(*this);
    }
};

class UITextFieldBuilder final : public TUIBuilderBase<UITextFieldBuilder>
{
  public:
    UITextFieldBuilder(std::string key, std::string displayName = {})
        : TUIBuilderBase(EWidgetKind::TextField, std::move(key), std::move(displayName))
    {
    }

    [[nodiscard]] UITextFieldBuilder& setFontSize(uint32_t value) &
    {
        _node._fontSize = value;
        _node._bHasFontSize = true;
        return *this;
    }

    [[nodiscard]] UITextFieldBuilder&& setFontSize(uint32_t value) &&
    {
        _node._fontSize = value;
        _node._bHasFontSize = true;
        return std::move(*this);
    }

    [[nodiscard]] UITextFieldBuilder& setText(std::string value) &
    {
        _node._text = std::move(value);
        _node._bHasText = true;
        return *this;
    }

    [[nodiscard]] UITextFieldBuilder&& setText(std::string value) &&
    {
        _node._text = std::move(value);
        _node._bHasText = true;
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
    [[nodiscard]] UIElementRef createNode(const UIDescription& node) const;
    void applyNodeProperties(UIElement& widget, const UIDescription& node) const;
    void reconcileChildren(UIElement& widget, const UIDescription& node);
    [[nodiscard]] static bool sameKind(const UIElement& widget, EWidgetKind kind);
    [[nodiscard]] static std::string identityKey(const UIDescription& node);
    [[nodiscard]] static bool validateNode(const UIDescription& node, const std::string& path, std::string* error);
    [[nodiscard]] static const char* kindName(EWidgetKind kind);
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
