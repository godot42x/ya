#pragma once

#include "Core/Api.h"
#include "Core/Input/Cursor.h"

namespace ya
{

/// Process-wide native mouse cursor. SDL is an implementation detail of
/// this type; GUI host and GameRuntime must not call SDL cursor APIs.
struct YA_CORE_API OsCursor
{
    /// Apply a system cursor shape. No-op when the shape is already active.
    static void set(ECursorType type);
    static void show();
    static void hide();
};

} // namespace ya
