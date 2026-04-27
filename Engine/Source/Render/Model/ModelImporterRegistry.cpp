#include "Render/Model/ModelImporterRegistry.h"

#include "Render/Model/AssimpImporter.h"
#include "Render/Model/GltfImporter.h"

namespace ya::model_importer
{

const IModelImporter& getImporterForPath(const std::string& filepath)
{
    const IModelImporter& gltfImporter = getGltfImporter();
    if (gltfImporter.supports(filepath)) {
        return gltfImporter;
    }

    return getAssimpImporter();
}

} // namespace ya::model_importer
