#include "UIRender.h"

#include "Core/App/App.h"
#include "Core/Log.h"
#include "Platform/Render/Vulkan/VulkanRender.h"
#include "Platform/Render/Vulkan/VulkanUtils.h"

#include <cmath>

// Static data for 2D rendering
struct Render2DData
{
    // Configuration
    uint32_t maxVertices{10000};
    uint32_t maxIndices{15000};
    uint32_t maxTextureSlots{32};

    // Rendering state
    bool isInitialized{false};
    bool isFrameBegun{false};

    // Current batch data
    std::vector<UIVertex> vertices;
    std::vector<uint32_t> indices;
    uint32_t              currentVertexCount{0};
    uint32_t              currentIndexCount{0};

    // Texture management
    std::vector<uint32_t> textureSlots;
    uint32_t              textureSlotIndex{1}; // 0 is reserved for white texture
    uint32_t              whiteTextureId{0};

    // Vulkan resources
    VkBuffer       vertexBuffer{VK_NULL_HANDLE};
    VkDeviceMemory vertexBufferMemory{VK_NULL_HANDLE};
    void          *vertexBufferMapped{nullptr};

    VkBuffer       indexBuffer{VK_NULL_HANDLE};
    VkDeviceMemory indexBufferMemory{VK_NULL_HANDLE};
    void          *indexBufferMapped{nullptr};

    // Pipeline resources
    VkPipelineLayout      pipelineLayout{VK_NULL_HANDLE};
    VkPipeline            graphicsPipeline{VK_NULL_HANDLE};
    VkDescriptorSetLayout descriptorSetLayout{VK_NULL_HANDLE};
    VkDescriptorPool      descriptorPool{VK_NULL_HANDLE};
    VkDescriptorSet       descriptorSet{VK_NULL_HANDLE};

    // Uniform data
    struct CameraData
    {
        glm::mat4 projectionMatrix;
    } cameraData;

    VkBuffer       uniformBuffer{VK_NULL_HANDLE};
    VkDeviceMemory uniformBufferMemory{VK_NULL_HANDLE};
    void          *uniformBufferMapped{nullptr};

    // Statistics
    struct RenderStats
    {
        uint32_t drawCalls{0};
        uint32_t vertexCount{0};
        uint32_t indexCount{0};
        uint32_t quadCount{0};
    } stats;

    // Default quad vertices (in NDC space)
    static constexpr std::array<glm::vec2, 4> quadVertices = {{
        {-0.5f, -0.5f}, // Bottom left
        {0.5f, -0.5f},  // Bottom right
        {0.5f, 0.5f},   // Top right
        {-0.5f, 0.5f}   // Top left
    }};

    // Default quad indices
    static constexpr std::array<uint32_t, 6> quadIndices = {{0, 1, 2, 2, 3, 0}};
};

static Render2DData s_data;

// Helper functions
static VulkanRender *GetVulkanRenderer()
{
    auto app = Neon::App::get();
    if (!app)
        return nullptr;
    return static_cast<VulkanRender *>(app->getRender());
}

static bool CreateBuffer(VkDevice device, VkPhysicalDevice physicalDevice,
                         VkDeviceSize size, VkBufferUsageFlags usage,
                         VkMemoryPropertyFlags properties,
                         VkBuffer &buffer, VkDeviceMemory &bufferMemory)
{

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size        = size;
    bufferInfo.usage       = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        NE_CORE_ERROR("Failed to create buffer");
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memRequirements.size;
    allocInfo.memoryTypeIndex = VulkanUtils::findMemoryType(physicalDevice, memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        NE_CORE_ERROR("Failed to allocate buffer memory");
        return false;
    }

    vkBindBufferMemory(device, buffer, bufferMemory, 0);
    return true;
}

static void FlushBatch()
{
    if (s_data.currentVertexCount == 0)
        return;

    auto vkRender = GetVulkanRenderer();
    if (!vkRender)
        return;

    // Update vertex buffer
    memcpy(s_data.vertexBufferMapped, s_data.vertices.data(), s_data.currentVertexCount * sizeof(UIVertex));

    // Update index buffer
    memcpy(s_data.indexBufferMapped, s_data.indices.data(), s_data.currentIndexCount * sizeof(uint32_t));

    // Update uniform buffer
    memcpy(s_data.uniformBufferMapped, &s_data.cameraData, sizeof(Render2DData::CameraData));

    // TODO: Record draw commands to command buffer
    // This would be done in the actual render pass

    // Update stats
    s_data.stats.drawCalls++;
    s_data.stats.vertexCount += s_data.currentVertexCount;
    s_data.stats.indexCount += s_data.currentIndexCount;

    // Reset batch
    s_data.currentVertexCount = 0;
    s_data.currentIndexCount  = 0;
    s_data.textureSlotIndex   = 1;
}

