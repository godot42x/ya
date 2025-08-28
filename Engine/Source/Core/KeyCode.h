#pragma once

#include "Core/Base.h"
#include "SDL3/SDL_mouse.h"
#include "SDL3/sdl_keycode.h"
#include "reflect.cc/enum"



namespace ya
{


namespace EKeyMod
{

enum T
{
    LShift = SDL_KMOD_LSHIFT,
    RShift = SDL_KMOD_RSHIFT,
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
    Shift = LShift | RShift,
    Alt   = LAlt | RAlt,

};

constexpr const char *toString(enum T v)
{
    switch (v) {
        CASE_ENUM_TO_STR(EKeyMod::LCtrl);
        CASE_ENUM_TO_STR(EKeyMod::RCtrl);
        CASE_ENUM_TO_STR(EKeyMod::LAlt);
        CASE_ENUM_TO_STR(EKeyMod::RAlt);
        CASE_ENUM_TO_STR(EKeyMod::LShift);
        CASE_ENUM_TO_STR(EKeyMod::RShift);
        CASE_ENUM_TO_STR(EKeyMod::LMeta);
        CASE_ENUM_TO_STR(EKeyMod::RMeta);
        CASE_ENUM_TO_STR(EKeyMod::Num);

        CASE_ENUM_TO_STR(EKeyMod::Caps);
        CASE_ENUM_TO_STR(EKeyMod::Mode);
        CASE_ENUM_TO_STR(EKeyMod::Scroll);
        CASE_ENUM_TO_STR(EKeyMod::Ctrl);
        CASE_ENUM_TO_STR(EKeyMod::Shift);
        CASE_ENUM_TO_STR(EKeyMod::Alt);
    default:
        UNREACHABLE();
        return "";
    }
}



}; // namespace EKeyMod



namespace EKey
{
enum T
{
    NONE = -1,


    K_A = SDLK_A,
    K_B = SDLK_B,
    K_C = SDLK_C,
    K_D = SDLK_D,
    K_E = SDLK_E,
    K_F = SDLK_F,
    K_G = SDLK_G,
    K_H = SDLK_H,
    K_I = SDLK_I,
    K_J = SDLK_J,
    K_K = SDLK_K,
    K_L = SDLK_L,
    K_M = SDLK_M,
    K_N = SDLK_N,
    K_O = SDLK_O,
    K_P = SDLK_P,
    K_Q = SDLK_Q,
    K_R = SDLK_R,
    K_S = SDLK_S,
    K_T = SDLK_T,
    K_U = SDLK_U,
    K_V = SDLK_V,
    K_W = SDLK_W,
    K_X = SDLK_X,
    K_Y = SDLK_Y,
    K_Z = SDLK_Z,

    K_0 = SDLK_0,
    K_1 = SDLK_1,
    K_2 = SDLK_2,
    K_3 = SDLK_3,
    K_4 = SDLK_4,
    K_5 = SDLK_5,
    K_6 = SDLK_6,
    K_7 = SDLK_7,
    K_8 = SDLK_8,
    K_9 = SDLK_9,

    Space     = SDLK_SPACE,
    Enter     = SDLK_RETURN,
    Escape    = SDLK_ESCAPE,
    Backspace = SDLK_BACKSPACE,
    Tab       = SDLK_TAB,
    LShift    = SDLK_LSHIFT,
    LCtrl     = SDLK_LCTRL,
    LAlt      = SDLK_LALT,
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

