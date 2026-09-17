#pragma once

#include "Core/Api.h"
#include "GUI/Widgets/UIFrameSnapshot.h"

#include <nlohmann/json.hpp>

#include <cstdint>

namespace ya
{

/// Deterministic structural representation of an immutable frame packet.
/// Resource pointers are deliberately omitted: the dump captures the
/// cross-path visual contract (order, geometry, clip, color, text), not a
/// process-local GPU allocation identity.
[[nodiscard]] YA_GUI_API nlohmann::json dumpUIFrameSnapshot(const UIFrameSnapshot& snapshot);

/// Stable FNV-1a digest of dumpUIFrameSnapshot(snapshot). Intended for
/// automation assertions; callers that need diagnostics should persist the
/// JSON dump alongside the digest.
[[nodiscard]] YA_GUI_API uint64_t digestUIFrameSnapshot(const UIFrameSnapshot& snapshot);

/// Stable digest of semantic paint order only: item kind, clipped state,
/// color and text. It intentionally excludes resolved geometry so the same
/// logical page can be compared between a windowed real-font run and a
/// headless synthetic-font run.
[[nodiscard]] YA_GUI_API uint64_t semanticDigestUIFrameSnapshot(const UIFrameSnapshot& snapshot);

} // namespace ya
