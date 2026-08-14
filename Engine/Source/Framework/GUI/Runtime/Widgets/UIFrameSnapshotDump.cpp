#include "GUI/Widgets/UIFrameSnapshotDump.h"

#include <string_view>

namespace ya
{

namespace
{

uint64_t fnv1a64(std::string_view value)
{
    uint64_t hash = 14695981039346656037ull;
    for (const unsigned char c : value) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

nlohmann::json dumpVec2(const glm::vec2& value)
{
    return {
        {"x", value.x},
        {"y", value.y},
    };
}

nlohmann::json dumpVec4(const glm::vec4& value)
{
    return {
        {"r", value.r},
        {"g", value.g},
        {"b", value.b},
        {"a", value.a},
    };
}

nlohmann::json dumpSemanticItem(const UIFrameDrawItem& item)
{
    return {
        {"kind", item.kind == UIFrameDrawItem::EKind::Sprite ? "sprite" : "text"},
        {"clipped", item.bClipped},
        {"color", dumpVec4(item.color)},
        {"text", item.text},
    };
}

} // namespace

nlohmann::json dumpUIFrameSnapshot(const UIFrameSnapshot& snapshot)
{
    nlohmann::json items = nlohmann::json::array();
    for (const UIFrameDrawItem& item : snapshot.items) {
        items.push_back({
            {"kind", item.kind == UIFrameDrawItem::EKind::Sprite ? "sprite" : "text"},
            {"pos", dumpVec2(item.pos)},
            {"size", dumpVec2(item.size)},
            {"color", dumpVec4(item.color)},
            {"clipped", item.bClipped},
            {"clip",
             {
                 {"pos", dumpVec2(item.clip.pos)},
                 {"size", dumpVec2(item.clip.extent)},
             }},
            {"text", item.text},
            {"textScale", dumpVec2(item.textScale)},
        });
    }

    return {
        {"logicalExtent",
         {
             {"width", snapshot.logicalExtent.width},
             {"height", snapshot.logicalExtent.height},
         }},
        {"uiScale", dumpVec2(snapshot.buildContext.uiScale)},
        {"offset", dumpVec2(snapshot.buildContext.offset)},
        {"items", std::move(items)},
    };
}

uint64_t digestUIFrameSnapshot(const UIFrameSnapshot& snapshot)
{
    return fnv1a64(dumpUIFrameSnapshot(snapshot).dump());
}

uint64_t semanticDigestUIFrameSnapshot(const UIFrameSnapshot& snapshot)
{
    nlohmann::json items = nlohmann::json::array();
    for (const UIFrameDrawItem& item : snapshot.items) {
        items.push_back(dumpSemanticItem(item));
    }
    return fnv1a64(items.dump());
}

} // namespace ya
