#include "Core/Profiling/RenderCounters.h"

namespace ya
{

namespace
{

struct CounterSet
{
    std::atomic<uint32_t> draw{0};
    std::atomic<uint32_t> drawIndexed{0};
    std::atomic<uint32_t> indirectDraw{0};
    std::atomic<uint32_t> dispatch{0};
    std::atomic<uint32_t> dispatchIndirect{0};
    std::atomic<uint32_t> pipelineBind{0};
    std::atomic<uint32_t> descriptorBind{0};
    std::atomic<uint32_t> batch2D{0};
    std::atomic<uint32_t> textureTableSwitch2D{0};
};

CounterSet& counters()
{
    static CounterSet set;
    return set;
}

} // namespace

void incrementRenderCounterDraw()
{
    counters().draw.fetch_add(1, std::memory_order_relaxed);
}

void incrementRenderCounterDrawIndexed()
{
    counters().drawIndexed.fetch_add(1, std::memory_order_relaxed);
}

void incrementRenderCounterIndirectDraw(uint32_t count)
{
    counters().indirectDraw.fetch_add(count, std::memory_order_relaxed);
}

void incrementRenderCounterDispatch()
{
    counters().dispatch.fetch_add(1, std::memory_order_relaxed);
}

void incrementRenderCounterDispatchIndirect()
{
    counters().dispatchIndirect.fetch_add(1, std::memory_order_relaxed);
}

void incrementRenderCounterPipelineBind()
{
    counters().pipelineBind.fetch_add(1, std::memory_order_relaxed);
}

void incrementRenderCounterDescriptorBind()
{
    counters().descriptorBind.fetch_add(1, std::memory_order_relaxed);
}

void incrementRenderCounterBatch2D()
{
    counters().batch2D.fetch_add(1, std::memory_order_relaxed);
}

void incrementRenderCounterTextureTableSwitch2D()
{
    counters().textureTableSwitch2D.fetch_add(1, std::memory_order_relaxed);
}

RenderCounters renderCountersSnapshot()
{
    CounterSet& set = counters();
    RenderCounters snapshot{
        .drawCount                 = set.draw.load(std::memory_order_relaxed),
        .drawIndexedCount          = set.drawIndexed.load(std::memory_order_relaxed),
        .indirectDrawCount         = set.indirectDraw.load(std::memory_order_relaxed),
        .dispatchCount             = set.dispatch.load(std::memory_order_relaxed),
        .dispatchIndirectCount     = set.dispatchIndirect.load(std::memory_order_relaxed),
        .pipelineBindCount         = set.pipelineBind.load(std::memory_order_relaxed),
        .descriptorBindCount       = set.descriptorBind.load(std::memory_order_relaxed),
        .batch2DCount              = set.batch2D.load(std::memory_order_relaxed),
        .textureTableSwitch2DCount = set.textureTableSwitch2D.load(std::memory_order_relaxed),
    };
    return snapshot;
}

void resetRenderCounters()
{
    CounterSet& set = counters();
    set.draw.store(0, std::memory_order_relaxed);
    set.drawIndexed.store(0, std::memory_order_relaxed);
    set.indirectDraw.store(0, std::memory_order_relaxed);
    set.dispatch.store(0, std::memory_order_relaxed);
    set.dispatchIndirect.store(0, std::memory_order_relaxed);
    set.pipelineBind.store(0, std::memory_order_relaxed);
    set.descriptorBind.store(0, std::memory_order_relaxed);
    set.batch2D.store(0, std::memory_order_relaxed);
    set.textureTableSwitch2D.store(0, std::memory_order_relaxed);
}

} // namespace ya
