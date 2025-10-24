
#pragma once

#include "Core/Base.h"
#include "Math/Geometry.h"
#include "Render/Core/Buffer.h"
#include "Render/Core/CommandBuffer.h"

namespace ya
{

struct Mesh
{

    std::string _name;

    std::shared_ptr<IBuffer> _vertexBuffer;
    std::shared_ptr<IBuffer> _indexBuffer;

    uint32_t _indexCount  = 0;
    uint32_t _vertexCount = 0;

  public:
    // TODO: the vertex struct should be different in 2d and 3d, and different pipeline?
    Mesh(const std::vector<ya::Vertex> &vertices, const std::vector<uint32_t> &indices, const std::string &name = "");
    ~Mesh() = default;

    [[nodiscard]] uint32_t getIndexCount() const { return _indexCount; }
    [[nodiscard]] uint32_t getVertexCount() const { return _vertexCount; }


    [[nodiscard]] const IBuffer *getVertexBuffer() const { return _vertexBuffer.get(); }
    [[nodiscard]] IBuffer       *getVertexBufferMut() const { return _vertexBuffer.get(); }
    [[nodiscard]] const IBuffer *getIndexBuffer() const { return _indexBuffer.get(); }
    [[nodiscard]] IBuffer       *getIndexBufferMut() const { return _indexBuffer.get(); }

    void draw(ICommandBuffer *cmdBuf) const;
};

} // namespace ya