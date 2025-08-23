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
            .debugName     = name.empty() ? name : std::format("{}_VertexBuffer", name),
        });

    _indexBuffer = VulkanBuffer::create(
        render,
        {
            .usage         = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            .data          = (void *)indices.data(),
            .size          = static_cast<uint32_t>(sizeof(indices[0]) * indices.size()),
            .memProperties = 0,
            .debugName     = name.empty() ? name : std::format("{}_IndexBuffer", name),
        });
}


} // namespace ya
