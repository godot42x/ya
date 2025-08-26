#pragma once

#include "SDL3/SDL_mouse.h"
#include "SDL3/sdl_keycode.h"

namespace ya
{


namespace EKeyCode
{
enum T
{
    A = SDLK_A,
    B = SDLK_B,
    C = SDLK_C,
    D = SDLK_D,
    E = SDLK_E,
    F = SDLK_F,
    G = SDLK_G,
    H = SDLK_H,
    I = SDLK_I,
    J = SDLK_J,
    K = SDLK_K,
    L = SDLK_L,
    M = SDLK_M,
    N = SDLK_N,
    O = SDLK_O,
    P = SDLK_P,
    Q = SDLK_Q,
    R = SDLK_R,
    S = SDLK_S,
    T = SDLK_T,
    U = SDLK_U,
    V = SDLK_V,
    W = SDLK_W,
    X = SDLK_X,
    Y = SDLK_Y,
    Z = SDLK_Z,

    _0 = SDLK_0,
    _1 = SDLK_1,
    _2 = SDLK_2,
    _3 = SDLK_3,
    _4 = SDLK_4,
    _5 = SDLK_5,
    _6 = SDLK_6,
    _7 = SDLK_7,
    _8 = SDLK_8,
    _9 = SDLK_9,

    Space     = SDLK_SPACE,
    Enter     = SDLK_RETURN,
    Escape    = SDLK_ESCAPE,
    Backspace = SDLK_BACKSPACE,
    Tab       = SDLK_TAB,
    Shift     = SDLK_LSHIFT,
    Ctrl      = SDLK_LCTRL,
    Alt       = SDLK_LALT,
    CapsLock  = SDLK_CAPSLOCK,
    F1        = SDLK_F1,
    F2        = SDLK_F2,
    F3        = SDLK_F3,
    F4        = SDLK_F4,
    F5        = SDLK_F5,
    F6        = SDLK_F6,
    F7        = SDLK_F7,
    F8        = SDLK_F8,
    F9        = SDLK_F9,
    F10       = SDLK_F10,
    F11       = SDLK_F11,
    F12       = SDLK_F12,

    Up    = SDLK_UP,
    Down  = SDLK_DOWN,
    Left  = SDLK_LEFT,
    Right = SDLK_RIGHT,

    Insert   = SDLK_INSERT,
    Delete   = SDLK_DELETE,
    Home     = SDLK_HOME,
    End      = SDLK_END,
    Pageup   = SDLK_PAGEUP,
    PagedowN = SDLK_PAGEDOWN,

    LCtrl  = SDLK_LCTRL,
    RCtrl  = SDLK_RCTRL,
    LAlt   = SDLK_LALT,
    RAlt   = SDLK_RALT,
    LShift = SDLK_LSHIFT,
    RShift = SDLK_RSHIFT,
    LMeta  = SDLK_LGUI, // Left Command/Windows key
    RMeta  = SDLK_RGUI, // Right Command/Windows key

};


namespace EKeyMod
{

enum T
{
    LShift = SDL_KMOD_LSHIFT,
    RSift  = SDL_KMOD_RSHIFT,
    Level5 = SDL_KMOD_LEVEL5,
    LCtrl  = SDL_KMOD_LCTRL,
    RCtrl  = SDL_KMOD_RCTRL,
    LAlt   = SDL_KMOD_LALT,
    RAlt   = SDL_KMOD_RALT,
    LMeta  = SDL_KMOD_LGUI,
    RMeta  = SDL_KMOD_RGUI,
    Num    = SDL_KMOD_NUM,
    Caps   = SDL_KMOD_CAPS,
    Mode   = SDL_KMOD_MODE,
    Scroll = SDL_KMOD_SCROLL,

    Ctrl  = LCtrl | RCtrl,
    Shift = LShift | RSift,
    Alt   = LAlt | RAlt,

};

};


} // namespace EKeyCode


namespace EMouse
{
enum T
{
    Left   = SDL_BUTTON_LEFT,
    Middle = SDL_BUTTON_MIDDLE,
    Right  = SDL_BUTTON_RIGHT,
    X1     = SDL_BUTTON_X1,
    X2     = SDL_BUTTON_X2,

};

};
} // namespace ya