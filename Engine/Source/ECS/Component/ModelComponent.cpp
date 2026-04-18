#include "ModelComponent.h"

#include "Render/Material/MaterialFactory.h"

namespace ya
{

ModelComponent::~ModelComponent()
{
    // Release shared materials back to MaterialFactory on destruction.
    // This handles the scene-reload path where ModelInstantiationSystem
    // does NOT get a chance to call cleanupChildEntities() before the scene
    // (and therefore the component) is destroyed.
    if (auto* factory = MaterialFactory::get()) {
        for (auto& [matIndex, material] : _cachedMaterials) {
            if (material) {
                factory->destroyMaterial(material);
            }
        }
    }
    _cachedMaterials.clear();
}

} // namespace ya
