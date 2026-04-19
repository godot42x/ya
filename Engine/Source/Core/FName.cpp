#include "FName.h"

namespace ya
{

NameRegistry& NameRegistry::get()
{
    static NameRegistry* instance = new NameRegistry();
    return *instance;
}

index_t NameRegistry::indexing(std::string_view name)
{
    {
        std::shared_lock<std::shared_mutex> lock(_mutex);
        if (auto it = _str2Index.find(name); it != _str2Index.end()) {
            return it->second;
        }
    }

    std::unique_lock<std::shared_mutex> lock(_mutex);
    if (auto it = _str2Index.find(name); it != _str2Index.end()) {
        return it->second;
    }

    _indexToString.emplace_back(name);
    const index_t index = static_cast<index_t>(_indexToString.size());
    _str2Index.emplace(_indexToString.back(), index);
    return index;
}

std::string_view NameRegistry::view(index_t index) const
{
    if (index == 0) {
        return INVALID_FNAME_TEXT;
    }

    std::shared_lock<std::shared_mutex> lock(_mutex);
    const size_t storageIndex = static_cast<size_t>(index - 1);
    if (storageIndex >= _indexToString.size()) {
        return INVALID_FNAME_TEXT;
    }

    return _indexToString[storageIndex];
}

} // namespace ya