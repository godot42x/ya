#pragma once
#include "Core/Base.h"
#include "Render/Core/Buffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Render.h"
#include "Render/RenderDefines.h"
#include <format>
#include <functional>
#include <vector>

namespace ya
{

/**
 * @brief Manages per-material descriptor sets and UBOs with dynamic capacity.
 *
 * Holds N param DSs (each backed by one UBO) + N resource DSs.
 * Grows by 2x when ensureCapacity() is called with more materials than the current cap.
 * UBO-to-paramDS rebinding is performed once on rebuild, not on every dirty flush.
 *
 * @tparam TMaterial  Material type (must expose getIndex(), getParamVersion(), getResourceVersion(), isParamDirty/setParamDirty, isResourceDirty/setResourceDirty)
 * @tparam TParamUBO  UBO struct stored per material
 */
template <typename TMaterial, typename TParamUBO>
struct MaterialDescPool
{
    static constexpr uint32_t MAX_CAP = 2048;

    IRender*                     _render = nullptr;
    stdptr<IDescriptorSetLayout> _paramDSL;
    stdptr<IDescriptorSetLayout> _resourceDSL;
    // Returns pool sizes for N materials (called during rebuild)
    std::function<std::vector<DescriptorPoolSize>(uint32_t)> _poolSizeFn;

    stdptr<IDescriptorPool>          _pool;
    std::vector<stdptr<IBuffer>>     _paramUBOs;
    std::vector<DescriptorSetHandle> _paramDSs;
    std::vector<DescriptorSetHandle> _resourceDSs;
    std::vector<uint64_t>            _uploadedParamVersions;
    std::vector<uint64_t>            _uploadedResourceVersions;
    uint32_t                         _capacity = 0;

    void init(IRender*                                                 render,
              stdptr<IDescriptorSetLayout>                             paramDSL,
              stdptr<IDescriptorSetLayout>                             resourceDSL,
              std::function<std::vector<DescriptorPoolSize>(uint32_t)> poolSizeFn,
              uint32_t                                                 initialCapacity = 16)
    {
        _render      = render;
        _paramDSL    = std::move(paramDSL);
        _resourceDSL = std::move(resourceDSL);
        _poolSizeFn  = std::move(poolSizeFn);
        rebuild(initialCapacity);
    }

    // Returns true if the pool was rebuilt (caller should force-update all materials)
    bool ensureCapacity(uint32_t materialCount)
    {
        if (materialCount <= _capacity)
            return false;

        uint32_t newCap = std::max<uint32_t>(1, _capacity);
        while (newCap < materialCount)
            newCap *= 2;

        YA_CORE_ASSERT(newCap <= MAX_CAP, "Too many materials, exceed max limit ({})", MAX_CAP);
        if (newCap > MAX_CAP)
            return false;

        rebuild(newCap);
        return true;
    }

    DescriptorSetHandle paramDS(uint32_t i) const { return _paramDSs[i]; }
    DescriptorSetHandle resourceDS(uint32_t i) const { return _resourceDSs[i]; }
    IBuffer*            paramUBO(uint32_t i) const { return _paramUBOs[i].get(); }

    /**
     * @brief Flush upload state for one material for this consumer.
     * @param forceUpdate  True when the pool was just rebuilt and all materials need a full update.
     * @param onParam      void(IBuffer* ubo, TMaterial* mat) — write UBO data
     * @param onResource   void(DescriptorSetHandle ds, TMaterial* mat) — write texture DS
     */
    template <typename FOnParam, typename FOnResource>
    void flushDirty(TMaterial* mat, bool forceUpdate, FOnParam onParam, FOnResource onResource)
    {
        uint32_t idx             = static_cast<uint32_t>(mat->getIndex());
        uint64_t paramVersion    = mat->getParamVersion();
        uint64_t resourceVersion = mat->getResourceVersion();

        if (forceUpdate || _uploadedParamVersions[idx] != paramVersion) {
            onParam(_paramUBOs[idx].get(), mat);
            _uploadedParamVersions[idx] = paramVersion;
            mat->setParamDirty(false);
        }
        if (forceUpdate || _uploadedResourceVersions[idx] != resourceVersion) {
            onResource(_resourceDSs[idx], mat);
            _uploadedResourceVersions[idx] = resourceVersion;
            mat->setResourceDirty(false);
        }
    }

  private:
    void rebuild(uint32_t capacity)
    {
        _paramDSs.clear();
        _resourceDSs.clear();

        if (_pool)
            _pool->resetPool();

        _pool = IDescriptorPool::create(
            _render,
            DescriptorPoolCreateInfo{
                .maxSets   = capacity * 2, // N param DSs + N resource DSs
                .poolSizes = _poolSizeFn(capacity),
            });

        _pool->allocateDescriptorSets(_paramDSL, capacity, _paramDSs);
        _pool->allocateDescriptorSets(_resourceDSL, capacity, _resourceDSs);
        _uploadedParamVersions.assign(capacity, 0);
        _uploadedResourceVersions.assign(capacity, 0);

        // Create UBOs for any new slots
        uint32_t prevCount = static_cast<uint32_t>(_paramUBOs.size());
        for (uint32_t i = prevCount; i < capacity; i++) {
            _paramUBOs.push_back(IBuffer::create(
                _render,
                BufferCreateInfo{
                    .label         = std::format("MaterialPool_Param_UBO_{}", i),
                    .usage         = EBufferUsage::UniformBuffer,
                    .size          = sizeof(TParamUBO),
                    .memoryUsage = EMemoryUsage::CpuToGpu,
                }));
        }

        // Re-bind ALL UBOs to their new DSs (pool reset invalidated old bindings)
        std::vector<WriteDescriptorSet> writes;
        writes.reserve(capacity);
        for (uint32_t i = 0; i < capacity; i++) {
            writes.push_back(IDescriptorSetHelper::genSingleBufferWrite(
                _paramDSs[i], 0, EPipelineDescriptorType::UniformBuffer, _paramUBOs[i].get()));
        }
        _render->getDescriptorHelper()->updateDescriptorSets(writes, {});

        _capacity = capacity;
    }
};

} // namespace ya
