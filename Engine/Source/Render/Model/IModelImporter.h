#pragma once

#include "Render/Model/ImportedModelData.h"

#include <string_view>


namespace ya
{
inline constexpr uint32_t INVALID_SKELETON_NODE_INDEX = std::numeric_limits<uint32_t>::max();
inline constexpr uint32_t INVALID_SKELETON_BONE_INDEX = std::numeric_limits<uint32_t>::max();
} // namespace ya
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
