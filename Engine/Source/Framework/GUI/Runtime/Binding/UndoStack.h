#pragma once

#include "Core/Api.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ya
{

/// One already-applied edit. `push` does not call `redo`; undo/redo later
/// invoke the captured closures. Closures may look up editor objects by
/// identity; the stack itself never stores ECS/Scene pointers.
struct FUndoCommand
{
    std::string           label;
    std::string           mergeKey;
    std::function<void()> undo;
    std::function<void()> redo;
    uint64_t              mergeGeneration = 0;
};

/// Identity undo history for GUI/editor models. Drag coalescing is an
/// explicit merge session (`beginMerge`/`endMerge`): only commands pushed
/// in the same session with the same `mergeKey` replace the top redo.
class YA_GUI_API UndoStack
{
  public:
    [[nodiscard]] bool push(FUndoCommand command);
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();
    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;
    [[nodiscard]] const std::string& undoLabel() const;
    [[nodiscard]] const std::string& redoLabel() const;
    [[nodiscard]] uint64_t revision() const { return _revision; }
    [[nodiscard]] size_t undoCount() const { return _undo.size(); }
    [[nodiscard]] size_t redoCount() const { return _redo.size(); }

    void beginMerge();
    void endMerge();
    void beginGroup(std::string label, std::string mergeKey = {});
    void endGroup();
    void clear();

  private:
    struct FOpenGroup
    {
        std::string                label;
        std::string                mergeKey;
        std::vector<FUndoCommand>  commands;
        uint64_t                   mergeGeneration = 0;
    };

    std::vector<FUndoCommand> _undo;
    std::vector<FUndoCommand> _redo;
    std::vector<FOpenGroup>   _groups;
    uint64_t                  _revision        = 0;
    uint64_t                  _mergeGeneration = 0;
    bool                      _bMergeOpen      = false;

    void bump();
    [[nodiscard]] bool tryMerge(FUndoCommand& command);
    void commit(FUndoCommand command);
    [[nodiscard]] static FUndoCommand compound(std::string label,
                                               std::string mergeKey,
                                               std::vector<FUndoCommand> children);
};

/// Nested RAII group: inner pushes become one undo step when the outermost
/// transaction exits, matching `ReactiveTransaction` depth.
class YA_GUI_API UndoTransaction
{
  public:
    UndoTransaction(UndoStack& stack, std::string label, std::string mergeKey = {});
    ~UndoTransaction();
    UndoTransaction(const UndoTransaction&)            = delete;
    UndoTransaction& operator=(const UndoTransaction&) = delete;

  private:
    UndoStack* _stack  = nullptr;
    bool       _bArmed = false;
};

} // namespace ya
