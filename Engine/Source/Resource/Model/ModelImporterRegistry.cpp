#include "Resource/Model/ModelImporterRegistry.h"

#include "Resource/Model/AssimpImporter.h"
#include "Resource/Model/GltfImporter.h"

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
