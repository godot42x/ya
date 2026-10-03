#pragma once

#include "Core/Common/DocumentAssetRef.h"
#include "Core/Reflection/Reflection.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ya
{

// One named run of sheet frames. `frames` are sheet indices (row-major from
// the top-left of the image), so a walk cycle may revisit a frame: {0, 1, 2, 1}.
// Field names match the clips embedded in SpriteAnimationComponent so a
// document and a component describe the same clip.
struct YA_CORE_API SpriteAnimationClip
{
    YA_REFLECT_BEGIN(SpriteAnimationClip)
    YA_REFLECT_FIELD(name)
    YA_REFLECT_FIELD(frames)
    YA_REFLECT_FIELD(fps)
    YA_REFLECT_FIELD(bLoop)
    YA_REFLECT_END()

    std::string          name;
    std::vector<int32_t> frames;
    float                fps   = 8.0f;
    bool                 bLoop = true;
};

// Shared sprite-sheet animation document (.yaanim.json). Several entities
// point at one file; the component only chooses which clip is playing.
//
// The image is cut into a uniform `columns` x `rows` grid. A frame index is
// `row * columns + column`, counted from the top-left (the image-space
// convention of `Sprite2DComponent::uvRect`).
struct YA_CORE_API SpriteAnimationSet
{
    YA_REFLECT_BEGIN(SpriteAnimationSet)
    YA_REFLECT_FIELD(columns)
    YA_REFLECT_FIELD(rows)
    YA_REFLECT_FIELD(clips)
    YA_REFLECT_END()

    int32_t                          columns = 1;
    int32_t                          rows    = 1;
    std::vector<SpriteAnimationClip> clips;

    [[nodiscard]] bool isValid() const { return columns > 0 && rows > 0; }

    /// Image-space window of one sheet frame: (u0, v0, u1, v1). A frame
    /// outside the grid has no window (zero rect).
    [[nodiscard]] glm::vec4 frameRect(int32_t frame) const;

    /// Clip with this name, or nullptr. The pointer addresses `clips`.
    [[nodiscard]] const SpriteAnimationClip* findClip(const std::string& name) const;
};

// Path reference to a .yaanim.json file. Only the path is serialized; the
// document is parsed synchronously on first request and shared between refs
// naming the same file. A ref with a path but no parsed document failed to load.
struct YA_CORE_API SpriteAnimationSetRef : public DocumentAssetRef<SpriteAnimationSet>
{
    YA_REFLECT_BEGIN(SpriteAnimationSetRef, AssetRefBase)
    YA_REFLECT_END()
    YA_REFLECT_COPIES_AS_VALUE()

    SpriteAnimationSetRef() = default;
    explicit SpriteAnimationSetRef(const std::string& path) : DocumentAssetRef<SpriteAnimationSet>(path) {}
};

// Parses one .yaanim.json document through reflection, then checks the grid
// and every clip. Returns nullptr with outError set when the document is
// malformed or breaks those rules.
[[nodiscard]] YA_CORE_API std::shared_ptr<SpriteAnimationSet> parseSpriteAnimationSetJson(const std::string& text,
                                                                                         std::string&       outError);

// Reflection JSON of one document, for the editor save path.
[[nodiscard]] YA_CORE_API std::string serializeSpriteAnimationSetJson(const SpriteAnimationSet& set);

} // namespace ya
