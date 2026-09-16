#include "Render3D/Common/RenderRecordingContext.h"
#include "Render3D/Common/RenderSubmissionTable.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <memory>

namespace ya
{
namespace
{

ICommandBuffer* dummyCmdBuf(uintptr_t token)
{
    return reinterpret_cast<ICommandBuffer*>(token);
}

RenderSubmissionContext makeContext(uint32_t flightIndex, uint64_t frameToken, uintptr_t cmdToken = 1)
{
    return RenderSubmissionContext{
        .frameToken  = frameToken,
        .flightIndex = flightIndex,
        .cmdBuf      = dummyCmdBuf(cmdToken),
    };
}

} // namespace

TEST(RenderSubmissionTableTest, KeepalivesSurviveRecordingComplete)
{
    RenderSubmissionTable table;
    auto                  owner = std::make_shared<int>(7);
    std::weak_ptr<int>    weak  = owner;

    ASSERT_TRUE(table.begin(0, 11u, makeContext(0, 11u)));
    ASSERT_TRUE(table.retain(0, owner));
    owner.reset();

    ASSERT_TRUE(table.markRecordingComplete(0));
    const RenderSubmissionRecord* live = table.get(0);
    ASSERT_NE(live, nullptr);
    EXPECT_TRUE(live->recordingComplete);
    EXPECT_EQ(live->frameToken, 11u);
    EXPECT_FALSE(weak.expired());
    EXPECT_EQ(live->keepalives.size(), 1u);
}

TEST(RenderSubmissionTableTest, NewTokenOnSameFlightDropsPreviousKeepalives)
{
    RenderSubmissionTable table;
    auto                  first = std::make_shared<int>(1);
    auto                  second = std::make_shared<int>(2);
    std::weak_ptr<int>    firstWeak = first;

    ASSERT_TRUE(table.begin(0, 1u, makeContext(0, 1u)));
    ASSERT_TRUE(table.retain(0, first));
    first.reset();
    ASSERT_TRUE(table.markRecordingComplete(0));
    EXPECT_FALSE(firstWeak.expired());

    ASSERT_TRUE(table.begin(0, 2u, makeContext(0, 2u)));
    EXPECT_TRUE(firstWeak.expired());
    ASSERT_TRUE(table.retain(0, second));

    const RenderSubmissionRecord* live = table.get(0);
    ASSERT_NE(live, nullptr);
    EXPECT_FALSE(live->recordingComplete);
    EXPECT_EQ(live->frameToken, 2u);
    EXPECT_EQ(live->keepalives.size(), 1u);
    EXPECT_EQ(live->keepalives.front().as<int>(), second.get());
}

TEST(RenderSubmissionTableTest, OtherFlightBeginDoesNotDropKeepalives)
{
    RenderSubmissionTable table;
    auto                  flight0 = std::make_shared<int>(10);
    auto                  flight1 = std::make_shared<int>(11);
    std::weak_ptr<int>    weak0   = flight0;

    ASSERT_TRUE(table.begin(0, 5u, makeContext(0, 5u)));
    ASSERT_TRUE(table.retain(0, flight0));
    flight0.reset();
    ASSERT_TRUE(table.markRecordingComplete(0));

    ASSERT_TRUE(table.begin(1, 5u, makeContext(1, 5u)));
    ASSERT_TRUE(table.retain(1, flight1));
    ASSERT_TRUE(table.markRecordingComplete(1));

    EXPECT_FALSE(weak0.expired());
    ASSERT_NE(table.get(0), nullptr);
    ASSERT_NE(table.get(1), nullptr);
    EXPECT_TRUE(table.get(0)->recordingComplete);
    EXPECT_EQ(table.get(0)->keepalives.front().as<int>(), weak0.lock().get());
    EXPECT_EQ(table.get(1)->keepalives.front().as<int>(), flight1.get());
}

TEST(RenderSubmissionTableTest, SameTokenBeginKeepsKeepalives)
{
    RenderSubmissionTable table;
    auto                  owner = std::make_shared<int>(3);

    ASSERT_TRUE(table.begin(0, 9u, makeContext(0, 9u, 1)));
    ASSERT_TRUE(table.retain(0, owner));
    ASSERT_TRUE(table.begin(0, 9u, makeContext(0, 9u, 1)));

    const RenderSubmissionRecord* live = table.get(0);
    ASSERT_NE(live, nullptr);
    EXPECT_EQ(live->keepalives.size(), 1u);
    EXPECT_FALSE(live->recordingComplete);
}

TEST(RenderSubmissionTableTest, BeginRequiresMatchingFlightAndCommandBuffer)
{
    RenderSubmissionTable table;
    EXPECT_FALSE(table.begin(0, 1u, makeContext(1, 1u)));
    EXPECT_FALSE(table.begin(MAX_FLIGHTS_IN_FLIGHT, 1u, makeContext(0, 1u)));
    EXPECT_FALSE(table.retain(0, std::make_shared<int>(1)));
    EXPECT_EQ(table.get(0), nullptr);
    EXPECT_EQ(table.context(0), nullptr);
    EXPECT_FALSE(table.markRecordingComplete(0));
}

} // namespace ya
