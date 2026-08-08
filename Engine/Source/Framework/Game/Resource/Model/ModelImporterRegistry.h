#pragma once

#include "Resource/Model/IModelImporter.h"

namespace ya::model_importer
{

const IModelImporter& getImporterForPath(const std::string& filepath);

} // namespace ya::model_importer
