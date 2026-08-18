#pragma once
#include "Core/Base.h"

#include "reflects-core/enum.h"

#include "KeyCode.h"

#include <chrono>

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
    WindowRestore,
    WindowMinimize,
    WindowFocus,
    WindowFocusLost,
    WindowMoved,

    AppTick,
    AppUpdate,
    AppRender,
    AppQuit,

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

inline const std::string &toString(T value)
{
    auto *enumInfo = EnumRegistry::instance().getEnum(::ya::type_index_v<T>);
    YA_CORE_ASSERT(enumInfo != nullptr, "EEvent enum is not registered");
    return enumInfo->valueToName.at(static_cast<int64_t>(value));
}
} // namespace EEvent


namespace EEventCategory
{
enum T : uint32_t
{
    None        = 0,
    Application = 0x01,
    Window      = 0x02,
    Input       = 0x04,
    Keyboard    = 0x08,
    Mouse       = 0x10,
    MouseButton = 0x20
};
}


#define EVENT_CLASS_TYPE(type)                                                    \
    static EEvent::T    getStaticType() { return EEvent::type; }                  \
    virtual EEvent::T   getEventType() const override { return getStaticType(); } \
    virtual const char *getName() const override { return #type; }

#define EVENT_CLASS_CATEGORY(category) \
    virtual uint32_t getCategory() const override { return category; }


class YA_CORE_API Event
{
  public:
    /// Base constructor stamps the event with the current monotonic time so
    /// double-click / long-press style logic needs no external clock. The
    /// scenario driver overrides it with simulated step time.
    Event() : _timestampMs(currentTimestampMs()) {}
    virtual ~Event() = default;

    [[nodiscard]] virtual EEvent::T   getEventType() const = 0;
    [[nodiscard]] virtual const char *getName() const      = 0;
    [[nodiscard]] virtual uint32_t    getCategory() const  = 0;
    [[nodiscard]] virtual std::string toString() const { return getName(); }

    [[nodiscard]] inline bool isInCategory(EEventCategory::T category) const
    {
        return getCategory() & category;
    }

    /// Monotonic timestamp in milliseconds (guardrail G3).
    [[nodiscard]] uint64_t getTimestampMs() const { return _timestampMs; }
    void setTimestampMs(uint64_t value) { _timestampMs = value; }

  private:
    static uint64_t currentTimestampMs();
    uint64_t _timestampMs = 0;
};

/// Monotonic millisecond clock (header-only; reading the clock needs no
/// shared state, so an inline definition is safe across DLLs).
inline uint64_t Event::currentTimestampMs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

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

struct YA_CORE_API AppEvent : public Event
{
  public:
    EVENT_CLASS_CATEGORY(EEventCategory::Application)
};

struct YA_CORE_API AppQuitEvent : public AppEvent
{
  public:
    EVENT_CLASS_CATEGORY(EEventCategory::Application)
    EVENT_CLASS_TYPE(AppQuit)
};


struct YA_CORE_API WindowEvent : public Event
{
    uint32_t _windowID;

    WindowEvent(uint32_t windowID) : _windowID(windowID) {}

    [[nodiscard]] uint32_t getWindowID() const { return _windowID; }
    std::string            toString() const override { return std::format("WindowEvent: {}", _windowID); }

  public:
    EVENT_CLASS_CATEGORY(EEventCategory::Application)
};

class YA_CORE_API WindowResizeEvent : public WindowEvent
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



struct YA_CORE_API WindowCloseEvent : public WindowEvent
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

struct WindowRestoreEvent : public WindowEvent
{

    WindowRestoreEvent(uint32_t windowID) : WindowEvent(windowID) {}

    std::string toString() const override
    {
        return std::format("{} |WindowRestoreEvent", WindowEvent::toString());
    }

  public:
    EVENT_CLASS_CATEGORY(EEventCategory::Application)
    EVENT_CLASS_TYPE(WindowRestore)
};

struct WindowMinimizeEvent : public WindowEvent
{

    WindowMinimizeEvent(uint32_t windowID) : WindowEvent(windowID) {}

    std::string toString() const override
    {
        return std::format("{} |WindowMinimizeEvent", WindowEvent::toString());
    }

  public:
    EVENT_CLASS_CATEGORY(EEventCategory::Application)
    EVENT_CLASS_TYPE(WindowMinimize)
};



// MARK: KeyEvent
struct YA_CORE_API KeyEvent : public Event
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



struct YA_CORE_API KeyPressedEvent : public KeyEvent
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

class YA_CORE_API KeyReleasedEvent : public KeyEvent
{
  public:


    EVENT_CLASS_TYPE(KeyReleased)
    enum EKey::T              _keyCode;
    [[nodiscard]] EKey::T     getKeyCode() const { return _keyCode; }
    [[nodiscard]] std::string toString() const override { return std::format("KeyReleasedEvent: {} ", EKey::toString(_keyCode)); }

  protected:
};

class YA_CORE_API KeyTypedEvent : public KeyEvent
{
  private:
    std::string _text;

  public:
    KeyTypedEvent() = default;
    explicit KeyTypedEvent(std::string text)
        : _text(std::move(text))
    {
    }

    EVENT_CLASS_TYPE(KeyTyped)
    [[nodiscard]] const std::string& getText() const { return _text; }
    [[nodiscard]] std::string        toString() const override { return std::format("KeyTypedEvent: {}", _text); }
};



// MARK: MouseEvent

class YA_CORE_API MouseMoveEvent : public Event
{
  private:
    float _mouseX, _mouseY;
    float _mouseDeltaX = 0.0f, _mouseDeltaY = 0.0f;

  public:
    MouseMoveEvent(float x, float y)
        : _mouseX(x)
        , _mouseY(y)
    {
    }

    MouseMoveEvent(float x, float y, float deltaX, float deltaY)
        : _mouseX(x)
        , _mouseY(y)
        , _mouseDeltaX(deltaX)
        , _mouseDeltaY(deltaY)
    {
    }

    inline float getX() const { return _mouseX; }
    inline float getY() const { return _mouseY; }
    inline float getDeltaX() const { return _mouseDeltaX; }
    inline float getDeltaY() const { return _mouseDeltaY; }

    [[nodiscard]] std::string toString() const override { return std::format("MouseMovedEvent: {}, {} ", _mouseX, _mouseY); }

    EVENT_CLASS_TYPE(MouseMoved)
    EVENT_CLASS_CATEGORY(EEventCategory::Mouse | EEventCategory::Input);
};

struct YA_CORE_API MouseScrolledEvent : public Event
{
    float _offsetX, _offsetY;

  public:
    MouseScrolledEvent() = default;
    MouseScrolledEvent(float x, float y) : _offsetX(x), _offsetY(y) {}

    inline float getOffsetX() const { return _offsetX; }
    inline float getOffsetY() const { return _offsetY; }

    [[nodiscard]] std::string toString() const override { return std::format("MouseScrolledEvent: {}, {} ", _offsetX, _offsetY); }

    EVENT_CLASS_TYPE(MouseScrolled)
    EVENT_CLASS_CATEGORY(EEventCategory::Mouse | EEventCategory::Input);
};



class YA_CORE_API MouseButtonEvent : public Event
{
  public:
    [[nodiscard]] inline EMouse::T GetMouseButton() const { return m_Button; }

    EVENT_CLASS_CATEGORY(EEventCategory::MouseButton | EEventCategory::Input)

  protected:
    explicit MouseButtonEvent(EMouse::T button) : m_Button(button) {};
    EMouse::T m_Button;
};

class YA_CORE_API MouseButtonPressedEvent : public MouseButtonEvent
{
  public:
    explicit MouseButtonPressedEvent(EMouse::T button) : MouseButtonEvent(button) {};

    [[nodiscard]] std::string toString() const override { return std::format("MousePressedEvent: {} ", static_cast<int>(m_Button)); }

    EVENT_CLASS_TYPE(MouseButtonPressed)
};


class YA_CORE_API MouseButtonReleasedEvent : public MouseButtonEvent
{
  public:
    explicit MouseButtonReleasedEvent(EMouse::T button) : MouseButtonEvent(button) {};

    [[nodiscard]] std::string toString() const override { return std::format("MouseReleasedEvent: {} ", static_cast<int>(m_Button)); }

    EVENT_CLASS_TYPE(MouseButtonReleased)
};

} // namespace ya

