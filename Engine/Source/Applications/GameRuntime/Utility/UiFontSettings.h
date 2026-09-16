#pragma once

#include "Core/Api.h"
#include "Render/Resources/FontManager.h"

#include <string>
#include <string_view>
#include <vector>

namespace ya
{

struct IRender;

/// User-facing UI face selection: which catalog entry (FontManager::uiFontFaces)
/// the shell's DEFAULT text uses. This is APPLICATION policy, not framework
/// mechanism - FontManager owns "how to build a stack for a face", this owns
/// "which face, where it is persisted, and when it takes effect".
///
/// It exists so the editor, the workbench and a plain GUI app all ask the same
/// question instead of each resolving a path: the previous arrangement had the
/// GUI host take a fontPath and the game runtime compute one of its own, so
/// "the default font" was two independent strings that could disagree.
namespace ui_font_settings
{

/// Config document + key. One document ("ui") for UI-wide preferences, so a
/// later text-scale or density setting joins it instead of widening "editor".
inline constexpr const char* kConfigDocument = "ui";
inline constexpr const char* kKeyFace        = "font.face";

/// One selectable face, flattened for a settings combo so the UI layer does not
/// have to know FUiFontFace (or probe the filesystem).
struct FOption
{
    std::string id;
    std::string label;
    bool        bMonospace = false;
    /// False when the face is a catalog entry whose file is missing on this
    /// machine. Listed anyway so the set of choices is stable, and so the combo
    /// can say WHY it cannot be picked instead of silently omitting a face the
    /// user may have configured on another machine.
    bool bAvailable = false;
};

[[nodiscard]] YA_GAME_RUNTIME_API std::vector<FOption> availableFaces();

/// The configured face id, or the engine default when nothing is stored or the
/// stored id is unknown. A stale id falls back rather than stranding the shell,
/// and the stored value is left alone so re-installing a face brings it back.
[[nodiscard]] YA_GAME_RUNTIME_API std::string faceId();

/// Persist `faceId` and return the id that is now configured. An id that is not
/// a catalog entry resolves to the default, so a bad write cannot leave the
/// next launch unresolvable.
YA_GAME_RUNTIME_API std::string setFaceId(std::string_view faceId);

/// Rebuild the font stack for the configured face and publish the new metrics.
/// Returns false when no face could be resolved (the caller keeps whatever was
/// loaded, so the shell still has text).
YA_GAME_RUNTIME_API bool apply(IRender& render);

/// Resolve `faceId` against the catalog and apply it, persisting nothing.
/// Returning false leaves the previous stack in place.
YA_GAME_RUNTIME_API bool applyAndStore(IRender& render, std::string_view faceId);

} // namespace ui_font_settings

} // namespace ya
