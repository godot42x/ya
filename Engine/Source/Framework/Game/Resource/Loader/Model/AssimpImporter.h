#pragma once

#include "Resource/Core/Model/IModelImporter.h"

namespace ya::model_importer
{

class AssimpImporter final : public IModelImporter
{
  public:
    std::string_view  getName() const override;
    bool              supports(std::string_view filepath) const override;
    ImportedModelData import(const std::string& filepath) const override;
};

YA_RESOURCE_LOADER_API const IModelImporter& getAssimpImporter();

} // namespace ya::model_importer
