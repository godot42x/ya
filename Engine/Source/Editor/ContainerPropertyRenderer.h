/**
 * @file ContainerPropertyRenderer.h
 * @brief ImGui 容器属性渲染器
 * 
 * 集成到 DetailsView::renderReflectedType 中
 */

#pragma once

#include "Core/Reflection/PropertyExtensions.h"
#include <imgui.h>
#include <string>

namespace ya::editor
{

class ContainerPropertyRenderer
{
  public:
    /**
     * @brief 渲染容器属性（Vector/Map等）
     * 
     * @param name 属性名称
     * @param prop Property 对象
     * @param containerPtr 容器实例指针
     * @param renderElementFn 元素渲染回调 (const std::string& label, void* elementPtr, uint32_t typeIndex) -> bool
     * @param renderKeyFn Map专用: Key渲染回调 (const std::string& currentKey, uint32_t keyTypeIndex) -> std::pair<bool, std::string> (changed, newKey)
     * @return 是否有修改
     */
    template <typename ElementRenderer, typename KeyRenderer = std::nullptr_t>
    static bool renderContainer(const std::string              &name,
                                 Property                       &prop,
                                 void                           *containerPtr,
                                 ElementRenderer                &&renderElementFn,
                                 KeyRenderer                    &&renderKeyFn = nullptr)
    {
        using namespace reflection;

        auto *accessor = PropertyContainerHelper::getContainerAccessor(prop);
        if (!accessor) {
            return false;
        }

        bool bModified = false;

        // 容器头部
        ImGui::PushID(name.c_str());

        size_t size = accessor->getSize(containerPtr);
        ImGui::Text("%s (Size: %zu)", name.c_str(), size);

        // 添加/删除按钮
        ImGui::SameLine();
        if (ImGui::SmallButton("+")) {
            if (accessor->getCategory() == ContainerCategory::SequenceContainer) {
                accessor->addElement(containerPtr, nullptr); // 添加默认元素
                bModified = true;
            }
        }

        if (size > 0) {
            ImGui::SameLine();
            if (ImGui::SmallButton("-")) {
                if (accessor->getCategory() == ContainerCategory::SequenceContainer) {
                    accessor->removeElement(containerPtr, size - 1);
                    bModified = true;
                }
            }

            ImGui::SameLine();
            if (ImGui::SmallButton("Clear")) {
                accessor->clear(containerPtr);
                bModified = true;
            }
        }

        ImGui::Separator();

        // 遍历容器元素
        if (accessor->isMapLike()) {
            // Map 渲染 - 支持编辑 Key 和 Value
            std::vector<void*> keysToDelete; // 存储要删除的 key 指针
            
            // Map 条目缓存结构
            struct MapEntry {
                void* keyPtr;          // key 指针
                uint32_t keyTypeIndex; // key 类型索引
                void* valuePtr;        // value 指针
                uint32_t valueTypeIndex; // value 类型索引
            };
            
            // 使用静态缓存避免每帧重新收集
            static std::unordered_map<void*, std::vector<MapEntry>> entriesCache;
            static std::unordered_map<void*, size_t> cacheSizeCheck; // 检测 map 大小变化
            
            size_t currentSize = accessor->getSize(containerPtr);
            bool needRebuild = false;
            
            // 检查缓存是否需要重建
            if (entriesCache.find(containerPtr) == entriesCache.end() ||
                cacheSizeCheck[containerPtr] != currentSize) {
                needRebuild = true;
            }
            
            if (needRebuild) {
                // 重新收集 map 元素
                std::vector<MapEntry> entries;
                PropertyContainerHelper::iterateMapContainer(
                    prop,
                    containerPtr,
                    [&](void* keyPtr, uint32_t keyTypeIndex, void *valuePtr, uint32_t valueTypeIndex) {
                        entries.push_back({keyPtr, keyTypeIndex, valuePtr, valueTypeIndex});
                    });
                entriesCache[containerPtr] = std::move(entries);
                cacheSizeCheck[containerPtr] = currentSize;
            }
            
            auto& entries = entriesCache[containerPtr];
            
            
            // 排序功能暂时移除（需要根据实际 key 类型实现）
            
            // 渲染所有条目
            for (auto& entry : entries) {
                ImGui::PushID(entry.keyPtr);

                // 渲染 Key - 直接使用指针渲染
                ImGui::PushItemWidth(120);
                
                if constexpr (!std::is_same_v<KeyRenderer, std::nullptr_t>) {
                    // 使用自定义 key 渲染函数
                    renderKeyFn("##key", entry.keyPtr, entry.keyTypeIndex);
                }
                else {
                    // 默认渲染：只读显示（map 的 key 是 const 的，不可修改）
                    renderElementFn("##key_readonly", entry.keyPtr, entry.keyTypeIndex);
                }
                
                ImGui::PopItemWidth();

                ImGui::SameLine();
                ImGui::Text(":");
                ImGui::SameLine();

                // 渲染 Value (可编辑)
                if (renderElementFn("##value", entry.valuePtr, entry.valueTypeIndex)) {
                    bModified = true;
                }

                // 删除按钮
                ImGui::SameLine();
                if (ImGui::SmallButton("X")) {
                    keysToDelete.push_back(entry.keyPtr);
                    bModified = true;
                }

                ImGui::PopID();
            }

            // 删除标记的条目
            for (void* keyPtr : keysToDelete) {
                accessor->removeByKey(containerPtr, keyPtr);
                needRebuild = true; // 标记需要重建缓存
            }
            
            if (needRebuild) {
                entriesCache.erase(containerPtr);
            }

            // 添加新条目功能暂时移除（需要重新设计，因为 key 不再是字符串）
            // TODO: 添加一个弹窗，让用户输入新 key 的值
        }
        else {
            // Vector/Set 渲染
            PropertyContainerHelper::iterateContainer(
                prop,
                containerPtr,
                [&](size_t index, void *elementPtr, uint32_t elementTypeIndex) {
                    ImGui::PushID(static_cast<int>(index));

                    std::string label = std::format("[{}]", index);
                    if (renderElementFn(label, elementPtr, elementTypeIndex)) {
                        bModified = true;
                    }

                    // 删除按钮（仅 Vector）
                    if (accessor->getCategory() == ContainerCategory::SequenceContainer) {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("X")) {
                            accessor->removeElement(containerPtr, index);
                            bModified = true;
                        }
                    }

                    ImGui::PopID();
                });
        }

