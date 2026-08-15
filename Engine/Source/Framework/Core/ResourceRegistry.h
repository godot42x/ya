#pragma once

#include "Core/Api.h"
#include "Core/FName.h"
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace ya
{


struct RID
{
    uint64_t id    = 0;
    FName    owner = "None";

    bool isValid();
    bool operator==(const RID &other) const { return id == other.id; }
    bool operator!=(const RID &other) const { return id != other.id; }
    bool operator<(const RID &other) const { return id < other.id; }
};

} // namespace ya

namespace std
{

template <>
struct hash<ya::RID>
{
    std::size_t operator()(const ya::RID &rid) const
    {
        // Use a simple hash function for RID
        size_t hash = std::hash<uint64_t>{}(rid.id);
        hash ^= std::hash<ya::FName>{}(rid.owner);
        return hash;
    }
};


} // namespace std

namespace ya
{
/**
 * @brief Interface for resource caches
 * All resource caches should implement this interface to be managed by ResourceRegistry
 */
struct IResourceCache
{
    virtual ~IResourceCache() = default;

    /**
     * @brief Clear all cached resources
     * Called during cleanup in priority order (higher priority first)
     */
    virtual void clearCache() = 0;

    /**
     * @brief Get the name of this cache for debugging
     */
    virtual FName getCacheName() const = 0;

    // path or asset name
    // TODO: replace all to FName
    virtual void invalidate(const std::string &assetName) {}

    virtual bool isValid(const RID &rid) { return true; }
};

class YA_CORE_API ResourceRegistry
{
    struct CacheEntry
    {
        FName           name;
        IResourceCache *cache;
        int             priority;
    };

    std::unordered_map<FName, CacheEntry> _caches;
    std::vector<std::pair<FName, int>>    _deleteQueue;


  public:
    /// Defined in ResourceRegistry.cpp: the singleton must have a single
    /// strong symbol across dylibs (a header-inline static would be copied
    /// per image and split registered caches between modules).
    static ResourceRegistry &get();

    void registerCache(IResourceCache *cache, int priority = 0);
    void clearAll();
    // bool   empty() const { return _caches.empty(); }
    // size_t size() const { return _caches.size(); }
    IResourceCache *getCache(const FName &name);

  private:
    ResourceRegistry()  = default;
    ~ResourceRegistry() = default;

    ResourceRegistry(const ResourceRegistry &)            = delete;
    ResourceRegistry &operator=(const ResourceRegistry &) = delete;
};


inline bool RID::isValid()
{
    return id != 0 &&
           ResourceRegistry::get().getCache(owner) != nullptr &&
           ResourceRegistry::get().getCache(owner)->isValid(*this);
}

} // namespace ya
