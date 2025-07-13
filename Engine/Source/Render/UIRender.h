#pragma once

#include "Core/Base.h"
#include "Core/FName.h"
#include "Render/Render.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations
struct RenderPass;
class Texture2D;

// 顶点数据结构
struct UIVertex {
  glm::vec2 position; // 屏幕坐标
  glm::vec2 texCoord; // 纹理坐标
  glm::vec4 color;    // 顶点颜色
  float textureId;    // 纹理ID (用于批处理)
};

namespace EVisibility {

enum T {
  Collapsed = 1 << 0,
  Hidden = 1 << 1,
  HitTestable = 1 << 2,
  Visible = 1 << 3,
  SelfOnly = 1 << 4, // only apply to self, not children
};

inline bool check(T &flags) {
  if (flags & SelfOnly && (flags & Collapsed || flags & Hidden)) {
    NE_CORE_ASSERT(false, "SelfOnly cannot be set with Collapsed or Hidden");
    return false;
  }

  return true;
}

}; // namespace EVisibility

// UI元素基类
class UIElement {

public:
  UIElement() = default;
  virtual ~UIElement() = default;

  // 基础属性
  glm::vec2 position{0.0f};
  glm::vec2 size{100.0f}; // width, height
  glm::vec4 color{1.0f};
  float rotation{0.0f};
  float scale{1.0f};
  int zOrder{0}; // 同层内的排序
  EVisibility::T visiblity;

  uint32_t layerID;

  glm::vec2 padding{0.0f};
  glm::vec4 margin{0.0f}; // 外边距

  // 变换相关
  virtual glm::mat4 getTransform() const {
    // TODO: Implement transform calculation
    return glm::mat4(1.0f);
  }
  virtual glm::vec4 getBounds() const {
    return glm::vec4(position.x, position.y, size.x, size.y);
  }

  //
  virtual void onPaint() = 0;

  // 更新和事件
  virtual void update(float deltaTime) {}
  virtual bool hitTest(const glm::vec2 &point) const;
};

// 文本UI元素
class UIText : public UIElement {
public:
  std::string text;
  float fontSize{16.0f};
  uint32_t fontTextureId{0};

  void onPaint() override;
};

// 图片UI元素
class UIImage : public UIElement {
public:
  uint32_t textureID{0};
  glm::vec4 uvRect{0.0f, 0.0f, 1.0f, 1.0f};
  bool maintainAspectRatio{true};

  void onPaint() override;
};

// 按钮UI元素
class UIButton : public UIElement {
public:
  enum class State { Normal, Hovered, Pressed, Disabled };

  std::string text;
  State currentState{State::Normal};

  // 不同状态的样式
  struct ButtonStyle {
    glm::vec4 backgroundColor{0.3f, 0.3f, 0.3f, 1.0f};
    glm::vec4 textColor{1.0f, 1.0f, 1.0f, 1.0f};
    uint32_t backgroundTexture{0};
  };

  ButtonStyle normalStyle;
  ButtonStyle hoveredStyle;
  ButtonStyle pressedStyle;
  ButtonStyle disabledStyle;

  void onPaint() override;

  // 事件处理
  virtual void onHover() { currentState = State::Hovered; }
  virtual void onPress() { currentState = State::Pressed; }
  virtual void onRelease() { currentState = State::Normal; }
  virtual void onClick() {} // 用户重写

private:
  const ButtonStyle &getCurrentStyle() const;
};

struct Texture2D; // unimplemented forward declaration

// UI渲染器
struct F2DRender {

public:
  // renderpass/pipeline setup, use the render.h to create a pipeline
  // vertex and index buffers
  // ...
  static bool initialize(uint32_t maxVertices = 10000,
                         uint32_t maxIndices = 15000);
  static void shutdown();

  static void beginFrame(const glm::mat4 &projectionMatrix);
  static void endFrame();
  static void render();
  static void submit();
  static void flush();

  static void drawQuad(const glm::vec2 &position, const glm::vec2 &size,
                       const glm::vec4 &color = {1.0f, 1.0f, 1.0f, 1.0f},
                       float rotation = 0.0f, float scale = 1.0f,
                       uint32_t textureId = 0);
  static void drawText(const std::string &text, const glm::vec2 &position,
                       float fontSize = 16.0f,
                       const glm::vec4 &color = {1.0f, 1.0f, 1.0f, 1.0f},
                       uint32_t fontTextureId = 0);
  static void drawImage(const std::shared_ptr<Texture2D> &texture,
                        const glm::vec2 &position, const glm::vec2 &size,
                        const glm::vec4 &uvRect = {0.0f, 0.0f, 1.0f, 1.0f},
                        float rotation = 0.0f, float scale = 1.0f,
                        bool maintainAspectRatio = true);
  static void drawLine(const glm::vec2 &start, const glm::vec2 &end,
                       const glm::vec4 &color = {1.0f, 1.0f, 1.0f, 1.0f},
                       float thickness = 1.0f);
  static void drawCircle(const glm::vec2 &center, float radius,
                         const glm::vec4 &color = {1.0f, 1.0f, 1.0f, 1.0f});

  // Statistics and debugging
  struct RenderStats {
    uint32_t drawCalls{0};
    uint32_t vertexCount{0};
    uint32_t indexCount{0};
    uint32_t quadCount{0};
  };
  
  static const RenderStats& getStats();
  static void resetStats();
};

// UI管理器 - 整合到渲染系统中
// class UIApplication {

// private:
//   std::unique_ptr<UI2DRenderer> m_renderer;
//   std::vector<std::shared_ptr<UIElement>> m_elements;

//   // 输入状态
//   glm::vec2 m_mousePosition{0.0f};
//   std::shared_ptr<UIElement> m_hoveredElement{nullptr};
//   std::shared_ptr<UIElement> m_pressedElement{nullptr};

//   // 屏幕信息
//   float m_screenWidth{800.0f};
//   float m_screenHeight{600.0f};

//   // 内部方法
//   std::shared_ptr<UIElement> findElementAt(const glm::vec2 &position);
//   void updateElementStates();

// public:
//   UIApplication();
//   ~UIApplication();

//   bool initialize();
//   void shutdown();

//   // 渲染集成
//   void registerWithRenderManager(class RenderPassManager *renderManager);
//   void render(RenderPass *renderPass);

//   // UI元素创建工厂
//   std::shared_ptr<UIText> createText(const std::string &text);
//   std::shared_ptr<UIImage> createImage(const std::string &texturePath);
//   std::shared_ptr<UIButton> createButton(const std::string &text);

//   // 元素管理
//   void addElement(std::shared_ptr<UIElement> element);
//   void removeElement(std::shared_ptr<UIElement> element);

//   // 更新
//   void update(float deltaTime);

//   // 事件处理
//   void handleMouseMove(float x, float y);
//   void handleMouseButton(int button, bool pressed, float x, float y);
//   void handleKeyboard(int key, bool pressed);

//   // 工具
//   void setScreenSize(float width, float height);
//   UI2DRenderer *getRenderer() { return m_renderer.get(); }
// };