static uint32_t GetTextureSlot(uint32_t textureId)
{
    if (textureId == 0)
        return 0; // White texture

    // Find existing slot
    for (uint32_t i = 1; i < s_data.textureSlotIndex; i++) {
        if (s_data.textureSlots[i] == textureId) {
            return i;
        }
    }

    // Add new slot if we have space
    if (s_data.textureSlotIndex < s_data.maxTextureSlots) {
        s_data.textureSlots[s_data.textureSlotIndex] = textureId;
        return s_data.textureSlotIndex++;
    }

    // Flush batch and start new one
    FlushBatch();
    s_data.textureSlots[s_data.textureSlotIndex] = textureId;
    return s_data.textureSlotIndex++;
}

// Implementation of UIElement methods
bool UIElement::hitTest(const glm::vec2 &point) const
{
    return point.x >= position.x && point.x <= position.x + size.x &&
           point.y >= position.y && point.y <= position.y + size.y;
}

void UIText::onPaint()
{
    // TODO: Implement text rendering
}

void UIImage::onPaint()
{
    // TODO: Implement image rendering
}

void UIButton::onPaint()
{
    const auto &style = getCurrentStyle();
    F2DRender::drawQuad(position, size, style.backgroundColor);
    // TODO: Draw text and background texture
}

const UIButton::ButtonStyle &UIButton::getCurrentStyle() const
{
    switch (currentState) {
    case State::Hovered:
        return hoveredStyle;
    case State::Pressed:
        return pressedStyle;
    case State::Disabled:
        return disabledStyle;
    default:
        return normalStyle;
    }
}

// Implementation of F2DRender static methods
bool F2DRender::initialize(uint32_t maxVertices, uint32_t maxIndices)
{
    if (s_data.isInitialized) {
        NE_CORE_WARN("F2DRender already initialized");
        return true;
    }

    auto vkRender = GetVulkanRenderer();
    if (!vkRender) {
        NE_CORE_ERROR("Failed to get Vulkan renderer");
        return false;
    }

    s_data.maxVertices = maxVertices;
    s_data.maxIndices  = maxIndices;

    // Reserve memory for batching
    s_data.vertices.reserve(maxVertices);
    s_data.indices.reserve(maxIndices);
    s_data.textureSlots.resize(s_data.maxTextureSlots);

    // Create buffers
    VkDevice         device         = vkRender->getLogicalDevice();
    VkPhysicalDevice physicalDevice = vkRender->getPhysicalDevice();

    // Vertex buffer
    VkDeviceSize vertexBufferSize = maxVertices * sizeof(UIVertex);
    if (!VulkanUtils::createBuffer(device,
                                   physicalDevice,
                                   vertexBufferSize,
                                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                   s_data.vertexBuffer,
                                   s_data.vertexBufferMemory)) {
        NE_CORE_ERROR("Failed to create vertex buffer");
        return false;
    }

    vkMapMemory(device, s_data.vertexBufferMemory, 0, vertexBufferSize, 0, &s_data.vertexBufferMapped);

    // Index buffer
    VkDeviceSize indexBufferSize = maxIndices * sizeof(uint32_t);
    if (!CreateBuffer(device, physicalDevice, indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, s_data.indexBuffer, s_data.indexBufferMemory)) {
        NE_CORE_ERROR("Failed to create index buffer");
        return false;
    }

    vkMapMemory(device, s_data.indexBufferMemory, 0, indexBufferSize, 0, &s_data.indexBufferMapped);

    // Uniform buffer
    VkDeviceSize uniformBufferSize = sizeof(Render2DData::CameraData);
    if (!CreateBuffer(device, physicalDevice, uniformBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, s_data.uniformBuffer, s_data.uniformBufferMemory)) {
        NE_CORE_ERROR("Failed to create uniform buffer");
        return false;
    }

    vkMapMemory(device, s_data.uniformBufferMemory, 0, uniformBufferSize, 0, &s_data.uniformBufferMapped);

    // TODO: Create pipeline, descriptor sets, etc.
    // This would involve creating a dedicated 2D rendering pipeline

    s_data.isInitialized = true;
    NE_CORE_INFO("F2DRender initialized successfully");

    return true;
}

