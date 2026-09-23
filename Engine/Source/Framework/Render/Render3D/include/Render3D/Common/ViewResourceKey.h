#pragma once

#include "Render3D/Common/RenderFeatures.h"
#include "Render3D/Common/SceneViewDesc.h"
#include "RHI/RenderDefines.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace ya
{

/// Identity of one View's own render resources.
///
/// A View's attachments are that View's: its identity, the extent it declared,
/// the formats the pipeline resolved for it, and the feature policy it renders
/// with. Two Views in one tick are two keys, so publishing B cannot overwrite
/// A. The key is what tells a reader whether two records describe the *same*
/// resources -- a View that changed size has different resources, not a
/// rewritten one.
struct ViewResourceKey
{
    SceneViewId viewId      = 0;
    Extent2D    extent{};
    EFormat::T  colorFormat = EFormat::Undefined;
    EFormat::T  depthFormat = EFormat::Undefined;
    uint32_t    featureMask = 0;

    [[nodiscard]] bool operator==(const ViewResourceKey&) const = default;
};

/// The View resources a pipeline published, keyed by `ViewResourceKey`.
///
/// Not a cache and not "the current View": it is the list of what the last
/// recorded tick left behind, one entry per View. A View owns exactly one live
/// entry -- republishing it at a new extent or with new formats replaces that
/// View's resources, because it is still the same View. Entries stay in publish
/// order, so a reader that wants a representative can take the first while an
/// identity-carrying lookup answers about that View alone.
///
/// The table holds the only owner of those resources, so a View that is no
/// longer recorded has to leave this table for its attachments to be released;
/// `retainIf` is how the caller states which Views still exist.
template <typename Resources>
class ViewResourceTable
{
  public:
    struct Entry
    {
        ViewResourceKey key{};
        Resources       resources{};
    };

    Resources& publish(const ViewResourceKey& key, Resources resources)
    {
        for (Entry& entry : _entries) {
            if (entry.key.viewId == key.viewId) {
                entry.key       = key;
                entry.resources = std::move(resources);
                return entry.resources;
            }
        }
        _entries.push_back(Entry{.key = key, .resources = std::move(resources)});
        return _entries.back().resources;
    }

    [[nodiscard]] const Entry* findEntry(SceneViewId viewId) const
    {
        for (const Entry& entry : _entries) {
            if (entry.key.viewId == viewId) {
                return &entry;
            }
        }
        return nullptr;
    }

    /// The resources of the named View, or nullptr when this table holds none.
    [[nodiscard]] const Resources* findForView(SceneViewId viewId) const
    {
        const Entry* entry = findEntry(viewId);
        return entry ? &entry->resources : nullptr;
    }

    /// Non-null only when a View *with these exact facts* is recorded, so a
    /// caller can ask "are these still the resources I saw?" across a resize.
    [[nodiscard]] const Resources* find(const ViewResourceKey& key) const
    {
        const Entry* entry = findEntry(key.viewId);
        return entry && entry->key == key ? &entry->resources : nullptr;
    }

    [[nodiscard]] const std::vector<Entry>& entries() const { return _entries; }
    [[nodiscard]] std::size_t               size() const { return _entries.size(); }
    [[nodiscard]] bool                      empty() const { return _entries.empty(); }

    /// Keep only the entries whose View `keepView` accepts, and release the
    /// rest.
    ///
    /// The criterion is the caller's, not this table's: whether a View still
    /// exists is a fact about the tick's declarations, and a table that answered
    /// it for itself could only guess. An entry the predicate accepts keeps its
    /// order, its key and its resources, so dropping one View never disturbs
    /// another.
    template <typename KeepView>
    void retainIf(KeepView&& keepView)
    {
        std::erase_if(_entries, [&keepView](const Entry& entry) { return !keepView(entry.key.viewId); });
    }

    void clear() { _entries.clear(); }

  private:
    std::vector<Entry> _entries;
};

} // namespace ya
