#include "Mesh.h"
#include "Core/App/App.h"
#include "Platform/Render/Vulkan/VulkanRender.h"

namespace ya
{


Mesh::Mesh(const std::vector<ya::Vertex> &vertices,
           const std::vector<uint32_t>   &indices,
           const std::string             &name,
           CoordinateSystem               sourceCoordSystem)
{
    _name       = name;
    auto render = App::get()->getRender<VulkanRender>();

    _vertexCount = static_cast<uint32_t>(vertices.size());
    _indexCount  = static_cast<uint32_t>(indices.size());

    // Convert coordinate system if source differs from engine target
    std::vector<uint32_t> processedIndices = indices;

    if (sourceCoordSystem != ENGINE_COORDINATE_SYSTEM) {
        // Flip winding order when converting between coordinate systems:
        // - RightHanded→LeftHanded: CCW becomes CW (flip needed)
        // - LeftHanded→RightHanded: CW becomes CCW (flip needed)
        for (size_t i = 0; i < processedIndices.size(); i += 3) {
            std::swap(processedIndices[i], processedIndices[i + 2]);
        }
        YA_CORE_TRACE("Mesh '{}': Converted from {} to {} coordinate system",
                      name,
                      sourceCoordSystem == CoordinateSystem::RightHanded ? "RightHanded" : "LeftHanded",
                      ENGINE_COORDINATE_SYSTEM == CoordinateSystem::RightHanded ? "RightHanded" : "LeftHanded");
    }

    _vertexBuffer = IBuffer::create(
        render,
        {
            .label         = name.empty() ? name : std::format("{}_VertexBuffer", name),
            .usage         = EBufferUsage::VertexBuffer,
            .data          = (void *)vertices.data(),
            .size          = static_cast<uint32_t>(sizeof(vertices[0]) * vertices.size()),
            .memProperties = EMemoryProperty::DeviceLocal,
        });

    _indexBuffer = IBuffer::create(
        render,
        {
            .label         = name.empty() ? name : std::format("{}_IndexBuffer", name),
            .usage         = EBufferUsage::IndexBuffer,
            .data          = (void *)processedIndices.data(),
            .size          = static_cast<uint32_t>(sizeof(processedIndices[0]) * processedIndices.size()),
            .memProperties = EMemoryProperty::DeviceLocal,
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
