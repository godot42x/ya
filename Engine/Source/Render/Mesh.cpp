#include "Mesh.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Runtime/App/App.h"

namespace ya
{

stdptr<Mesh> Mesh::create(const EngineMeshData& meshData)
{
    return makeShared<Mesh>(Mesh(meshData));
}

Mesh::Mesh(const EngineMeshData& meshData)
{
    _name = meshData.name;

    auto render = App::get()->getRender<VulkanRender>();

    std::ranges::for_each(meshData.vertices, [&](const ya::Vertex& v)
                          { boundingBox.expand(v.position); });

    _vertexCount = static_cast<uint32_t>(meshData.vertices.size());
    _indexCount  = static_cast<uint32_t>(meshData.indices.size());

    _vertexBuffer = IBuffer::create(
        render,
        {
            .label       = _name.empty() ? _name : std::format("{}_VertexBuffer", _name),
            .usage       = EBufferUsage::VertexBuffer,
            .data        = (void*)meshData.vertices.data(),
            .size        = static_cast<uint32_t>(sizeof(meshData.vertices[0]) * meshData.vertices.size()),
            .memoryUsage = EMemoryUsage::GpuOnly,
        });

    _indexBuffer = IBuffer::create(
        render,
        {
            .label       = _name.empty() ? _name : std::format("{}_IndexBuffer", _name),
            .usage       = EBufferUsage::IndexBuffer,
            .data        = (void*)meshData.indices.data(),
            .size        = static_cast<uint32_t>(sizeof(meshData.indices[0]) * meshData.indices.size()),
            .memoryUsage = EMemoryUsage::GpuOnly,
        });

    if (!meshData.skeletonVertices.empty()) {
        _optVertexBuffers.push_back(IBuffer::create(
            render,
            {
                .label       = _name.empty() ? _name : std::format("{}_VertexBuffer_Skeleton", _name),
                .usage       = EBufferUsage::VertexBuffer,
                .data        = (void*)meshData.skeletonVertices.data(),
                .size        = static_cast<uint32_t>(sizeof(meshData.skeletonVertices[0]) * meshData.skeletonVertices.size()),
                .memoryUsage = EMemoryUsage::GpuOnly,
            }));
        _optVertexBufferOffsets.resize(_optVertexBuffers.size(), 0);
        YA_CORE_ASSERT(_optVertexBufferOffsets.size() == _optVertexBuffers.size(), "Vertex buffer offsets size does not match vertex buffers size");
    }
}

} // namespace ya