inline std::ostream &operator<<(std::ostream &os, const ya::Event &ev) { return os << ev.toString(); }

YA_REFLECT_ENUM_BEGIN(ya::EEvent::T)
YA_REFLECT_ENUM_VALUE(None)
YA_REFLECT_ENUM_VALUE(WindowClose)
YA_REFLECT_ENUM_VALUE(WindowResize)
YA_REFLECT_ENUM_VALUE(WindowRestore)
YA_REFLECT_ENUM_VALUE(WindowMinimize)
YA_REFLECT_ENUM_VALUE(WindowFocus)
YA_REFLECT_ENUM_VALUE(WindowFocusLost)
YA_REFLECT_ENUM_VALUE(WindowMoved)
YA_REFLECT_ENUM_VALUE(AppTick)
YA_REFLECT_ENUM_VALUE(AppUpdate)
YA_REFLECT_ENUM_VALUE(AppRender)
YA_REFLECT_ENUM_VALUE(AppQuit)
YA_REFLECT_ENUM_VALUE(KeyPressed)
YA_REFLECT_ENUM_VALUE(KeyReleased)
YA_REFLECT_ENUM_VALUE(KeyTyped)
YA_REFLECT_ENUM_VALUE(MouseMoved)
YA_REFLECT_ENUM_VALUE(MouseScrolled)
YA_REFLECT_ENUM_VALUE(MouseButtonPressed)
YA_REFLECT_ENUM_VALUE(MouseButtonReleased)
YA_REFLECT_ENUM_VALUE(EventTypeCount)
YA_REFLECT_ENUM_END()
