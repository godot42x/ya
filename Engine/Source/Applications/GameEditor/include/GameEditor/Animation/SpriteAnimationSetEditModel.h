#pragma once

#include "Core/Api.h"
#include "Core/Common/SpriteAnimationSet.h"

#include <cstdint>
#include <string>

namespace ya
{

/// Working copy of one `.yaanim.json` document. No GUI: tests drive the same
/// add/remove/rename/frame/grid/atlas operations the animation tab does.
/// `save` writes the file and replaces the document slot in place so an
/// already-acquired `SpriteAnimationSetRef` sees the new generation.
class YA_GAME_EDITOR_API SpriteAnimationSetEditModel
{
    SpriteAnimationSet _working;
    SpriteAnimationSet _saved;
    std::string        _assetPath;
    std::string        _error;
    int                _selectedClip = -1;
    bool               _bDirty       = false;

  public:
    /// Filesystem or VFS path → the key a component ref uses (`Content/...`
    /// or `Mount:tail`). Empty when the path cannot be resolved.
    [[nodiscard]] static std::string canonicalAssetPath(const std::string& path);

    /// Replace the working copy. Failure leaves a previously loaded document
    /// in place and sets `error()`.
    [[nodiscard]] bool loadFromText(const std::string& text, const std::string& assetPath);
    [[nodiscard]] bool loadFromPath(const std::string& path);

    [[nodiscard]] const SpriteAnimationSet& document() const { return _working; }
    [[nodiscard]] const std::string&        assetPath() const { return _assetPath; }
    [[nodiscard]] const std::string&        error() const { return _error; }
    [[nodiscard]] bool                      dirty() const { return _bDirty; }
    [[nodiscard]] int                       selectedClip() const { return _selectedClip; }

    void selectClip(int index);

    [[nodiscard]] bool addClip();
    [[nodiscard]] bool removeClip(int index);
    [[nodiscard]] bool renameClip(int index, const std::string& name);
    [[nodiscard]] bool appendFrame(int clipIndex, int32_t frame);
    [[nodiscard]] bool removeFrame(int clipIndex, int frameIndex);
    [[nodiscard]] bool moveFrame(int clipIndex, int frameIndex, int delta);
    [[nodiscard]] bool setFps(int clipIndex, float fps);
    [[nodiscard]] bool setLoop(int clipIndex, bool bLoop);
    [[nodiscard]] bool setColumns(int32_t columns);
    [[nodiscard]] bool setRows(int32_t rows);
    [[nodiscard]] bool setAtlas(std::string path);

    /// Sheet cell of `frame` (row-major, top-left), matching `frameRect`.
    [[nodiscard]] static bool frameToCell(int32_t frame, int32_t columns, int32_t rows, int32_t& outColumn,
                                          int32_t& outRow);

    /// Same rules as `parseSpriteAnimationSetJson`. Failure sets `error()`.
    [[nodiscard]] bool validate();

    [[nodiscard]] std::string serialize() const;

    void revert();

    /// Refuse when `validate()` fails. On success the file and the document
    /// slot (same path key, generation incremented) both match the working copy.
    [[nodiscard]] bool save();
};

} // namespace ya
