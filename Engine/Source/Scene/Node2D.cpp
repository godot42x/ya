#include "Scene/Node2D.h"

#include "Core/Reflection/ReflectionSerializer.h"
#include "reflects-core/lib.h"

namespace ya
{

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
    // Match on the last name segment so namespace-qualified type names work too.
    const std::string shortName = typeName.substr(typeName.find_last_of(':') + 1);

    if (shortName == "UICanvasNode") {
        return makeShared<UICanvasNode>(name);
    }
    if (shortName == "UIPanelNode") {
        return makeShared<UIPanelNode>(name);
    }
    if (shortName == "UITextNode") {
        return makeShared<UITextNode>(name);
    }
    if (shortName == "UIButtonNode") {
        return makeShared<UIButtonNode>(name);
    }
    return nullptr;
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
