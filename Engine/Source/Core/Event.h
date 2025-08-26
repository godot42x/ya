#pragma once
#include "Core/Base.h"

#include "SDL3/SDL_events.h"
#include "SDL3/SDL_oldnames.h"
#include "reflect.cc/enum"

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


namespace ya
{


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
    Mouse       = 0x30,
    MouseButton = 0x40
};
}


#define EVENT_CLASS_TYPE(type)                                                    \
    static EEvent::T    getStaticType() { return EEvent::type; }                  \
    virtual EEvent::T   getEventType() const override { return getStaticType(); } \
    virtual const char *getName() const override { return #type; }

#define EVENT_CLASS_CATEGORY(category) \
    virtual uint32_t getCategory() const override { return category; }

class EventDispatcher;

class Event
{
    friend class EventDispatcher;

  public:
    bool bHandled = false;

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

class EventDispatcher
{
    Event &_event;

  public:

    template <class T>
    using event_func_t = std::function<bool(T &)>;

  public:

    EventDispatcher(Event &ev) : _event(ev) {}

    template <class T>
    bool dispatch(event_func_t<T> func)
    {
        if (_event.getEventType() == T::getStaticType())
        {
            _event.bHandled = func(std ::ref(*(T *)&_event));
            return true;
        }
        return false;
    }

    template <typename EventType, typename Owner, typename Fn>
    bool dispatch(Owner *instance, Fn func)
    {
        if (_event.getEventType() == EventType::getStaticType())
        {
            _event.bHandled = (instance->*func)(std::ref(*(EventType *)&_event));
            return true;
        }
        return false;
    }
};

// MARK: ApplicationEvent


struct ENGINE_API WindowEvent : public Event
{
  public:
    EVENT_CLASS_CATEGORY(EEventCategory::Application)
};

class ENGINE_API WindowResizeEvent : public WindowEvent
{
  private:
    uint32_t _h, _w;

  public:
    WindowResizeEvent(uint32_t w, uint32_t h) : _h(h), _w(w) {}


    [[nodiscard]] uint32_t GetWidth() const { return _w; }
    [[nodiscard]] uint32_t GetHeight() const { return _h; }

    [[nodiscard]] std::string toString() const override { return std::format("WindowResizeEvent: {}, {}", _w, _h); }


    EVENT_CLASS_CATEGORY(EEventCategory::Application)
    EVENT_CLASS_TYPE(WindowResize)
};



struct ENGINE_API WindowCloseEvent : public WindowEvent
{
    uint32_t _windowIndex;

  public:
    EVENT_CLASS_CATEGORY(EEventCategory::Application)
    EVENT_CLASS_TYPE(WindowClose)
};

struct WindowFocusEvent : public WindowEvent
{
    uint32_t _windowIndex;

  public:
    EVENT_CLASS_CATEGORY(EEventCategory::Application)
    EVENT_CLASS_TYPE(WindowFocus)
};

struct WindowFocusLostEvent : public WindowEvent
{
    uint32_t _windowIndex;

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
class ENGINE_API KeyEvent : public Event
{
  public:
    // using key_code_t = SDL_KW;


  protected:
    KeyEvent(int key_code) : _keyCode(key_code) {}
    int _keyCode;

  public:
    inline int GetKeyCode() const { return _keyCode; }

    EVENT_CLASS_CATEGORY(EEventCategory::Keyboard | EEventCategory::Input);
};



class ENGINE_API KeyPressedEvent : public KeyEvent
{
  public:
    KeyPressedEvent(int keycode, bool bRepeat) : KeyEvent(keycode), bRepeat(bRepeat) {}

    EVENT_CLASS_CATEGORY(EEventCategory::Keyboard | EEventCategory::Input);
    EVENT_CLASS_TYPE(KeyPressed)

  protected:
    bool bRepeat;
};

class ENGINE_API KeyReleasedEvent : public KeyEvent
{
  public:
    explicit KeyReleasedEvent(int keycode) : KeyEvent(keycode) {};

    [[nodiscard]] std::string toString() const override { return std::format("KeyReleasedEvent: {} ", _keyCode); }

    EVENT_CLASS_TYPE(KeyReleased)

  protected:
};



class ENGINE_API KeyTypedEvent : public KeyEvent
{
  public:
    explicit KeyTypedEvent(int keycode) : KeyEvent(keycode) {}

    [[nodiscard]] std::string toString() const override { return std::format("KeyTypeEvent: {} ", _keyCode); }

    EVENT_CLASS_TYPE(KeyTyped)
};


// MARK: MouseEvent

class ENGINE_API MouseMoveEvent : public Event
{
  public:
    MouseMoveEvent(float x, float y) : _mouseX(x), _mouseY(y) {}

    inline float GetX() { return _mouseX; }
    inline float GetY() { return _mouseY; }

    [[nodiscard]] std::string toString() const override { return std::format("MouseMovedEvent: {}, {} ", _mouseX, _mouseY); }

    EVENT_CLASS_TYPE(MouseMoved)
    EVENT_CLASS_CATEGORY(EEventCategory::Mouse | EEventCategory::Input);

  private:
    float _mouseX, _mouseY;
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