void F2DRender::shutdown()
{
    if (!s_data.isInitialized) {
        NE_CORE_WARN("F2DRender not initialized");
        return;
    }
    auto vkRender = GetVulkanRenderer();
    if (!vkRender) {
        NE_CORE_ERROR("Failed to get Vulkan renderer");
        return;
    }

    VkDevice device = vkRender->getLogicalDevice();
    vkDeviceWaitIdle(device);
    // Cleanup buffers
    if (s_data.vertexBufferMapped) {
        vkUnmapMemory(device, s_data.vertexBufferMemory);
    }
    vkDestroyBuffer(device, s_data.vertexBuffer, nullptr);
    vkFreeMemory(device, s_data.vertexBufferMemory, nullptr);
    if (s_data.indexBufferMapped) {
        vkUnmapMemory(device, s_data.indexBufferMemory);
    }
    vkDestroyBuffer(device, s_data.indexBuffer, nullptr);
    vkFreeMemory(device, s_data.indexBufferMemory, nullptr);
    if (s_data.uniformBufferMapped) {
        vkUnmapMemory(device, s_data.uniformBufferMemory);
    }
    vkDestroyBuffer(device, s_data.uniformBuffer, nullptr);
    vkFreeMemory(device, s_data.uniformBufferMemory, nullptr);
    s_data.isInitialized = false;
    s_data.isFrameBegun  = false;
    // s_data.vertices.clear();
    // s_data.indices.clear();
    // s_data.textureSlots.clear();
}

void F2DRender::beginFrame(const glm::mat4 &projectionMatrix)
{
    if (!s_data.isInitialized) {
        NE_CORE_ERROR("F2DRender not initialized");
        return;
    }

    if (s_data.isFrameBegun) {
        NE_CORE_WARN("Frame already begun");
        return;
    }

    s_data.isFrameBegun = true;
    s_data.cameraData.projectionMatrix = projectionMatrix;

    // Reset batch data
    s_data.currentVertexCount = 0;
    s_data.currentIndexCount = 0;
    s_data.textureSlotIndex = 1; // 0 is reserved for white texture
    s_data.vertices.clear();
    s_data.indices.clear();

    // Reset stats
    s_data.stats = {};
}

void F2DRender::endFrame()
{
    if (!s_data.isFrameBegun) {
        NE_CORE_WARN("Frame not begun");
        return;
    }

    // Flush any remaining batched data
    FlushBatch();
    s_data.isFrameBegun = false;
}

void F2DRender::render()
{
    // This method would typically be called from the main render loop
    // to actually submit draw commands to the GPU
    if (!s_data.isFrameBegun) {
        return;
    }
    
    // Flush any remaining batched data
    FlushBatch();
}

void F2DRender::submit()
{
    // This is an alternative name for render(), depending on your API design
    render();
}

const F2DRender::RenderStats& F2DRender::getStats()
{
    // Convert internal stats to public interface
    static F2DRender::RenderStats publicStats;
    publicStats.drawCalls = s_data.stats.drawCalls;
    publicStats.vertexCount = s_data.stats.vertexCount;
    publicStats.indexCount = s_data.stats.indexCount;
    publicStats.quadCount = s_data.stats.quadCount;
    return publicStats;
}

void F2DRender::resetStats()
{
    s_data.stats = {};
}

void F2DRender::flush()
{
    FlushBatch();
}

