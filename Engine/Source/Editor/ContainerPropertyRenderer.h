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
    template <typename RenderFn>
    static bool renderContainer(const std::string &name,
                                Property          &prop,
                                void              *containerPtr,
                                RenderFn         &&renderFn)
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

        // Render collapsible header with size info
        bModified |= renderHeader(name, size, accessor, containerPtr);

        // TreeNode with performance-optimized flags
        char headerLabel[128];
        snprintf(headerLabel, sizeof(headerLabel), "%s [%zu]", name.c_str(), size);

        constexpr ImGuiTreeNodeFlags kTreeFlags = ImGuiTreeNodeFlags_FramePadding;

        if (ImGui::TreeNodeEx(headerLabel, kTreeFlags)) {
            // Only render contents when expanded - key performance optimization
            if (accessor->isMapLike()) {
                bModified |= renderMapContainer(accessor,
                                                containerPtr,
                                                prop,
                                                std::forward<RenderFn>(renderFn));
            }
            else {
                bModified |= renderSequenceContainer(accessor,
                                                     containerPtr,
                                                     prop,
                                                     std::forward<RenderFn>(renderFn));
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
        void    *keyPtr;
        uint32_t keyTypeIndex;
        void    *valuePtr;
        uint32_t valueTypeIndex;
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

    template <typename RenderFn>
    static bool renderMapContainer(reflection::IContainerProperty *accessor,
                                   void                           *containerPtr,
                                   Property                       &prop,
                                   RenderFn                      &&renderFn)
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

        for (auto &entry : cache.entries) {
            ImGui::PushID(entry.keyPtr);

            // Key (read-only for maps)
            ImGui::PushItemWidth(120);
            bModified |= std::forward<RenderFn>(renderFn)("##key", entry.keyPtr, entry.keyTypeIndex);
            ImGui::PopItemWidth();

            ImGui::SameLine();
            ImGui::TextUnformatted(":");
            ImGui::SameLine();

            // Value (editable)
            bModified |= std::forward<RenderFn>(renderFn)("##val", entry.valuePtr, entry.valueTypeIndex);


            // Delete button
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) {
                keysToDelete.push_back(entry.keyPtr);
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

                if (renderFn(label, elementPtr, elementTypeIndex)) {
                    bModified = true;
                }

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

    static bool renderHeader(const std::string              &name,
                             size_t                          size,
                             reflection::IContainerProperty *accessor,
                             void                           *containerPtr)
    {
        bool bModified = false;

        // Add element button
        ImGui::SameLine();
        if (ImGui::SmallButton("+")) {
            if (accessor->getCategory() == reflection::ContainerCategory::SequenceContainer) {
                accessor->addElement(containerPtr, nullptr);
                bModified = true;
            }
        }

        if (size > 0) {
            // Remove last element button
            ImGui::SameLine();
            if (ImGui::SmallButton("-")) {
                if (accessor->getCategory() == reflection::ContainerCategory::SequenceContainer) {
                    accessor->removeElement(containerPtr, size - 1);
                    bModified = true;
                }
            }

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
            strncpy(buf, str.c_str(), sizeof(buf) - 1);
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
};

} // namespace ya::editor
