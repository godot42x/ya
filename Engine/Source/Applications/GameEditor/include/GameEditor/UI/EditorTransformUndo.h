#pragma once

#include <glm/mat4x4.hpp>
#include <cstdint>
#include <vector>

namespace ya
{

class UndoStack;
struct Entity;
struct Scene;

struct FEditorTransformSnapshot
{
    uint64_t  entityUUID = 0;
    glm::mat4 world{1.0f};
};

std::vector<FEditorTransformSnapshot> captureEditorTransformSelection(const std::vector<Entity*>& selections);

bool pushEditorTransformUndo(UndoStack& undo,
                             Scene* scene,
                             std::vector<FEditorTransformSnapshot> before,
                             std::vector<FEditorTransformSnapshot> after);

} // namespace ya
