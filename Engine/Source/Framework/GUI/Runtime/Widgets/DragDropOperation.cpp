#include "GUI/Widgets/DragDropOperation.h"

namespace ya
{

UIDragDropOperation::~UIDragDropOperation() = default;

UIDragDropOperationRef UIDragDropOperation::make(std::string payload,
                                                 std::string ghostLabel,
                                                 std::string typeId)
{
    auto operation = std::make_shared<UIDragDropOperation>();
    operation->payload = std::move(payload);
    operation->ghostLabel = ghostLabel.empty() ? operation->payload : std::move(ghostLabel);
    operation->typeId = typeId.empty() ? kTypeId : std::move(typeId);
    return operation;
}

} // namespace ya
