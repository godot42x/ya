#pragma once

// ============================================================================
// IGameUIBehaviorRuntime - what turns authored widget behaviours into running
// ones for a GameUIHost (game-ui-script-framework S3).
//
// Activation (IUIBehaviorActivator) happens at mount. Anything activated then
// starts in the next UILogic step, not at mount: mounting can run outside the
// frame (scene load), and a batch that starts together can see each other.
// The host calls update() once per UILogic step, before its timers and before
// the tree tick.
// ============================================================================

#include "GUI/Widgets/UIBehaviorSpec.h"

namespace ya
{

struct IGameUIBehaviorRuntime : IUIBehaviorActivator
{
    /// Start what was activated since the last step; report visibility changes.
    virtual void update() = 0;
};

} // namespace ya
