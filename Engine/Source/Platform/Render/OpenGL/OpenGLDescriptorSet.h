#pragma once

#include "Core/Base.h"
#include "Render/Core/DescriptorSet.h"
#include "glad/glad.h"

#include <memory>
#include <vector>

namespace ya
{

struct OpenGLRender;

/**
 * @brief OpenGL descriptor set layout implementation
 * 
 * In OpenGL, descriptor sets are implemented as uniform buffer bindings
 * and texture unit bindings.
 */
struct OpenGLDescriptorSetLayout : public IDescriptorSetLayout
{
  private:
    OpenGLRender       *_render = nullptr;
    DescriptorSetLayout _layout;

  public:
    OpenGLDescriptorSetLayout(OpenGLRender *render, const DescriptorSetLayout &layout)
        : _render(render), _layout(layout) {}

    ~OpenGLDescriptorSetLayout() override = default;

    const DescriptorSetLayout &getLayoutInfo() const override { return _layout; }
    void                      *getHandle() const override { return (void *)this; }
};

/**
 * @brief OpenGL descriptor pool implementation
 * 
 * OpenGL doesn't have descriptor pools. This is a lightweight wrapper
 * that tracks allocated descriptor sets.
 */
struct OpenGLDescriptorPool : public IDescriptorPool
{
  private:
    OpenGLRender *_render = nullptr;

    // Track allocated descriptor sets (simple counter)
    uint32_t _allocatedSets = 0;

  public:
    OpenGLDescriptorPool(OpenGLRender *render, const DescriptorPoolCreateInfo &ci)
        : _render(render) {}

    ~OpenGLDescriptorPool() override = default;

    bool allocateDescriptorSets(
        const std::shared_ptr<IDescriptorSetLayout> &layout,
        uint32_t                                     count,
        std::vector<DescriptorSetHandle>            &outSets) override;

    void  resetPool() override { _allocatedSets = 0; }
    void  setDebugName(const char *name) override {} // No-op in OpenGL
    void *getHandle() const override { return (void *)this; }
};

/**
 * @brief OpenGL descriptor set helper
 * 
 * Manages uniform buffer bindings and texture bindings.
 */
struct OpenGLDescriptorHelper : public IDescriptorSetHelper
{
  private:
    OpenGLRender *_render = nullptr;

    // Track current bindings
    struct DescriptorSetData
    {
        std::vector<GLuint> bufferBindings;
        std::vector<GLuint> textureBindings;
    };

    // Map descriptor set handles to their data
    std::unordered_map<void *, DescriptorSetData> _descriptorSets;

  public:
    OpenGLDescriptorHelper(OpenGLRender *render)
        : _render(render) {}

    ~OpenGLDescriptorHelper() override = default;

    void updateDescriptorSets(
        const std::vector<WriteDescriptorSet> &writes,
        const std::vector<CopyDescriptorSet>  &copies = {}) override;

    // OpenGL-specific: apply descriptor set bindings
    void bindDescriptorSet(DescriptorSetHandle descriptorSet, GLuint program);

  private:
    void applyBufferWrite(const WriteDescriptorSet &write);
    void applyImageWrite(const WriteDescriptorSet &write);
};

} // namespace ya
