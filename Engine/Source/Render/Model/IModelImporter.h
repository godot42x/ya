#pragma once

#include "Render/Model/ImportedModelData.h"

#include <string_view>

namespace ya::model_importer
{

struct IModelImporter
{
    virtual ~IModelImporter() = default;

    virtual std::string_view  getName() const                           = 0;
    virtual bool              supports(std::string_view filepath) const = 0;
    virtual ImportedModelData import(const std::string& filepath) const = 0;
};

} // namespace ya::model_importer
