/**
 * @file ContainerPropertyRenderer.h
 * @brief ImGui container property renderer with optimized performance
 *
 * Integrated into DetailsView::renderReflectedType
 *
 * Performance optimizations:
 * - Lazy rendering: Only render when TreeNode is expanded
 * - Efficient caching: Map entries cached with size-based invalidation
 * - Stack allocation: Use char arrays instead of std::string for labels
 * - Deferred deletion: Collect items to delete, then delete after iteration
 */

#pragma once

#include "Core/Debug/Instrumentor.h"
#include "Core/Reflection/ContainerProperty.h"
#include "Core/Reflection/PropertyExtensions.h"
#include "Core/Reflection/ReflectionHelper.h"

#include "TypeRenderer.h"
#include "utility.cc/ranges.h"
#include <imgui.h>
#include <string>
#include <unordered_map>



namespace ya::editor
{

class ContainerPropertyRenderer
{
  public:
    /**
     * @brief Render a container property (Vector/Map/Set)
     *
     * @param name Property display name
     * @param prop Property object with container extension
     * @param containerPtr Pointer to the container instance
     * @param renderFn Element render callback: (label, elementPtr, typeIndex) -> bool
     * @return true if any modification was made
     */
    static bool renderContainer(const std::string &name,
                                Property          &prop,
                                void              *containerPtr,
                                int                depth)
    {
        YA_PROFILE_SCOPE("ContainerPropertyRenderer::renderContainer");
        using namespace reflection;

        auto *accessor = PropertyContainerHelper::getContainerAccessor(prop);
        if (!accessor) {
            return false;
        }

        bool   bModified = false;
        size_t size      = accessor->getSize(containerPtr);

        ImGui::PushID(containerPtr); // Use pointer as ID for better uniqueness


        // TreeNode with performance-optimized flags
        char headerLabel[128];
        snprintf(headerLabel, sizeof(headerLabel), "%s [%zu]", name.c_str(), size);

        bool bNodeOpen = ImGui::TreeNodeEx(headerLabel);

        // Render collapsible header with size info
        bModified |= renderButtons(name, size, accessor, containerPtr);

        if (bNodeOpen) {
            // Only render contents when expanded - key performance optimization
            if (accessor->isMapLike()) {
                bModified |= renderMapContainer(accessor,
                                                containerPtr,
                                                prop,
                                                depth + 1);
            }
            else {
                bModified |= renderSequenceContainer(accessor,
                                                     containerPtr,
                                                     prop,
                                                     depth + 1);
            }


            ImGui::TreePop();
        }

        ImGui::PopID();
        return bModified;
    }

    /**
     * @brief Clear all cached data (call when scene changes or on cleanup)
     */
    static void clearCache()
    {
        getMapCache().clear();
    }

  private:
    // ==================== Map Container Support ====================

    struct MapEntry
    {
        void       *keyPtr;
        uint32_t    keyTypeIndex;
        void       *valuePtr;
        uint32_t    valueTypeIndex;
        std::string keyStr;
        std::string valueStr;
        std::string typeStr;
        bool        bModified = false;
    };

    struct CachedMapData
    {
        std::vector<MapEntry> entries;
        size_t                cachedSize = 0;

        bool needsRebuild(size_t currentSize) const
        {
            return cachedSize != currentSize;
        }

        void markDirty()
        {
            cachedSize = SIZE_MAX;
        }
    };

    using MapCacheType = std::unordered_map<void *, CachedMapData>;

    static MapCacheType &getMapCache()
    {
        static MapCacheType cache;
        return cache;
    }

