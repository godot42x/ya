
#pragma once

#include "Core/Base.h"
#include <glm/glm.hpp>



namespace ya
{

using layer_idx_t = uint32_t;



struct UIRenderContext
{
    glm::vec2 pos;
};



// Common alignment enums for UI layout
enum EHorizontalAlignment
{
    HAlign_Left   = 0,
    HAlign_Center = 1,
    HAlign_Right  = 2,
};

enum EVerticalAlignment
{
    VAlign_Top    = 0,
    VAlign_Center = 1,
    VAlign_Bottom = 2,
};

struct FUIColor
{
    glm::vec4 data = glm::vec4(1.0f);

    FUIColor() = default;

    FUIColor(float r, float g, float b, float a)
        : data(r, g, b, a)
    {
    }

    [[nodiscard]] const glm::vec4 &asVec4() const { return data; }


    static auto red() { return FUIColor(1.0f, 0.0f, 0.0f, 1.0f); }
};


// For root UI types without a base class
#define UI_ROOT_TYPE(T)                                             \
    using Super = void;                                             \
    static uint32_t getStaticType() { return ya::type_index_v<T>; } \
    static uint32_t getStaticBaseType() { return 0; } // Root type has no base

struct UILayout
{
    float borderWidth = 1.0f;
};


struct UIAppCtx
{
    glm::vec2 lastMousePos;
};


struct UIMeta
{
    std::unordered_map<uint32_t, uint32_t> inheritanceMap;

    static UIMeta *get();
    UIMeta()                          = default;
    UIMeta(const UIMeta &)            = delete;
    UIMeta(UIMeta &&)                 = delete;
    UIMeta &operator=(const UIMeta &) = delete;
    UIMeta &operator=(UIMeta &&)      = delete;
    // 自动注册 UI 类型继承关系
    void registerInheritance(uint32_t childType, uint32_t parentType)
    {
        inheritanceMap[childType] = parentType;
    }

    bool isBaseOf(uint32_t baseType, uint32_t derivedType)
    {
        uint32_t currentType = derivedType;
        while (currentType != 0) {
            if (currentType == baseType) {
                return true;
            }
            auto it = inheritanceMap.find(currentType);
            if (it == inheritanceMap.end()) {
                break;
            }
            currentType = it->second;
        }
        return false;
    }
};

#define UI_TYPE(T, BASE)                                                                 \
    using Super = BASE;                                                                  \
    static uint32_t    getStaticType() { return ya::type_index_v<T>; }                   \
    static uint32_t    getStaticBaseType() { return ya::type_index_v<BASE>; }            \
    inline static bool _auto_registered = []() {                                         \
        UIMeta::get()->registerInheritance(ya::type_index_v<T>, ya::type_index_v<BASE>); \
        return true;                                                                     \
    }();


struct FUIHelper
{
    static bool isPointInRect(const glm::vec2 &point, const glm::vec2 &rectPos, const glm::vec2 &rectSize)
    {
        return point.x >= rectPos.x &&
               point.x <= rectPos.x + rectSize.x &&
               point.y >= rectPos.y &&
               point.y <= rectPos.y + rectSize.y;
    }
};

}; // namespace ya