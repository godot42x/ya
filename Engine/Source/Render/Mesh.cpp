#include "Mesh.h"
#include "Core/App/App.h"
#include "Platform/Render/Vulkan/VulkanRender.h"

namespace ya
{

Mesh::Mesh(const std::vector<ya::Vertex> &vertices, const std::vector<uint32_t> &indices)
{
    auto render = App::get()->getRender<VulkanRender>();

    _vertexCount = static_cast<uint32_t>(vertices.size());
    _indexCount  = static_cast<uint32_t>(indices.size());

    _vertexBuffer = VulkanBuffer::create(
        render,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        sizeof(vertices[0]) * vertices.size(),
        vertices.data());

    _indexBuffer = VulkanBuffer::create(
        render,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        sizeof(indices[0]) * indices.size(),
        indices.data());
}

void Mesh::setDebugName(const std::string &name)
{
    NE_CORE_ASSERT(_vertexBuffer && _indexBuffer, "Mesh buffers are not initialized!");
    auto render = App::get()->getRender<VulkanRender>();
    render->setDebugObjectName(VK_OBJECT_TYPE_BUFFER, _vertexBuffer->getHandle(), name + "_VertexBuffer");
    render->setDebugObjectName(VK_OBJECT_TYPE_BUFFER, _indexBuffer->getHandle(), name + "_IndexBuffer");
}

} // namespace ya
