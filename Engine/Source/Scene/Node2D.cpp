#include "Scene/Node2D.h"

#include "Core/Reflection/ReflectionSerializer.h"
#include "reflects-core/lib.h"

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <vector>

namespace ya
{

namespace
{

std::string shortTypeName(std::string_view typeName)
{
    const size_t pos = typeName.find_last_of(':');
    return pos == std::string_view::npos ? std::string(typeName) : std::string(typeName.substr(pos + 1));
}

using Node2DFactory = std::function<std::shared_ptr<Node2D>(const std::string& name)>;

/// Reflection-driven Node2D factory map: every registered subclass of Node2D
/// discovered through ClassRegistry::parentToChildren automatically becomes a
/// creatable UI node type. No hardcoded if-chain per node type.
const std::unordered_map<std::string, Node2DFactory>& getNode2DFactories()
{
    static const std::unordered_map<std::string, Node2DFactory> factories = [] {
        std::unordered_map<std::string, Node2DFactory> map;
        auto& registry = ClassRegistry::instance();

        std::vector<type_index_t> pending{ya::type_index_v<Node2D>};
        while (!pending.empty()) {
            const type_index_t typeId = pending.back();
            pending.pop_back();

            const auto childrenIt = registry.parentToChildren.find(typeId);
            if (childrenIt == registry.parentToChildren.end()) {
                continue;
            }
            for (const type_index_t childTypeId : childrenIt->second) {
                if (Class* cls = registry.getClass(childTypeId)) {
                    const std::string shortName = shortTypeName(cls->name);
                    if (!shortName.empty() && !map.contains(shortName)) {
                        map.emplace(shortName, [cls](const std::string& name) -> std::shared_ptr<Node2D> {
                            void* raw = cls->createInstance(); // default ctor registered via reflection
                            auto* node = static_cast<Node2D*>(raw);
                            node->setName(name);
                            return std::shared_ptr<Node2D>(node, [cls](Node2D* ptr) { cls->destroyInstance(ptr); });
                        });
                    }
                }
                pending.push_back(childTypeId);
            }
        }
        return map;
    }();
    return factories;
}

} // namespace

glm::vec2 Node2D::getScreenPosition() const
{
    glm::vec2 pos = _position;
    for (const Node* parent = getParent(); parent != nullptr; parent = parent->getParent()) {
        if (const auto* parent2D = dynamic_cast<const Node2D*>(parent)) {
            pos += parent2D->_position;
        }
    }
    return pos;
}

bool Node2D::hitTest(const glm::vec2& screenPoint) const
{
    if (!_visible) {
        return false;
    }
    const glm::vec2 pos = getScreenPosition();
    return screenPoint.x >= pos.x && screenPoint.x <= pos.x + _size.x &&
           screenPoint.y >= pos.y && screenPoint.y <= pos.y + _size.y;
}

nlohmann::json Node2D::serializeFields() const
{
    auto* cls = ClassRegistry::instance().getClass(getTypeIndex());
    if (!cls) {
        return nlohmann::json();
    }
    return ReflectionSerializer::serializeByRuntimeReflection(this, getTypeIndex(), cls->getName());
}

void Node2D::deserializeFields(const nlohmann::json& fields)
{
    auto* cls = ClassRegistry::instance().getClass(getTypeIndex());
    if (!cls) {
        return;
    }
    ReflectionSerializer::deserializeByRuntimeReflection(this, getTypeIndex(), fields, cls->getName());
}

std::shared_ptr<Node2D> createNode2DByTypeName(const std::string& typeName, const std::string& name)
{
    const auto& factories = getNode2DFactories();
    const auto  it        = factories.find(shortTypeName(typeName));
    return it == factories.end() ? nullptr : it->second(name);
}

std::vector<std::string> getRegisteredUINodeTypeNames()
{
    std::vector<std::string> names;
    for (const auto& [shortName, factory] : getNode2DFactories()) {
        (void)factory;
        names.push_back(shortName);
    }
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace ya

// Enum reflection for serialization (must register at global scope)
YA_REFLECT_ENUM_BEGIN(ya::EUIAlignH)
YA_REFLECT_ENUM_VALUE(Left)
YA_REFLECT_ENUM_VALUE(Center)
YA_REFLECT_ENUM_VALUE(Right)
YA_REFLECT_ENUM_END()

YA_REFLECT_ENUM_BEGIN(ya::EUIAlignV)
YA_REFLECT_ENUM_VALUE(Top)
YA_REFLECT_ENUM_VALUE(Center)
YA_REFLECT_ENUM_VALUE(Bottom)
YA_REFLECT_ENUM_END()
