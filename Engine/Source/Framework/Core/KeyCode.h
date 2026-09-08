#pragma once

#include "Core/Base.h"
#include "log.cc/helper.h"
#include "reflects-core/enum.h"

#include <cstdint>

namespace ya
{

namespace EKeyMod
{

enum T
{
    LShift = 0x0001,
    RShift = 0x0002,
    Level5 = 0x0004,
    LCtrl  = 0x0040,
    RCtrl  = 0x0080,
    LAlt   = 0x0100,
    RAlt   = 0x0200,
    LMeta  = 0x0400,
    RMeta  = 0x0800,
    Num    = 0x1000,
    Caps   = 0x2000,
    Mode   = 0x4000,
    Scroll = 0x8000,

    Ctrl  = LCtrl | RCtrl,
    Shift = LShift | RShift,
    Alt   = LAlt | RAlt,
    Gui   = LMeta | RMeta,
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
        CASE_ENUM_TO_STR(EKeyMod::Gui);
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

    K_A = 0x00000061,
    K_B = 0x00000062,
    K_C = 0x00000063,
    K_D = 0x00000064,
    K_E = 0x00000065,
    K_F = 0x00000066,
    K_G = 0x00000067,
    K_H = 0x00000068,
    K_I = 0x00000069,
    K_J = 0x0000006a,
    K_K = 0x0000006b,
    K_L = 0x0000006c,
    K_M = 0x0000006d,
    K_N = 0x0000006e,
    K_O = 0x0000006f,
    K_P = 0x00000070,
    K_Q = 0x00000071,
    K_R = 0x00000072,
    K_S = 0x00000073,
    K_T = 0x00000074,
    K_U = 0x00000075,
    K_V = 0x00000076,
    K_W = 0x00000077,
    K_X = 0x00000078,
    K_Y = 0x00000079,
    K_Z = 0x0000007a,

    K_0 = 0x00000030,
    K_1 = 0x00000031,
    K_2 = 0x00000032,
    K_3 = 0x00000033,
    K_4 = 0x00000034,
    K_5 = 0x00000035,
    K_6 = 0x00000036,
    K_7 = 0x00000037,
    K_8 = 0x00000038,
    K_9 = 0x00000039,

    K_GRAVE = 0x00000060,

    Space     = 0x00000020,
    Enter     = 0x0000000d,
    Escape    = 0x0000001b,
    Backspace = 0x00000008,
    Tab       = 0x00000009,
    LShift    = 0x400000e1,
    LCtrl     = 0x400000e0,
    LAlt      = 0x400000e2,
    CapsLock  = 0x40000039,
    F1        = 0x4000003a,
    F2        = 0x4000003b,
    F3        = 0x4000003c,
    F4        = 0x4000003d,
    F5        = 0x4000003e,
    F6        = 0x4000003f,
    F7        = 0x40000040,
    F8        = 0x40000041,
    F9        = 0x40000042,
    F10       = 0x40000043,
    F11       = 0x40000044,
    F12       = 0x40000045,

    Up    = 0x40000052,
    Down  = 0x40000051,
    Left  = 0x40000050,
    Right = 0x4000004f,

    Insert   = 0x40000049,
    Delete   = 0x0000007f,
    Home     = 0x4000004a,
    End      = 0x4000004d,
    Pageup   = 0x4000004b,
    PagedowN = 0x4000004e,

    RCtrl  = 0x400000e4,
    RAlt   = 0x400000e6,
    RShift = 0x400000e5,
    LMeta  = 0x400000e3,
    RMeta  = 0x400000e7,
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
        CASE_ENUM_TO_STR(EKey::K_GRAVE);

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

inline EKey::T fromNativeKeycode(uint32_t keycode)
{
    return static_cast<EKey::T>(keycode);
}

} // namespace EKey

namespace EMouse
{
enum T
{
    Left   = 1,
    Middle = 2,
    Right  = 3,
    X1     = 4,
    X2     = 5,
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

inline EMouse::T fromNativeMouseButton(uint8_t button)
{
    return static_cast<EMouse::T>(button);
}

} // namespace EMouse

} // namespace ya
