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

    _vertexBuffer = VulkanBuffer::create(
        render,
        {
            .usage         = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            .data          = (void *)vertices.data(),
            .size          = static_cast<uint32_t>(sizeof(vertices[0]) * vertices.size()),
            .memProperties = 0,
            .label     = name.empty() ? name : std::format("{}_VertexBuffer", name),
        });

    _indexBuffer = VulkanBuffer::create(
        render,
        {
            .usage         = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            .data          = (void *)indices.data(),
            .size          = static_cast<uint32_t>(sizeof(indices[0]) * indices.size()),
            .memProperties = 0,
            .label     = name.empty() ? name : std::format("{}_IndexBuffer", name),
        });
}

void Mesh::draw(void *cmdBuf) const
{
    VkCommandBuffer vkCmdBuf        = static_cast<VkCommandBuffer>(cmdBuf);
    VkBuffer        vertexBuffers[] = {_vertexBuffer->getHandle()};
    VkDeviceSize    offsets[]       = {0};
    vkCmdBindVertexBuffers(vkCmdBuf, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(vkCmdBuf, _indexBuffer->getHandle(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(vkCmdBuf, _indexCount, 1, 0, 0, 0);
}


} // namespace ya