void F2DRender::drawQuad(const glm::vec2 &position, const glm::vec2 &size,
                         const glm::vec4 &color,
                         float rotation, float scale,
                         uint32_t textureId)
{
    if (!s_data.isFrameBegun) {
        NE_CORE_ERROR("Begin frame must be called before drawing");
        return;
    }

    // Check if we need to flush the batch
    if (s_data.currentVertexCount + 4 > s_data.maxVertices ||
        s_data.currentIndexCount + 6 > s_data.maxIndices) {
        FlushBatch();
    }

    // Get texture slot
    float texSlot = static_cast<float>(GetTextureSlot(textureId));

    // Calculate transformation matrix
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(position, 0.0f));
    if (rotation != 0.0f) {
        transform = glm::rotate(transform, glm::radians(rotation), glm::vec3(0.0f, 0.0f, 1.0f));
    }
    if (scale != 1.0f) {
        transform = glm::scale(transform, glm::vec3(scale, scale, 1.0f));
    }
    transform = glm::scale(transform, glm::vec3(size, 1.0f));

    // Define texture coordinates for a quad
    constexpr std::array<glm::vec2, 4> texCoords = {{
        {0.0f, 0.0f}, // Bottom left
        {1.0f, 0.0f}, // Bottom right
        {1.0f, 1.0f}, // Top right
        {0.0f, 1.0f}  // Top left
    }};

    // Add vertices to batch
    uint32_t baseVertex = s_data.currentVertexCount;
    for (size_t i = 0; i < 4; i++) {
        glm::vec4 worldPos = transform * glm::vec4(s_data.quadVertices[i], 0.0f, 1.0f);
        
        UIVertex vertex;
        vertex.position = glm::vec2(worldPos.x, worldPos.y);
        vertex.texCoord = texCoords[i];
        vertex.color = color;
        vertex.textureId = texSlot;
        
        s_data.vertices.push_back(vertex);
    }
    s_data.currentVertexCount += 4;

    // Add indices to batch
    for (uint32_t index : s_data.quadIndices) {
        s_data.indices.push_back(baseVertex + index);
    }
    s_data.currentIndexCount += 6;

    s_data.stats.quadCount++;
}

void F2DRender::drawCircle(const glm::vec2 &center, float radius,
                           const glm::vec4 &color)
{
    if (!s_data.isFrameBegun) {
        NE_CORE_ERROR("Begin frame must be called before drawing");
        return;
    }

    // Circle is drawn as a quad with special texture coordinates
    // The fragment shader can use distance from center to create a circle
    
    const uint32_t segments = 32; // Number of triangular segments for smooth circle
    const uint32_t vertexCount = segments + 1; // Center vertex + perimeter vertices
    const uint32_t indexCount = segments * 3; // Each triangle uses 3 indices

    // Check if we need to flush the batch
    if (s_data.currentVertexCount + vertexCount > s_data.maxVertices ||
        s_data.currentIndexCount + indexCount > s_data.maxIndices) {
        FlushBatch();
    }

    uint32_t baseVertex = s_data.currentVertexCount;
    
    // Add center vertex
    UIVertex centerVertex;
    centerVertex.position = center;
    centerVertex.texCoord = glm::vec2(0.5f, 0.5f); // Center of texture
    centerVertex.color = color;
    centerVertex.textureId = 0.0f; // Use white texture for solid color
    s_data.vertices.push_back(centerVertex);
    s_data.currentVertexCount++;

    // Add perimeter vertices
    for (uint32_t i = 0; i < segments; i++) {
        float angle = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f * 3.14159265359f;
        glm::vec2 offset = radius * glm::vec2(cos(angle), sin(angle));
        
        UIVertex vertex;
        vertex.position = center + offset;
        vertex.texCoord = glm::vec2(0.5f + 0.5f * cos(angle), 0.5f + 0.5f * sin(angle));
        vertex.color = color;
        vertex.textureId = 0.0f; // Use white texture for solid color
        
        s_data.vertices.push_back(vertex);
        s_data.currentVertexCount++;
    }

    // Add indices for triangles
    for (uint32_t i = 0; i < segments; i++) {
        s_data.indices.push_back(baseVertex); // Center vertex
        s_data.indices.push_back(baseVertex + 1 + i); // Current perimeter vertex
        s_data.indices.push_back(baseVertex + 1 + ((i + 1) % segments)); // Next perimeter vertex
        s_data.currentIndexCount += 3;
    }
}

void F2DRender::drawText(const std::string &text, const glm::vec2 &position,
                         float fontSize,
                         const glm::vec4 &color,
                         uint32_t fontTextureId)
{
    if (!s_data.isFrameBegun) {
        NE_CORE_ERROR("Begin frame must be called before drawing");
        return;
    }

    // TODO: Implement proper text rendering with font atlas
    // For now, we'll draw a placeholder quad for each character
    
    float charWidth = fontSize * 0.6f; // Approximate character width
    float charHeight = fontSize;
    
    glm::vec2 currentPos = position;
    
    for (size_t i = 0; i < text.length(); i++) {
        char c = text[i];
        
        if (c == ' ') {
            currentPos.x += charWidth;
            continue;
        }
        
        if (c == '\n') {
            currentPos.x = position.x;
            currentPos.y += charHeight;
            continue;
        }
        
        // Draw a quad for each character
        // In a real implementation, you would:
        // 1. Use a font atlas texture
        // 2. Calculate UV coordinates for the character
        // 3. Apply proper character spacing and kerning
        
        glm::vec2 charSize = glm::vec2(charWidth, charHeight);
        drawQuad(currentPos, charSize, color, 0.0f, 1.0f, fontTextureId);
        
        currentPos.x += charWidth;
    }
}