    static bool renderMapContainer(reflection::IContainerProperty *accessor,
                                   void                           *containerPtr,
                                   Property                       &prop,
                                   int                             depth)
    {
        bool   bModified   = false;
        size_t currentSize = accessor->getSize(containerPtr);

        // Get or create cache entry
        auto &cache = getMapCache()[containerPtr];

        // Rebuild cache if size changed
        if (cache.needsRebuild(currentSize)) {
            rebuildMapCache(cache, containerPtr, prop, currentSize);
        }


        // Render entries and collect deletions
        static std::vector<void *> keysToDelete;
        keysToDelete.clear();

        for (const auto &[idx, entry] : ut::enumerate(cache.entries)) {
            ImGui::PushID(entry.keyPtr);

            // 1. key and value is a plat type(int, float, string, bool) : render as key:value
            if (ReflectionHelper::isScalarType(entry.keyTypeIndex) && ReflectionHelper::isScalarType(entry.valueTypeIndex)) {
                // Render as key:value
                auto available = ImGui::GetContentRegionAvail();
                ImGui::PushItemWidth(available.x * 0.4f);

                ya::RenderContext ctx;
                ya::renderReflectedType("##key", entry.keyTypeIndex, entry.valuePtr, ctx, depth + 1);
                bModified |= ctx.hasModifications();

                ImGui::PopItemWidth();

                ImGui::SameLine();
                ImGui::TextUnformatted(":");
                ImGui::SameLine();

                // Value (editable)
                ya::RenderContext ctx2;
                ya::renderReflectedType("##val", entry.valueTypeIndex, entry.valuePtr, ctx2, depth + 1);
                bModified |= ctx2.hasModifications();

                // Delete button
                ImGui::SameLine();
                if (ImGui::SmallButton("X")) {
                    keysToDelete.push_back(entry.keyPtr);
                }
            }
            // 2. render in one tree node key \n value
            else {
                bool bNodeOpen = ImGui::TreeNodeEx(std::format("{}", idx).c_str());

                ImGui::SameLine();
                if (ImGui::SmallButton("X")) {
                    keysToDelete.push_back(entry.keyPtr);
                }
                // Key (read-only for maps)
                if (bNodeOpen) {
                    ya::RenderContext ctx3;
                    ya::renderReflectedType("##key", entry.keyTypeIndex, entry.keyPtr, ctx3, depth + 1);
                    ya::RenderContext ctx4;
                    ya::renderReflectedType(entry.typeStr, entry.valueTypeIndex, entry.valuePtr, ctx4, depth + 1);
                    bModified |= ctx3.hasModifications() || ctx4.hasModifications();
                    ImGui::TreePop();
                }

                ImGui::Separator();
            }



            ImGui::PopID();
        }

        // Process deletions after iteration
        if (!keysToDelete.empty()) {
            for (void *keyPtr : keysToDelete) {
                accessor->removeByKey(containerPtr, keyPtr);
            }
            cache.markDirty();
            bModified = true;
        }

        return bModified;
    }

    static void rebuildMapCache(CachedMapData &cache, void *containerPtr, Property &prop, size_t currentSize)
    {
        cache.entries.clear();
        cache.entries.reserve(currentSize);

        // Iterate map and collect all entries
        ya::reflection::PropertyContainerHelper::iterateMapContainer(
            prop,
            containerPtr,
            [&](void *keyPtr, uint32_t keyTypeIndex, void *valuePtr, uint32_t valueTypeIndex) {
                cache.entries.push_back({
                    .keyPtr         = keyPtr,
                    .keyTypeIndex   = keyTypeIndex,
                    .valuePtr       = valuePtr,
                    .valueTypeIndex = valueTypeIndex,
                    .keyStr         = toString(keyPtr, keyTypeIndex),
                    .valueStr       = toString(valuePtr, valueTypeIndex),
                    .typeStr        = getTypeName(valueTypeIndex),
                    .bModified      = false,
                });
            });

        cache.cachedSize = currentSize;
    }

    // ==================== Sequence Container Support ====================

