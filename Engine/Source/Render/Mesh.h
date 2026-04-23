
#pragma once

#include "Core/Base.h"
#include "Core/Math/AABB.h"
#include "Core/Math/Geometry.h"
#include "Render/Core/Buffer.h"
#include "Render/Core/CommandBuffer.h"
#include "Render/EngineMeshData.h"

namespace ya
{

// Forward declaration
enum class CoordinateSystem;

struct Mesh
{

    std::string _name;

    std::shared_ptr<IBuffer> _vertexBuffer;
    uint32_t                 _vertexBufferOffset = 0; // leave for feature sub-mesh

    std::shared_ptr<IBuffer> _indexBuffer;
    uint32_t                 _indexBufferOffset = 0;

    std::vector<stdptr<IBuffer>> _optVertexBuffers;
    std::vector<uint32_t>        _optVertexBufferOffsets;

    uint32_t _indexCount  = 0;
    uint32_t _vertexCount = 0;

    AABB boundingBox;

  public:
    static stdptr<Mesh> create(const EngineMeshData& meshData);

    ~Mesh() = default;

    [[nodiscard]] uint32_t getIndexCount() const { return _indexCount; }
    [[nodiscard]] uint32_t getVertexCount() const { return _vertexCount; }


    [[nodiscard]] const IBuffer* getVertexBuffer() const { return _vertexBuffer.get(); }
    [[nodiscard]] IBuffer*       getVertexBufferMut() const { return _vertexBuffer.get(); }
    [[nodiscard]] const IBuffer* getIndexBuffer() const { return _indexBuffer.get(); }
    [[nodiscard]] IBuffer*       getIndexBufferMut() const { return _indexBuffer.get(); }

    void draw(ICommandBuffer* cmdBuf /*, bool bHasOptVertexBuffer = false*/) const
    {
        // Use generic command buffer interface
        cmdBuf->bindVertexBuffer(0, _vertexBuffer.get(), _vertexBufferOffset);
        // if (bHasOptVertexBuffer) {
        for (int i = 0; i < _optVertexBuffers.size(); ++i) {
            cmdBuf->bindVertexBuffer(i + 1, _optVertexBuffers[i].get(), _optVertexBufferOffsets[i]);
        }
        // }
        cmdBuf->bindIndexBuffer(_indexBuffer.get(), _indexBufferOffset, false); // false = use 32-bit indices
        cmdBuf->drawIndexed(_indexCount, 1, 0, 0, 0);
    }

    const std::string& getName() const { return _name; }

  private:
    explicit Mesh(const EngineMeshData& meshData);
};

} // namespace ya