#include "GUI/Widgets/DragDropOperation.h"

namespace ya
{

UIDragDropOperation::~UIDragDropOperation() = default;
UIStringDragDropOperation::~UIStringDragDropOperation() = default;

UIDragDropOperationRef UIStringDragDropOperation::make(std::string text,
                                                       std::string ghostLabel,
                                                       std::string typeId)
{
    auto operation = std::make_shared<UIStringDragDropOperation>();
    operation->typeId = typeId.empty() ? kTypeId : std::move(typeId);
    operation->text = std::move(text);
    operation->ghostLabel = ghostLabel.empty() ? operation->text : std::move(ghostLabel);
    return operation;
}

} // namespace ya