    template <typename RenderFn>
    static bool renderSequenceContainer(reflection::IContainerProperty *accessor,
                                        void                           *containerPtr,
                                        Property                       &prop,
                                        RenderFn                      &&renderFn)
    {
        bool   bModified     = false;
        size_t indexToRemove = SIZE_MAX;

        ya::reflection::PropertyContainerHelper::iterateContainer(
            prop,
            containerPtr,
            [&](size_t index, void *elementPtr, uint32_t elementTypeIndex) {
                ImGui::PushID(static_cast<int>(index));

                // Stack-allocated label buffer
                char label[32];
                snprintf(label, sizeof(label), "[%zu]", index);

                ya::RenderContext ctx;
                ya::renderReflectedType(label, elementTypeIndex, elementPtr, ctx);
                bModified |= ctx.hasModifications();

                // Delete button for sequence containers
                if (accessor->getCategory() == reflection::ContainerCategory::SequenceContainer) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X")) {
                        indexToRemove = index;
                    }
                }

                ImGui::PopID();
            });

        // Deferred deletion to avoid iterator invalidation
        if (indexToRemove != SIZE_MAX) {
            accessor->removeElement(containerPtr, indexToRemove);
            bModified = true;
        }

        return bModified;
    }

    // ==================== Header Rendering ====================

    static bool renderButtons(const std::string              &name,
                              size_t                          size,
                              reflection::IContainerProperty *accessor,
                              void                           *containerPtr)
    {
        bool bModified = false;

        // Add element button
        ImGui::SameLine();
        if (ImGui::SmallButton("+")) {
            accessor->addEmptyEntry(containerPtr);
            bModified = true;
        }

        if (size > 0) {
            // Clear all button
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear")) {
                accessor->clear(containerPtr);
                // Also clear map cache for this container
                getMapCache().erase(containerPtr);
                bModified = true;
            }
        }

        return bModified;
    }

  public:
    // ==================== Basic Type Renderers ====================

    /**
     * @brief Render basic type elements (int, float, string, bool)
     * Used as default renderer for container values
     */
    static bool renderBasicElement(const char *label, void *elementPtr, uint32_t typeIndex)
    {
        // Cache type indices for fast comparison
        static const auto kIntTypeIdx    = ya::type_index_v<int>;
        static const auto kFloatTypeIdx  = ya::type_index_v<float>;
        static const auto kStringTypeIdx = ya::type_index_v<std::string>;
        static const auto kBoolTypeIdx   = ya::type_index_v<bool>;

        if (typeIndex == kIntTypeIdx) {
            return ImGui::InputInt(label, static_cast<int *>(elementPtr));
        }
        if (typeIndex == kFloatTypeIdx) {
            return ImGui::InputFloat(label, static_cast<float *>(elementPtr));
        }
        if (typeIndex == kStringTypeIdx) {
            auto &str = *static_cast<std::string *>(elementPtr);
            char  buf[256];
            std::memcpy(buf, str.c_str(), std::min(str.size() + 1, sizeof(buf)));
            buf[sizeof(buf) - 1] = '\0';

            if (ImGui::InputText(label, buf, sizeof(buf))) {
                str = buf;
                return true;
            }
            return false;
        }
        if (typeIndex == kBoolTypeIdx) {
            return ImGui::Checkbox(label, static_cast<bool *>(elementPtr));
        }

        // Unknown type fallback
        ImGui::TextDisabled("%s: [unsupported type: %u]", label, typeIndex);
        return false;
    }

    // Overload for std::string label (convenience)
    static bool renderBasicElement(const std::string &label, void *elementPtr, uint32_t typeIndex)
    {
        return renderBasicElement(label.c_str(), elementPtr, typeIndex);
    }

    static std::string toString(const void *ptr, uint32_t typeIndex)
    {
        if (typeIndex == ya::type_index_v<std::string>) {
            return *static_cast<const std::string *>(ptr);
        }
        else {
            return std::format("{} [unsupported type: {}]", uintptr_t(ptr), typeIndex);
        }
    }
    static std::string getTypeName(uint32_t typeIndex)
    {
        if (auto cls = ClassRegistry::instance().getClass(typeIndex); cls) {
            return cls->getName();
        }
        return std::format("unknown type: {}", typeIndex);
    }
};

} // namespace ya::editor