    RCtrl  = SDLK_RCTRL,
    RAlt   = SDLK_RALT,
    RShift = SDLK_RSHIFT,
    LMeta  = SDLK_LGUI, // Left Command/Windows key
    RMeta  = SDLK_RGUI, // Right Command/Windows key

};



constexpr const char *toString(EKey::T v)
{
    switch (v) {
        CASE_ENUM_TO_STR(EKey::K_A);
        CASE_ENUM_TO_STR(EKey::K_B);
        CASE_ENUM_TO_STR(EKey::K_C);
        CASE_ENUM_TO_STR(EKey::K_D);
        CASE_ENUM_TO_STR(EKey::K_E);
        CASE_ENUM_TO_STR(EKey::K_F);
        CASE_ENUM_TO_STR(EKey::K_G);
        CASE_ENUM_TO_STR(EKey::K_H);
        CASE_ENUM_TO_STR(EKey::K_I);
        CASE_ENUM_TO_STR(EKey::K_J);
        CASE_ENUM_TO_STR(EKey::K_K);
        CASE_ENUM_TO_STR(EKey::K_L);
        CASE_ENUM_TO_STR(EKey::K_M);
        CASE_ENUM_TO_STR(EKey::K_N);
        CASE_ENUM_TO_STR(EKey::K_O);
        CASE_ENUM_TO_STR(EKey::K_P);
        CASE_ENUM_TO_STR(EKey::K_Q);
        CASE_ENUM_TO_STR(EKey::K_R);
        CASE_ENUM_TO_STR(EKey::K_S);
        CASE_ENUM_TO_STR(EKey::K_T);
        CASE_ENUM_TO_STR(EKey::K_U);
        CASE_ENUM_TO_STR(EKey::K_V);
        CASE_ENUM_TO_STR(EKey::K_W);
        CASE_ENUM_TO_STR(EKey::K_X);
        CASE_ENUM_TO_STR(EKey::K_Y);
        CASE_ENUM_TO_STR(EKey::K_Z);

        CASE_ENUM_TO_STR(EKey::K_0);
        CASE_ENUM_TO_STR(EKey::K_1);
        CASE_ENUM_TO_STR(EKey::K_2);
        CASE_ENUM_TO_STR(EKey::K_3);
        CASE_ENUM_TO_STR(EKey::K_4);
        CASE_ENUM_TO_STR(EKey::K_5);
        CASE_ENUM_TO_STR(EKey::K_6);
        CASE_ENUM_TO_STR(EKey::K_7);
        CASE_ENUM_TO_STR(EKey::K_8);
        CASE_ENUM_TO_STR(EKey::K_9);

        CASE_ENUM_TO_STR(EKey::Space);
        CASE_ENUM_TO_STR(EKey::Enter);
        CASE_ENUM_TO_STR(EKey::Escape);
        CASE_ENUM_TO_STR(EKey::Backspace);
        CASE_ENUM_TO_STR(EKey::Tab);
        CASE_ENUM_TO_STR(EKey::CapsLock);

        CASE_ENUM_TO_STR(EKey::F1);
        CASE_ENUM_TO_STR(EKey::F2);
        CASE_ENUM_TO_STR(EKey::F3);


        CASE_ENUM_TO_STR(EKey::F4);
        CASE_ENUM_TO_STR(EKey::F5);
        CASE_ENUM_TO_STR(EKey::F6);
        CASE_ENUM_TO_STR(EKey::F7);
        CASE_ENUM_TO_STR(EKey::F8);
        CASE_ENUM_TO_STR(EKey::F9);
        CASE_ENUM_TO_STR(EKey::F10);
        CASE_ENUM_TO_STR(EKey::F11);
        CASE_ENUM_TO_STR(EKey::F12);

        CASE_ENUM_TO_STR(EKey::Up);
        CASE_ENUM_TO_STR(EKey::Down);
        CASE_ENUM_TO_STR(EKey::Left);
        CASE_ENUM_TO_STR(EKey::Right);

        CASE_ENUM_TO_STR(EKey::Insert);
        CASE_ENUM_TO_STR(EKey::Delete);
        CASE_ENUM_TO_STR(EKey::Home);
        CASE_ENUM_TO_STR(EKey::End);
        CASE_ENUM_TO_STR(EKey::Pageup);
        CASE_ENUM_TO_STR(EKey::PagedowN);

        CASE_ENUM_TO_STR(EKey::LCtrl);
        CASE_ENUM_TO_STR(EKey::RCtrl);
        CASE_ENUM_TO_STR(EKey::LAlt);
        CASE_ENUM_TO_STR(EKey::RAlt);
        CASE_ENUM_TO_STR(EKey::LShift);
        CASE_ENUM_TO_STR(EKey::RShift);
        CASE_ENUM_TO_STR(EKey::LMeta);
        CASE_ENUM_TO_STR(EKey::RMeta);

    default:
        UNREACHABLE();
        return "";
    }
}

} // namespace EKey

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
constexpr const char *toString(enum T v)
{
    switch (v) {
        CASE_ENUM_TO_STR(EMouse::Left);
        CASE_ENUM_TO_STR(EMouse::Middle);
        CASE_ENUM_TO_STR(EMouse::Right);
        CASE_ENUM_TO_STR(EMouse::X1);
        CASE_ENUM_TO_STR(EMouse::X2);
    default:
        UNREACHABLE();
        return "";
    }
}

}; // namespace EMouse

} // namespace ya