void F2DRender::drawImage(const std::shared_ptr<Texture2D> &texture,
                          const glm::vec2 &position, const glm::vec2 &size,
                          const glm::vec4 &uvRect,
                          float rotation, float scale,
                          bool maintainAspectRatio)
{
    if (!s_data.isFrameBegun) {
        NE_CORE_ERROR("Begin frame must be called before drawing");
        return;
    }

    if (!texture) {
        NE_CORE_ERROR("Texture is null");
        return;
    }

    // TODO: Get texture ID from texture object
    // For now, use a placeholder texture ID
    uint32_t textureId = 1; // This would be texture->getId() or similar

    glm::vec2 renderSize = size;
    
    if (maintainAspectRatio && texture) {
        // TODO: Get texture dimensions and maintain aspect ratio
        // float aspectRatio = texture->getWidth() / static_cast<float>(texture->getHeight());
        // Adjust renderSize based on aspect ratio
    }

    // Check if we need to flush the batch
    if (s_data.currentVertexCount + 4 > s_data.maxVertices ||
        s_data.currentIndexCount + 6 > s_data.maxIndices) {
        FlushBatch();
    }

    // Get texture slot
    float texSlot = static_cast<float>(GetTextureSlot(textureId));

    // Calculate transformation matrix
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(position, 0.0f));
    if (rotation != 0.0f) {
        transform = glm::rotate(transform, glm::radians(rotation), glm::vec3(0.0f, 0.0f, 1.0f));
    }
    if (scale != 1.0f) {
        transform = glm::scale(transform, glm::vec3(scale, scale, 1.0f));
    }
    transform = glm::scale(transform, glm::vec3(renderSize, 1.0f));

    // Define texture coordinates using UV rect
    std::array<glm::vec2, 4> texCoords = {{
        {uvRect.x, uvRect.y},                     // Bottom left
        {uvRect.x + uvRect.z, uvRect.y},          // Bottom right
        {uvRect.x + uvRect.z, uvRect.y + uvRect.w}, // Top right
        {uvRect.x, uvRect.y + uvRect.w}           // Top left
    }};

    // Add vertices to batch
    uint32_t baseVertex = s_data.currentVertexCount;
    for (size_t i = 0; i < 4; i++) {
        glm::vec4 worldPos = transform * glm::vec4(s_data.quadVertices[i], 0.0f, 1.0f);
        
        UIVertex vertex;
        vertex.position = glm::vec2(worldPos.x, worldPos.y);
        vertex.texCoord = texCoords[i];
        vertex.color = glm::vec4(1.0f); // Full white for pure texture sampling
        vertex.textureId = texSlot;
        
        s_data.vertices.push_back(vertex);
    }
    s_data.currentVertexCount += 4;

    // Add indices to batch
    for (uint32_t index : s_data.quadIndices) {
        s_data.indices.push_back(baseVertex + index);
    }
    s_data.currentIndexCount += 6;
}

void F2DRender::drawLine(const glm::vec2 &start, const glm::vec2 &end,
                         const glm::vec4 &color,
                         float thickness)
{
    if (!s_data.isFrameBegun) {
        NE_CORE_ERROR("Begin frame must be called before drawing");
        return;
    }

    // Draw line as a rotated rectangle
    glm::vec2 direction = end - start;
    float length = glm::length(direction);
    
    if (length < 0.001f) {
        return; // Line too short to draw
    }
    
    direction = glm::normalize(direction);
    float angle = atan2(direction.y, direction.x) * 180.0f / 3.14159265359f;
    
    // Calculate line center and size
    glm::vec2 center = (start + end) * 0.5f;
    glm::vec2 lineSize = glm::vec2(length, thickness);
    
    // Draw the line as a rotated quad
    drawQuad(center, lineSize, color, angle, 1.0f, 0);
}
