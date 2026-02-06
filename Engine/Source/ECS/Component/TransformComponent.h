#pragma once
#include "Core/Math/Math.h"
#include "Core/Reflection/Reflection.h"
#include "ECS/Component.h"

#include <functional>

namespace ya
{

/**
 * @brief TransformComponent - Pure data container for transform
 *
 * Design Philosophy (Data-Oriented):
 * - Stores local transform data (position/rotation/scale) - USER MODIFIABLE
 * - Caches computed matrices (_localMatrix/_worldMatrix) - READ ONLY, computed by TransformSystem
 * - Uses dirty flags to mark when recomputation is needed
 * - NO computation logic in Component - all matrix calculations done by TransformSystem
 *
 * Workflow:
 * 1. User modifies position/rotation/scale -> marks dirty
 * 2. TransformSystem detects dirty flag -> recomputes matrices
 * 3. Rendering/Physics reads cached matrices (always up-to-date after System update)
 *
 * Immediate Feedback (Gizmo/Details):
 * - When user drags Gizmo or edits in Details panel
 * - Call TransformSystem::setWorldTransform() to immediately compute matrices
 * - This ensures instant visual feedback without waiting for next frame
 */
struct TransformComponent : public IComponent
{
    YA_REFLECT_BEGIN(TransformComponent)
    YA_REFLECT_FIELD(_position)
    YA_REFLECT_FIELD(_rotation)
    YA_REFLECT_FIELD(_scale)
    YA_REFLECT_END()

    // === USER DATA (modifiable) ===
    // Local transform data (relative to parent)
    glm::vec3 _position = {0.0f, 0.0f, 0.0f};
    glm::vec3 _rotation = {0.0f, 0.0f, 0.0f}; // Euler angles in degrees
    glm::vec3 _scale    = {1.0f, 1.0f, 1.0f};

    // === CACHED DATA (computed by TransformSystem, READ ONLY) ===
    glm::mat4 _localMatrix = glm::mat4(1.0f); // Computed from position/rotation/scale
    glm::mat4 _worldMatrix = glm::mat4(1.0f); // Computed from parent world * local

    // === DIRTY FLAGS ===
    bool _localDirty = true; // Need to recompute _localMatrix
    bool _worldDirty = true; // Need to recompute _worldMatrix

    // === CACHED PARENT TC (maintained by Node3D) ===
    TransformComponent *_cachedParentTC = nullptr;

    // === CALLBACK for dirty propagation (set by Node3D) ===
    std::function<void()> _onChildrenDirtyCallback;

    // === BATCH UPDATE MODE ===
    int _batchUpdateCount = 0;

  public:
    // ========================================================================
    // Batch Update API
    // ========================================================================
    void beginBatchUpdate() { ++_batchUpdateCount; }
    void endBatchUpdate()
    {
        if (_batchUpdateCount > 0) {
            --_batchUpdateCount;
            if (_batchUpdateCount == 0 && _worldDirty) {
                notifyChildrenDirty();
            }
        }
    }
    [[nodiscard]] bool isInBatchUpdate() const { return _batchUpdateCount > 0; }

    // ========================================================================
    // Cached Parent TC Management (called by Node3D)
    // ========================================================================
    void                              setCachedParentTC(TransformComponent *parentTC) { _cachedParentTC = parentTC; }
    [[nodiscard]] TransformComponent *getCachedParentTC() const { return _cachedParentTC; }

    // ========================================================================
    // Dirty Callback Management (called by Node3D)
    // ========================================================================
    void setOnChildrenDirtyCallback(std::function<void()> callback)
    {
        _onChildrenDirtyCallback = std::move(callback);
    }

    // ========================================================================
    // Local Transform Setters (mark dirty only, no computation)
    // ========================================================================

    [[nodiscard]] const glm::vec3 &getPosition() const { return _position; }
    void                           setPosition(const glm::vec3 &position)
    {
        _position   = position;
        _localDirty = true;
        _worldDirty = true;
        if (!isInBatchUpdate()) {
            notifyChildrenDirty();
        }
    }

    [[nodiscard]] const glm::vec3 &getRotation() const { return _rotation; }
    void                           setRotation(const glm::vec3 &rotation)
    {
        _rotation   = rotation;
        _localDirty = true;
        _worldDirty = true;
        if (!isInBatchUpdate()) {
            notifyChildrenDirty();
        }
    }

    [[nodiscard]] const glm::vec3 &getScale() const { return _scale; }
    void                           setScale(const glm::vec3 &scale)
    {
        _scale      = scale;
        _localDirty = true;
        _worldDirty = true;
        if (!isInBatchUpdate()) {
            notifyChildrenDirty();
        }
    }

    // ========================================================================
    // Matrix Getters (READ ONLY - computed by TransformSystem)
    // ========================================================================

    [[nodiscard]] const glm::mat4 &getLocalMatrix() const { return _localMatrix; }
    [[nodiscard]] const glm::mat4 &getWorldMatrix() const { return _worldMatrix; }

    // Legacy alias
    [[nodiscard]] const glm::mat4 &getLocalTransform() const { return _localMatrix; }
    [[nodiscard]] const glm::mat4 &getTransform() const { return _worldMatrix; }

    // ========================================================================
    // Dirty Flag Management
    // ========================================================================

    [[nodiscard]] bool isLocalDirty() const { return _localDirty; }
    [[nodiscard]] bool isWorldDirty() const { return _worldDirty; }

    void markLocalDirty()
    {
        _localDirty = true;
        _worldDirty = true;
    }

    void markWorldDirty()
    {
        _worldDirty = true;
    }

    void markDirty() { markLocalDirty(); }

    // Called by TransformSystem after computing matrices
    void clearLocalDirty() { _localDirty = false; }
    void clearWorldDirty() { _worldDirty = false; }

    // ========================================================================
    // Convenience Methods
    // ========================================================================

    [[nodiscard]] glm::vec3 getWorldPosition() const
    {
        return glm::vec3(_worldMatrix[3]);
    }
    glm::vec3 getWorldRotation() const
    {
        auto rotation = glm::quat(_worldMatrix);
        return glm::eulerAngles(rotation);
    }

    [[nodiscard]] glm::vec3 getLocalForward() const
    {
        if constexpr (FMath::Vector::IsRightHanded) {
            return _localMatrix * glm::vec4(FMath::Vector::WorldForward, 0.0f);
        }
        else {
            return _localMatrix * -glm::vec4(FMath::Vector::WorldForward, 0.0f);
        }
    }

    [[nodiscard]] glm::vec3 getForward() const
    {
        if constexpr (FMath::Vector::IsRightHanded) {
            return _worldMatrix * glm::vec4(FMath::Vector::WorldForward, 0.0f);
        }
        else {
            return _worldMatrix * -glm::vec4(FMath::Vector::WorldForward, 0.0f);
        }
    }

    // ========================================================================
    // Serialization
    // ========================================================================

    void onPostSerialize() override
    {
        _localDirty = true;
        _worldDirty = true;
    }

    // ========================================================================
    // Dirty Propagation
    // ========================================================================

    void notifyChildrenDirty()
    {
        if (_onChildrenDirtyCallback) {
            _onChildrenDirtyCallback();
        }
    }

    // Legacy alias
    void propagateWorldDirtyToChildren() { notifyChildrenDirty(); }
};

} // namespace ya