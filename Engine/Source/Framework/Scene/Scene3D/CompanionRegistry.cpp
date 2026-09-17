#include "CompanionRegistry.h"

#include <algorithm>

namespace ya
{

CompanionRegistry& CompanionRegistry::get()
{
    static CompanionRegistry instance;
    return instance;
}

void CompanionRegistry::declare(type_index_t        hostType,
                               CompanionSpec       spec,
                               FCompanionPresentFn present,
                               FCompanionWireFn    wire,
                               FCompanionSweepFn   sweep)
{
    if (hostType == 0) {
        return;
    }

    auto found = std::find_if(_entries.begin(), _entries.end(), [hostType](const FEntry& entry) {
        return entry.hostType == hostType;
    });

    // Re-declaring replaces in place: declaration order is part of the
    // contract for entities that carry several declared host types.
    if (found != _entries.end()) {
        found->spec    = std::move(spec);
        found->present = present;
        found->wire    = wire;
        found->sweep   = sweep;
        return;
    }

    FEntry entry;
    entry.hostType = hostType;
    entry.spec     = std::move(spec);
    entry.present  = present;
    entry.wire     = wire;
    entry.sweep    = sweep;
    _entries.push_back(std::move(entry));
}

void CompanionRegistry::undefine(type_index_t hostType)
{
    std::erase_if(_entries, [hostType](const FEntry& entry) {
        return entry.hostType == hostType;
    });
}

const CompanionSpec* CompanionRegistry::find(type_index_t hostType) const
{
    for (const FEntry& entry : _entries) {
        if (entry.hostType == hostType) {
            return &entry.spec;
        }
    }
    return nullptr;
}

void CompanionRegistry::clear()
{
    _entries.clear();
}

} // namespace ya