        ImGui::PopID();

        return bModified;
    }

    /**
     * @brief 渲染基础类型元素（int, float, string等）- 用于 Value
     */
    static bool renderBasicElement(const std::string &label, void *elementPtr, uint32_t typeIndex)
    {
        static auto intTypeIdx    = ya::type_index_v<int>;
        static auto floatTypeIdx  = ya::type_index_v<float>;
        static auto stringTypeIdx = ya::type_index_v<std::string>;
        static auto boolTypeIdx   = ya::type_index_v<bool>;

        if (typeIndex == intTypeIdx) {
            return ImGui::InputInt(label.c_str(), static_cast<int *>(elementPtr));
        }
        else if (typeIndex == floatTypeIdx) {
            return ImGui::InputFloat(label.c_str(), static_cast<float *>(elementPtr));
        }
        else if (typeIndex == stringTypeIdx) {
            auto &str = *static_cast<std::string *>(elementPtr);
            char  buf[256];
            strncpy(buf, str.c_str(), sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';

            if (ImGui::InputText(label.c_str(), buf, sizeof(buf))) {
                str = buf;
                return true;
            }
        }
        else if (typeIndex == boolTypeIdx) {
            return ImGui::Checkbox(label.c_str(), static_cast<bool *>(elementPtr));
        }

        // 未知类型
        ImGui::TextDisabled("%s: [unsupported element type: %u]", label.c_str(), typeIndex);
        return false;
    }
};

} // namespace ya::editor
