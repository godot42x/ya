#pragma once

// ============================================================================
// UIDocumentStore - the one table that answers "what is the Game UI document
// at this asset path".
//
// A Scene stores a *reference* to a UI document (SceneWidgetEntry::documentPath),
// never the document itself. Something has to turn that path into a live
// UIDocument instance, and the runtime host, the editor designer and the
// inspector must all get the *same* instance: the designer publishes an edit
// and the mounted tree has to see it on the next rebuild.
//
//   resolve(path)  path -> live document, reading `.yaui` from disk on first use
//   put(path, doc) publish an authoring edit (memory only, no file write)
//   find(path)     the live document already known for a path, or nullptr
//   save(path)     write the live document to disk
//   revision(path) counter that changes whenever the live document changes
//
// The owner is the application (ya::App), not the GUI library: a pure GUI host
// that never loads a document simply never calls into a store.
//
// `revision` exists because a consumer that CACHES work derived from a document
// (the editor's scene-UI preview builds a WidgetTree from mounted documents)
// must be able to tell "the document I mounted is the same one" from "it was
// edited". Comparing shared_ptr identity would almost work, but an edit that
// rewrites the document in place would be invisible to it; a counter is the
// honest answer and does not constrain how an edit is applied.
// ============================================================================

#include "Core/Api.h"

#include "GUI/Widgets/UIDocument.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ya
{

struct YA_GUI_API UIDocumentStore
{
    /// File extension of a Game UI document asset.
    static constexpr std::string_view kFileExtension = ".yaui.json";

    /// Live document for `path`, loading it from disk the first time. Returns
    /// nullptr (with a diagnostic) when the path is empty, missing or malformed.
    /// Later calls return the cached instance without touching the file.
    [[nodiscard]] std::shared_ptr<UIDocument> resolve(std::string_view path);

    /// Publish a live document for `path` (authoring edit). In-memory only;
    /// use `save` to persist. A null document discards the entry.
    void put(std::string_view path, std::shared_ptr<UIDocument> document);

    /// The live document already known for `path`, or nullptr. Never reads
    /// the file: display paths (inspector, hierarchy) must not trigger loads.
    [[nodiscard]] std::shared_ptr<UIDocument> find(std::string_view path) const;

    /// Write the live document for `path` to disk, creating parent
    /// directories. Returns false when nothing is known for the path or the
    /// write failed.
    [[nodiscard]] bool save(std::string_view path);

    /// Monotonic counter for `path`, bumped by every `resolve` that loads it and
    /// every `put`. 0 means "this store has never known the path". Consumers
    /// that cache derived state key on this instead of on document identity.
    [[nodiscard]] uint64_t revision(std::string_view path) const;

  private:
    std::unordered_map<std::string, std::shared_ptr<UIDocument>> _documents;
    std::unordered_map<std::string, uint64_t>                    _revisions;
    uint64_t                                                     _nextRevision = 1;
};

} // namespace ya
