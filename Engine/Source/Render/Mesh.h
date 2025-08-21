
#pragma once

#include "Core/Base.h"
#include "Math/Geometry.h"
#include "Platform/Render/Vulkan/VulkanBuffer.h"

namespace ya
{

struct Mesh
{

    std::shared_ptr<VulkanBuffer> _vertexBuffer;
    std::shared_ptr<VulkanBuffer> _indexBuffer;

    uint32_t _indexCount  = 0;
    uint32_t _vertexCount = 0;

  public:
    // TODO: the vertex struct should be different in 2d and 3d, and different pipeline?
    Mesh(const std::vector<ya::Vertex> &vertices, const std::vector<uint32_t> &indices);
    ~Mesh() = default;

    [[nodiscard]] uint32_t getIndexCount() const { return _indexCount; }
    [[nodiscard]] uint32_t getVertexCount() const { return _vertexCount; }

    void setDebugName(const std::string &name);

    [[nodiscard]] const VulkanBuffer *getVertexBuffer() const { return _vertexBuffer.get(); }
    [[nodiscard]] const VulkanBuffer *getIndexBuffer() const { return _indexBuffer.get(); }
};

} // namespace ya