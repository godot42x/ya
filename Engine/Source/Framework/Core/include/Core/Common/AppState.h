#pragma once

// ============================================================================
// Application lifecycle state.
//
// Plain value enum shared by the Host, the editor, gameplay systems and the
// physics debug paths; lives in Core so lower layers never reach Host.
// ============================================================================

namespace ya
{

enum class AppState
{
    Stopped,
    Simulation,
    Runtime
};

} // namespace ya
