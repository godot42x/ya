#pragma once

#include "Core/Api.h"
#include "Core/TypeIndex.h"
#include "GUI/Widgets/UIElement.h"
#include "GameEditor/UI/Dialogs/EditorAssetPicker.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ya
{

struct EditorLayer;
class UndoStack;
struct WidgetTree;

/// Opens a script file. Same shape `EditorLuaScriptSection` already takes.
using EditorScriptPicker = std::function<void(std::string currentPath, std::function<void(std::string)> onPicked)>;

/// One custom inspector body plus the hooks the tab calls each frame.
struct EditorInspectorSectionHost
{
    std::shared_ptr<UIElement>        widget;
    std::function<void(WidgetTree&)>  sync;
    std::function<void(float)>        tick;
};

struct EditorInspectorSectionRequest
{
    std::string                 name;
    EditorLayer*                layer       = nullptr;
    uint64_t                    entityUuid  = 0;
    UndoStack*                  undo        = nullptr;
    bool                        bReadOnly   = false;
    std::function<void()>       onMutated;
    EditorAssetPickerCallback   assetPicker;
    EditorRevealAssetCallback   revealAsset;
    EditorScriptPicker          scriptPicker;
};

/// Component type → custom inspector section. Unregistered types use the
/// generic property section. A registered type with one instance uses its
/// factory; with several instances it either falls back to that generic
/// section or skips the body (scripts cannot be projected).
class YA_GAME_EDITOR_API EditorComponentSectionRegistry
{
  public:
    enum class EMultiInstance
    {
        AutoProperty,
        Skip,
    };
    enum class EChoice
    {
        Custom,
        AutoProperty,
        Skip,
    };

    using Factory = std::function<EditorInspectorSectionHost(const EditorInspectorSectionRequest&)>;

    struct Entry
    {
        type_index_t   type  = 0;
        EMultiInstance multi = EMultiInstance::AutoProperty;
        Factory        make;
    };

    static EditorComponentSectionRegistry& instance();

    void add(type_index_t type, EMultiInstance multi, Factory factory);
    [[nodiscard]] const Entry* find(type_index_t type) const;
    [[nodiscard]] EChoice choose(type_index_t type, size_t instanceCount) const;

  private:
    std::vector<Entry> _entries;
};

void registerBuiltinInspectorSections();

} // namespace ya
