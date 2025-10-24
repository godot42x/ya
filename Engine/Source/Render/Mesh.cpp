#include "Mesh.h"
#include "Core/App/App.h"
#include "Platform/Render/Vulkan/VulkanRender.h"

namespace ya
{


Mesh::Mesh(const std::vector<ya::Vertex> &vertices, const std::vector<uint32_t> &indices, const std::string &name)
{
    _name       = name;
    auto render = App::get()->getRender<VulkanRender>();

    _vertexCount = static_cast<uint32_t>(vertices.size());
    _indexCount  = static_cast<uint32_t>(indices.size());

    _vertexBuffer = IBuffer::create(
        render,
        {
            .usage         = EBufferUsage::VertexBuffer,
            .data          = (void *)vertices.data(),
            .size          = static_cast<uint32_t>(sizeof(vertices[0]) * vertices.size()),
            .memProperties = EMemoryProperty::DeviceLocal,
            .label     = name.empty() ? name : std::format("{}_VertexBuffer", name),
        });

    _indexBuffer = IBuffer::create(
        render,
        {
            .usage         = EBufferUsage::IndexBuffer,
            .data          = (void *)indices.data(),
            .size          = static_cast<uint32_t>(sizeof(indices[0]) * indices.size()),
            .memProperties = EMemoryProperty::DeviceLocal,
            .label     = name.empty() ? name : std::format("{}_IndexBuffer", name),
        });
}

void Mesh::draw(ICommandBuffer *cmdBuf) const
{
    // Use generic command buffer interface
    cmdBuf->bindVertexBuffer(0, _vertexBuffer.get(), 0);
    cmdBuf->bindIndexBuffer(_indexBuffer.get(), 0, false); // false = use 32-bit indices
    cmdBuf->drawIndexed(_indexCount, 1, 0, 0, 0);
}


} // namespace ya
