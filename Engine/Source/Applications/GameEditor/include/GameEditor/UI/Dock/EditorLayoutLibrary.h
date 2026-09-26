#pragma once

// ============================================================================
// EditorLayoutLibrary - dock layout documents as data, not compiled-in JSON.
//
// Two roots, both supplied by the product that owns the editor:
//   defaults   shipped, read-only documents
//   overrides  this machine's arrangement, wins when a document of the same
//              name exists
//
// The split is what lets a developer keep a local split without editing a
// shipped file, and it is why the roots are a parameter rather than a
// constant: the game editor and a standalone GUI app ship different
// workspaces, and the library must not prefer either.
//
// One document per concern:
//   WindowRoot        the window page well
//   Level/UI/Material/Script   each major editor's own tool dock
//   Workspace         the user's current arrangement (override only)
// ============================================================================

#include "Core/Api.h"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

namespace ya
{

struct FEditorLayoutRoots
{
    std::string defaults;
    std::string overrides;
};

class YA_GAME_EDITOR_API EditorLayoutLibrary
{
  public:
    EditorLayoutLibrary() = default;
    explicit EditorLayoutLibrary(FEditorLayoutRoots roots)
        : _roots(std::move(roots))
    {
    }

    /// The editor profile's documents. Bound once by the product that hosts the
    /// editor; until then the engine's own tree is used, so a tool or a test
    /// that never binds still resolves the shipped layouts.
    static EditorLayoutLibrary& get();

    void bindRoots(FEditorLayoutRoots roots);
    [[nodiscard]] const FEditorLayoutRoots& roots() const { return _roots; }

    /// The local override when this machine has one, otherwise the shipped
    /// default. An EMPTY OBJECT when neither exists (or the file is malformed):
    /// "no layout" reads as "places no panels", which is how the dock layer
    /// already treats a document with nothing in it. Never null -- callers
    /// query keys on the result, and a null document throws instead of falling
    /// back.
    [[nodiscard]] nlohmann::json document(std::string_view name) const;

    /// Write the local arrangement. Returns false when the override root is
    /// unset or the write fails; the caller keeps its in-memory state either
    /// way, so a failed persist is a lost convenience, not lost work.
    bool saveOverride(std::string_view name, const nlohmann::json& document) const;

  private:
    [[nodiscard]] nlohmann::json readFrom(const std::string& root, std::string_view name) const;

    FEditorLayoutRoots _roots;
};

/// Shipped document names. The per-root ones are the file stems under the
/// defaults root, so renaming one is a rename on both sides.
inline constexpr std::string_view kEditorLayoutWindowRoot = "WindowRoot";
inline constexpr std::string_view kEditorLayoutWorkspace  = "Workspace";

} // namespace ya
