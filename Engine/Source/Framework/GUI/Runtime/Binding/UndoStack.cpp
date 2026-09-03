#include "UndoStack.h"

#include <memory>
#include <utility>

namespace ya
{

namespace
{

const std::string kEmptyLabel;

} // namespace

void UndoStack::bump()
{
    ++_revision;
}

FUndoCommand UndoStack::compound(std::string label,
                                 std::string mergeKey,
                                 std::vector<FUndoCommand> children)
{
    FUndoCommand command;
    command.label     = std::move(label);
    command.mergeKey  = std::move(mergeKey);
    auto steps        = std::make_shared<std::vector<FUndoCommand>>(std::move(children));
    command.undo      = [steps]() {
        for (auto it = steps->rbegin(); it != steps->rend(); ++it) {
            if (it->undo) {
                it->undo();
            }
        }
    };
    command.redo = [steps]() {
        for (const FUndoCommand& step : *steps) {
            if (step.redo) {
                step.redo();
            }
        }
    };
    return command;
}

bool UndoStack::tryMerge(FUndoCommand& command)
{
    if (!_bMergeOpen || command.mergeKey.empty() || _undo.empty()) {
        return false;
    }
    FUndoCommand& top = _undo.back();
    if (top.mergeKey != command.mergeKey || top.mergeGeneration != _mergeGeneration) {
        return false;
    }
    top.redo  = std::move(command.redo);
    top.label = std::move(command.label);
    bump();
    return true;
}

void UndoStack::commit(FUndoCommand command)
{
    command.mergeGeneration = _mergeGeneration;
    if (tryMerge(command)) {
        return;
    }
    _undo.push_back(std::move(command));
    _redo.clear();
    bump();
}

bool UndoStack::push(FUndoCommand command)
{
    if (!command.undo || !command.redo) {
        return false;
    }
    if (!_groups.empty()) {
        _groups.back().commands.push_back(std::move(command));
        return true;
    }
    commit(std::move(command));
    return true;
}

bool UndoStack::undo()
{
    if (!canUndo()) {
        return false;
    }
    endMerge();
    FUndoCommand command = std::move(_undo.back());
    _undo.pop_back();
    command.undo();
    _redo.push_back(std::move(command));
    bump();
    return true;
}

bool UndoStack::redo()
{
    if (!canRedo()) {
        return false;
    }
    endMerge();
    FUndoCommand command = std::move(_redo.back());
    _redo.pop_back();
    command.redo();
    _undo.push_back(std::move(command));
    bump();
    return true;
}

bool UndoStack::canUndo() const
{
    return !_undo.empty() && _groups.empty();
}

bool UndoStack::canRedo() const
{
    return !_redo.empty() && _groups.empty();
}

const std::string& UndoStack::undoLabel() const
{
    return _undo.empty() ? kEmptyLabel : _undo.back().label;
}

const std::string& UndoStack::redoLabel() const
{
    return _redo.empty() ? kEmptyLabel : _redo.back().label;
}

void UndoStack::beginMerge()
{
    ++_mergeGeneration;
    _bMergeOpen = true;
}

void UndoStack::endMerge()
{
    _bMergeOpen = false;
}

void UndoStack::beginGroup(std::string label, std::string mergeKey)
{
    FOpenGroup group;
    group.label            = std::move(label);
    group.mergeKey         = std::move(mergeKey);
    group.mergeGeneration  = _mergeGeneration;
    _groups.push_back(std::move(group));
}

void UndoStack::endGroup()
{
    if (_groups.empty()) {
        return;
    }
    FOpenGroup group = std::move(_groups.back());
    _groups.pop_back();
    if (group.commands.empty()) {
        return;
    }
    FUndoCommand command = compound(std::move(group.label),
                                    std::move(group.mergeKey),
                                    std::move(group.commands));
    command.mergeGeneration = group.mergeGeneration;
    if (!_groups.empty()) {
        _groups.back().commands.push_back(std::move(command));
        return;
    }
    commit(std::move(command));
}

void UndoStack::clear()
{
    _undo.clear();
    _redo.clear();
    _groups.clear();
    _bMergeOpen = false;
    bump();
}

UndoTransaction::UndoTransaction(UndoStack& stack, std::string label, std::string mergeKey)
    : _stack(&stack), _bArmed(true)
{
    _stack->beginGroup(std::move(label), std::move(mergeKey));
}

UndoTransaction::~UndoTransaction()
{
    if (_bArmed && _stack) {
        _stack->endGroup();
    }
}

} // namespace ya
