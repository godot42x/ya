
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Node;
class Element;

class CommandBuffer;
class RenderPass;
struct Event
{
    enum Type
    {
        MouseButtonDown,
    } type;

    union
    {
        struct MouseButton
        {
            size_t x, y;
        } mouseButton;
    };
};


struct Style
{
    float width      = 100;
    float height     = 100;
    float x          = 0;
    float y          = 0;
    float padding[4] = {0}; // top, right, bottom, left
    float margin[4]  = {0}; // top, right, bottom, left

    std::string backgroundColor = "#FFFFFF";
    std::string textColor       = "#000000";
};

using EventCallBack_t = std::function<void()>;

class Element
{
  protected:
    std::string                                      tag;
    Element                                         *parent = nullptr;
    std::vector<std::shared_ptr<Element>>            children;
    std::unordered_map<std::string, std::string>     properties;
    std::unordered_map<std::string, EventCallBack_t> eventHandlers;
    Style                                            style;


  public:
    Element(const std::string &tag) : tag(tag) {}
    virtual ~Element() = default;

    // 添加子元素
    void addChild(std::shared_ptr<Element> child)
    {
        children.push_back(child);
        child->parent = this;
    }

    // 设置属性
    void setProperty(const std::string &name, const std::string &value)
    {
        properties[name] = value;
    }

    // 设置事件处理函数
    void on(const std::string &eventName, EventCallBack_t callback)
    {
        eventHandlers[eventName] = callback;
    }

    // 样式设置
    Style &getStyle() { return style; }

    // 布局计算（递归）
    virtual void layout(float parentWidth, float parentHeight)
    {
        float x = parent ? parent->style.x + style.margin[3] : style.margin[3];
        float y = parent ? parent->style.y + style.margin[0] : style.margin[0];

        style.x = x;
        style.y = y;

        // 布局子元素
        for (auto &child : children) {
            child->layout(style.width, style.height);
        }
    }

    // 渲染（递归）
    virtual void render(CommandBuffer *cmdBuffer, RenderPass *renderPass)
    {
        // 渲染自身（子类实现具体渲染）

        // 渲染子元素
        for (auto &child : children) {
            child->render(cmdBuffer, renderPass);
        }
    }

    // 处理事件（递归）
    virtual bool handleEvent(const Event &event)
    {
        // 检查自身是否处理了事件

        // 传递给子元素
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            if ((*it)->handleEvent(event)) {
                return true;
            }
        }

        return false;
    }
};


class ButtonElement : public Element
{

  private:
    std::string text;

  public:
    ButtonElement(const std::string &text) : Element("button"), text(text)
    {
        // 默认样式
        style.backgroundColor = "#4285F4";
        style.textColor       = "#FFFFFF";
        style.padding[0] = style.padding[1] = style.padding[2] = style.padding[3] = 10;
    }

    virtual void render(CommandBuffer *cmdBuffer, RenderPass *renderPass) override
    {
        // 渲染按钮背景和文本

        // 调用基类渲染子元素
        Element::render(cmdBuffer, renderPass);
    }

    virtual bool handleEvent(const Event &event) override
    {
        if (event.type == Event::MouseButtonDown) {
            // 检查点击是否在按钮范围内
            float mouseX = event.mouseButton.x;
            float mouseY = event.mouseButton.y;

            if (mouseX >= style.x && mouseX <= style.x + style.width &&
                mouseY >= style.y && mouseY <= style.y + style.height) {
                // 触发点击事件
                auto it = eventHandlers.find("click");
                if (it != eventHandlers.end()) {
                    it->second();
                    return true;
                }
            }
        }

        return Element::handleEvent(event);
    }
};



// UI容器/管理器
class UIManager
{
  private:
    std::shared_ptr<Element> root;

  public:
    UIManager() : root(std::make_shared<Element>("root"))
    {
        root->getStyle().width  = 800;
        root->getStyle().height = 600;
    }

    // 添加元素到根
    template <typename T, typename... Args>
    std::shared_ptr<T> createElement(Args &&...args)
    {
        auto element = std::make_shared<T>(std::forward<Args>(args)...);
        root->addChild(element);
        return element;
    }

    // 布局所有元素
    void layout(float width, float height)
    {
        root->getStyle().width  = width;
        root->getStyle().height = height;
        root->layout(width, height);
    }

    // 渲染所有元素
    void render(CommandBuffer *cmdBuffer, RenderPass *renderPass)
    {
        root->render(cmdBuffer, renderPass);
    }

    // 处理事件
    bool handleEvent(const Event &event)
    {
        return root->handleEvent(event);
    }
};



/**
 *
 */
inline void todo()
{
    /**
    <div>
        <text> hello world </>
        <button onClick="alert('hello')"/>
    </>

    Tag<div>{
        .name = "div"
        .size = {
            .width = 100,
            .height = 100
            .width = "100"
        },

    }[
        Tag<text> hello world{},
        Tag<button>{}.onClick("alert('hello')")
    ]
    */
}
