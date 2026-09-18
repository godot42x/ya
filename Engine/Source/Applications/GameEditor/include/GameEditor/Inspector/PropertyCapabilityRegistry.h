#pragma once

#include "GameEditor/UI/Dialogs/EditorAssetPicker.h"
#include "Core/TypeIndex.h"

#include <optional>
#include <unordered_map>

namespace ya
{

/// Editor capabilities associated with reflected value types. Reflection only
/// reports the storage type; this registry maps it to editor affordances.
class PropertyCapabilityRegistry final
{
  public:
    static PropertyCapabilityRegistry& instance();

    void registerAssetRef(type_index_t type, EEditorAssetPickerKind kind);
    [[nodiscard]] std::optional<EEditorAssetPickerKind> assetRefKind(type_index_t type) const;

  private:
    std::unordered_map<type_index_t, EEditorAssetPickerKind> _assetRefs;
};

void registerBuiltinPropertyCapabilities();

} // namespace ya
