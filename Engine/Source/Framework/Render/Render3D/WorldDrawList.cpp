#include "Render3D/WorldDraw.h"

#include <glm/gtc/constants.hpp>

namespace ya
{

void WorldDrawList::seal()
{
    if (pendingCount == 0) {
        bPending = false;
        return;
    }
    commands.push_back(Command{
        .firstVertex = pendingFirst,
        .vertexCount = pendingCount,
    });
    pendingCount = 0;
    bPending = false;
}

void WorldDrawList::appendSegment(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color)
{
    if (!bPending) {
        bPending = true;
        pendingFirst = static_cast<uint32_t>(vertices.size());
        pendingCount = 0;
    }
    vertices.push_back(WorldDrawVertex{.pos = from, .color = color});
    vertices.push_back(WorldDrawVertex{.pos = to, .color = color});
    pendingCount += 2;
}

void WorldDrawList::makeLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color)
{
    appendSegment(from, to, color);
}

void WorldDrawList::makeWireBox(const glm::mat4& model, const glm::vec3& halfExtent, const glm::vec4& color)
{
    static constexpr std::array<glm::vec3, 8> corners = {{
        {-1.0f, -1.0f, -1.0f}, {1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, -1.0f}, {-1.0f, 1.0f, -1.0f},
        {-1.0f, -1.0f, 1.0f},  {1.0f, -1.0f, 1.0f},  {1.0f, 1.0f, 1.0f},  {-1.0f, 1.0f, 1.0f},
    }};
    static constexpr std::array<std::array<uint8_t, 2>, 12> edges = {{
        {{0, 1}}, {{1, 2}}, {{2, 3}}, {{3, 0}},
        {{4, 5}}, {{5, 6}}, {{6, 7}}, {{7, 4}},
        {{0, 4}}, {{1, 5}}, {{2, 6}}, {{3, 7}},
    }};
    for (const auto& edge : edges) {
        appendSegment(glm::vec3(model * glm::vec4(corners[edge[0]] * halfExtent, 1.0f)),
                      glm::vec3(model * glm::vec4(corners[edge[1]] * halfExtent, 1.0f)),
                      color);
    }
}

void WorldDrawList::makeWireSphere(const glm::vec3& center, float radius, const glm::vec4& color)
{
    static constexpr int   kSegmentCount = 24;
    static constexpr float kStep         = glm::two_pi<float>() / static_cast<float>(kSegmentCount);
    const auto addRing = [&](const glm::vec3& axisA, const glm::vec3& axisB) {
        for (int i = 0; i < kSegmentCount; ++i) {
            const float a0 = static_cast<float>(i) * kStep;
            const float a1 = static_cast<float>(i + 1) * kStep;
            appendSegment(center + radius * (axisA * glm::cos(a0) + axisB * glm::sin(a0)),
                          center + radius * (axisA * glm::cos(a1) + axisB * glm::sin(a1)),
                          color);
        }
    };
    addRing({1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    addRing({1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f});
    addRing({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f});
}

} // namespace ya
