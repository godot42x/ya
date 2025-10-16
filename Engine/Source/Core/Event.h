#pragma once
#include "Core/Base.h"

#include "SDL3/SDL_events.h"
#include "SDL3/SDL_oldnames.h"
#include "reflect.cc/enum"

#include "KeyCode.h"

namespace ya
{


struct EventProcessState
{
    enum EResult
    {
        Handled = 0,
        Continue,
        ENUM_MAX
    };

    EResult result = Handled;

    EventProcessState(EResult result) : result(result) {}

    operator bool() const
    {
        return result == Handled;
    }

    bool operator==(EResult rhs) const
    {
        return result == rhs;
    }

}; // namespace EventProcessState



namespace EEvent
{
enum T
{
    None = 0,

    WindowClose,
    WindowResize,
    WindowFocus,
    WindowFocusLost,
    WindowMoved,

    AppTick,
    AppUpdate,
    AppRender,

    KeyPressed,
    KeyReleased,
    KeyTyped,

    MouseMoved,
    MouseScrolled,
    MouseButtonPressed,
    MouseButtonReleased,

    EventTypeCount,

    ENUM_MAX
};
GENERATED_ENUM_MISC(T);
} // namespace EEvent


namespace EEventCategory
{
enum T : uint32_t
{
    None        = 0,
    Application = 0x01,
    Window      = 0x02,
    Input       = 0x10,
    Keyboard    = 0x20,
    Mouse       = 0x40,
    MouseButton = 0x80
};
}


#define EVENT_CLASS_TYPE(type)                                                    \
    static EEvent::T    getStaticType() { return EEvent::type; }                  \
    virtual EEvent::T   getEventType() const override { return getStaticType(); } \
    virtual const char *getName() const override { return #type; }

#define EVENT_CLASS_CATEGORY(category) \
    virtual uint32_t getCategory() const override { return category; }


class Event
{
  public:
    virtual ~Event() = default;

    [[nodiscard]] virtual EEvent::T   getEventType() const = 0;
    [[nodiscard]] virtual const char *getName() const      = 0;
    [[nodiscard]] virtual uint32_t    getCategory() const  = 0;
    [[nodiscard]] virtual std::string toString() const { return getName(); }

    [[nodiscard]] inline bool isInCategory(EEventCategory::T category) const
    {
        return getCategory() & category;
    }
};

// class EventDispatcher
// {
//     const Event &_event;
//     bool         bHandled = false;

//   public:

//     template <class T>
//     using event_func_t = std::function<bool(T &)>;

//   public:

//     EventDispatcher(const Event &ev) : _event(ev) {}

//     template <class T>
//     bool dispatch(event_func_t<T> func)
//     {
//         if (_event.getEventType() == T::getStaticType())
//         {
//             bHandled = func(std ::ref(*(T *)&_event));
//             return bHandled;
//         }
//         return false;
//     }

//     template <typename EventType, typename Owner, typename Fn>
//     bool dispatch(Owner *instance, Fn func)
//     {
//         if (_event.getEventType() == EventType::getStaticType())
//         {
//             bHandled = (instance->*func)(std::ref(*(EventType *)&_event));
//             return bHandled;
//         }
//         return false;
//     }
// };

// MARK: ApplicationEvent


struct ENGINE_API WindowEvent : public Event
{
    uint32_t _windowID;

    WindowEvent(uint32_t windowID) : _windowID(windowID) {}

    [[nodiscard]] uint32_t getWindowID() const { return _windowID; }
    std::string            toString() const override { return std::format("WindowEvent: {}", _windowID); }

  public:
    EVENT_CLASS_CATEGORY(EEventCategory::Application)
};

class ENGINE_API WindowResizeEvent : public WindowEvent
{
  private:
    int32_t _h, _w;

  public:
    WindowResizeEvent(uint32_t windowID, int32_t w, int32_t h) : WindowEvent(windowID), _h(h), _w(w) {}


    [[nodiscard]] uint32_t    GetWidth() const { return _w; }
    [[nodiscard]] uint32_t    GetHeight() const { return _h; }
    [[nodiscard]] std::string toString() const override
    {
        return std::format("{} |WindowResizeEvent: {}, {}", WindowEvent::toString(), _w, _h);
    }


    EVENT_CLASS_CATEGORY(EEventCategory::Application)
    EVENT_CLASS_TYPE(WindowResize)
};



struct ENGINE_API WindowCloseEvent : public WindowEvent
{

    WindowCloseEvent(uint32_t windowID) : WindowEvent(windowID) {}

  public:
    EVENT_CLASS_CATEGORY(EEventCategory::Application)
    EVENT_CLASS_TYPE(WindowClose)
};

struct WindowFocusEvent : public WindowEvent
{


    WindowFocusEvent(uint32_t windowID) : WindowEvent(windowID) {}

    std::string toString() const override
    {
        return std::format("{} |WindowFocusEvent", WindowEvent::toString());
    }

  public:
    EVENT_CLASS_CATEGORY(EEventCategory::Application)
    EVENT_CLASS_TYPE(WindowFocus)
};

struct WindowFocusLostEvent : public WindowEvent
{

    WindowFocusLostEvent(uint32_t windowID) : WindowEvent(windowID) {}

    std::string toString() const override
    {
        return std::format("{} |WindowFocusLostEvent", WindowEvent::toString());
    }

  public:
    EVENT_CLASS_CATEGORY(EEventCategory::Application)
    EVENT_CLASS_TYPE(WindowFocusLost)
};

struct WindowMovedEvent : public WindowEvent
{
    uint32_t _x, _y;

  public:
    EVENT_CLASS_CATEGORY(EEventCategory::Application)
    EVENT_CLASS_TYPE(WindowMoved)

    uint32_t getX() const { return _x; }
    uint32_t getY() const { return _y; }
};



// MARK: KeyEvent
struct ENGINE_API KeyEvent : public Event
{
    uint32_t _mod;


    [[nodiscard]] bool isCtrlPressed() const { return _mod & EKeyMod::LCtrl || _mod & EKeyMod::RCtrl; }
    [[nodiscard]] bool isShiftPressed() const { return _mod & EKeyMod::LShift || _mod & EKeyMod::RShift; }
    [[nodiscard]] bool isAltPressed() const { return _mod & EKeyMod::LAlt || _mod & EKeyMod::RAlt; }
#if defined(__APPLE__)
    bool isMetaPressed() const { return _mod & EKeyMod::LMeta || _mod & EKeyMod::RMeta; }
#endif


    EVENT_CLASS_CATEGORY(EEventCategory::Keyboard | EEventCategory::Input);
};



struct ENGINE_API KeyPressedEvent : public KeyEvent
{
    EVENT_CLASS_CATEGORY(EEventCategory::Keyboard | EEventCategory::Input);
    EVENT_CLASS_TYPE(KeyPressed)

    enum EKey::T _keyCode;
    bool         bRepeat = false; // 标识是否为重复按键事件

    [[nodiscard]] EKey::T getKeyCode() const { return _keyCode; }
    [[nodiscard]] bool    isRepeat() const { return bRepeat; }

    [[nodiscard]] std::string toString() const override
    {
        return std::format("KeyPressedEvent: {} (repeat: {})", EKey::toString(_keyCode), bRepeat);
    }
};

class ENGINE_API KeyReleasedEvent : public KeyEvent
{
  public:


    EVENT_CLASS_TYPE(KeyReleased)
    enum EKey::T              _keyCode;
    [[nodiscard]] EKey::T     getKeyCode() const { return _keyCode; }
    [[nodiscard]] std::string toString() const override { return std::format("KeyReleasedEvent: {} ", EKey::toString(_keyCode)); }

  protected:
};



// MARK: MouseEvent

class ENGINE_API MouseMoveEvent : public Event
{
  private:
    float _mouseX, _mouseY;

  public:
    MouseMoveEvent(float x, float y) : _mouseX(x), _mouseY(y) {}

    inline float getX() const { return _mouseX; }
    inline float getY() const { return _mouseY; }

    [[nodiscard]] std::string toString() const override { return std::format("MouseMovedEvent: {}, {} ", _mouseX, _mouseY); }

    EVENT_CLASS_TYPE(MouseMoved)
    EVENT_CLASS_CATEGORY(EEventCategory::Mouse | EEventCategory::Input);
};

struct ENGINE_API MouseScrolledEvent : public Event
{
    float _offsetX, _offsetY;

  public:
    MouseScrolledEvent() = default;
    MouseScrolledEvent(float x, float y) : _offsetX(x), _offsetY(y) {}

    inline float getOffsetX() { return _offsetX; }
    inline float getOffsetY() { return _offsetY; }

    [[nodiscard]] std::string toString() const override { return std::format("MouseScrolledEvent: {}, {} ", _offsetX, _offsetY); }

    EVENT_CLASS_TYPE(MouseScrolled)
    EVENT_CLASS_CATEGORY(EEventCategory::Mouse | EEventCategory::Input);
};



class ENGINE_API MouseButtonEvent : public Event
{
  public:
    inline int GetMouseButton() const { return m_Button; }


    EVENT_CLASS_CATEGORY(EEventCategory::MouseButton | EEventCategory::Input)

  protected:
    explicit MouseButtonEvent(int button) : m_Button(button) {};
    int m_Button;
};

class ENGINE_API MouseButtonPressedEvent : public MouseButtonEvent
{
  public:
    explicit MouseButtonPressedEvent(int keycode) : MouseButtonEvent(keycode) {};

    [[nodiscard]] std::string toString() const override { return std::format("MousePressedEvent: {} ", m_Button); }

    EVENT_CLASS_TYPE(MouseButtonPressed)
};


class ENGINE_API MouseButtonReleasedEvent : public MouseButtonEvent
{
  public:
    explicit MouseButtonReleasedEvent(int keycode) : MouseButtonEvent(keycode) {};

    [[nodiscard]] std::string toString() const override { return std::format("MouseReleasedEvent: {} ", m_Button); }

    EVENT_CLASS_TYPE(MouseButtonReleased)
};

} // namespace ya

inline std::ostream &operator<<(std::ostream &os, const ya::Event &ev) { return os << ev.toString(); }