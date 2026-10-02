#include "App/Control/GuiEventDriver.h"
#include "Core/Input/InputManager.h"
#include "Core/Os/OsEvent.h"

#include <gtest/gtest.h>

namespace ya
{
namespace
{

// Lua names this the other way round: isKeyPressed is the edge (wasKeyPressed),
// isKeyDown is the held state (isKeyPressed).
TEST(InputInjection, PressEdgeIsOneFrameAndHoldLastsUntilRelease)
{
    InputManager input;
    const auto emit = [&](const Event& event) { input.processEvent(event); };

    OsEventPump::drainInjectedKeys([](const Event&) {});
    EXPECT_EQ(keyFromName("Right"), EKey::Right);
    EXPECT_EQ(keyFromName("Space"), EKey::Space);
    EXPECT_EQ(keyFromName("W"), EKey::K_W);
    EXPECT_EQ(keyFromName("no-such-key"), EKey::NONE);

    OsEventPump::emitKey(emit, EKey::Right, true);
    EXPECT_TRUE(input.wasKeyPressed(EKey::Right));
    EXPECT_TRUE(input.isKeyPressed(EKey::Right));

    // End of the press frame: the edge dies, the key stays down.
    input.preUpdate();
    EXPECT_FALSE(input.wasKeyPressed(EKey::Right));
    EXPECT_TRUE(input.isKeyPressed(EKey::Right));

    // hold frames=1 enqueues the release for the next poll.
    OsEventPump::enqueueKey(EKey::Right, false, 0);
    EXPECT_TRUE(input.isKeyPressed(EKey::Right));
    OsEventPump::drainInjectedKeys(emit);
    EXPECT_FALSE(input.isKeyPressed(EKey::Right));
    EXPECT_TRUE(input.wasKeyReleased(EKey::Right));

    // hold frames=2 skips one poll, so the key is still down after the first drain.
    input.preUpdate();
    OsEventPump::emitKey(emit, EKey::Right, true);
    EXPECT_TRUE(input.wasKeyPressed(EKey::Right));
    input.preUpdate();
    OsEventPump::enqueueKey(EKey::Right, false, 1);
    OsEventPump::drainInjectedKeys(emit);
    EXPECT_TRUE(input.isKeyPressed(EKey::Right));
    EXPECT_FALSE(input.wasKeyReleased(EKey::Right));
    input.preUpdate();
    OsEventPump::drainInjectedKeys(emit);
    EXPECT_FALSE(input.isKeyPressed(EKey::Right));
    EXPECT_TRUE(input.wasKeyReleased(EKey::Right));
}

} // namespace
} // namespace ya
