
#pragma once

#include "Core/Base.h"

namespace ya
{
struct IAsset
{
    virtual ~IAsset() = default;

    [[nodiscard]] virtual const std::string &getName() const     = 0;
    [[nodiscard]] virtual const std::string &getFilepath() const = 0;
};


struct AssetRegistry
{
    std::unordered_map<FName, std::function<stdptr<IAsset>()>> _registry;

    void                  init() {}
    static AssetRegistry &get();

    void registerAssetType(FName type, std::function<stdptr<IAsset>()> factoryFunction);

    stdptr<IAsset> createAssetFromExtension(FName type) const
    {
        auto it = _registry.find(type);
        if (it != _registry.end())
        {
            return it->second();
        }
        return nullptr;
    }
};

} // namespace ya