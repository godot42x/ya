
#pragma once

#include "Core/Base.h"
#include "Core/Math/AABB.h"
#include "Core/Math/Geometry.h"
#include "Render/Core/Buffer.h"
#include "Render/Core/CommandBuffer.h"

namespace ya
{

// Forward declaration
enum class CoordinateSystem;

struct Mesh
{

    std::string _name;

    std::shared_ptr<IBuffer> _vertexBuffer;
    std::shared_ptr<IBuffer> _indexBuffer;

    uint32_t _indexCount  = 0;
    uint32_t _vertexCount = 0;

    AABB boundingBox;

  public:
    // TODO: the vertex struct should be different in 2d and 3d, and different pipeline?
    /**
     * @brief Construct mesh from vertices and indices
     * @param vertices Vertex data
     * @param indices Index data
     * @param name Mesh name for debugging
     * @param sourceCoordSystem Source coordinate system (default: LeftHanded for procedural geometry)
     */
    Mesh(const std::vector<ya::Vertex> &vertices,
         const std::vector<uint32_t>   &indices,
         const std::string             &name              = "",
         CoordinateSystem               sourceCoordSystem = CoordinateSystem::LeftHanded);
    ~Mesh() = default;

    [[nodiscard]] uint32_t getIndexCount() const { return _indexCount; }
    [[nodiscard]] uint32_t getVertexCount() const { return _vertexCount; }


    [[nodiscard]] const IBuffer *getVertexBuffer() const { return _vertexBuffer.get(); }
    [[nodiscard]] IBuffer       *getVertexBufferMut() const { return _vertexBuffer.get(); }
    [[nodiscard]] const IBuffer *getIndexBuffer() const { return _indexBuffer.get(); }
    [[nodiscard]] IBuffer       *getIndexBufferMut() const { return _indexBuffer.get(); }

    void draw(ICommandBuffer *cmdBuf) const;

    const std::string &getName() const { return _name; }
};

} // namespace ya