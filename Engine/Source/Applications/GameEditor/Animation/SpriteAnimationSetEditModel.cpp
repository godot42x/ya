#include "GameEditor/Animation/SpriteAnimationSetEditModel.h"

#include "Core/Common/AssetRef.h"
#include "Core/Common/AssetTypeRegistry.h"
#include "Core/Log.h"
#include "Core/System/VirtualFileSystem.h"

#include <filesystem>
#include <utility>

namespace ya
{
namespace
{

[[nodiscard]] bool isMountShaped(const std::string& path)
{
    return path.find(':') != std::string::npos || path.starts_with("Content/") || path.starts_with("Engine:");
}

[[nodiscard]] bool fileExists(VirtualFileSystem& vfs, const std::string& path)
{
    return !path.empty() && !std::filesystem::path(path).is_absolute() && vfs.isFileExists(path);
}

} // namespace

std::string SpriteAnimationSetEditModel::canonicalAssetPath(const std::string& path)
{
    if (path.empty()) {
        return {};
    }
    VirtualFileSystem* vfs = VirtualFileSystem::get();
    const std::string  direct = canonicalizeAssetPath(path);
    if (!vfs) {
        return direct;
    }
    if (isMountShaped(direct) && fileExists(*vfs, direct)) {
        return direct;
    }

    std::error_code ec;
    const auto      absolute = std::filesystem::absolute(std::filesystem::path(path), ec);
    if (!ec) {
        const std::string mounted = canonicalizeAssetPath(vfs->toVfsPath(absolute.generic_string()));
        if (fileExists(*vfs, mounted)) {
            return mounted;
        }
    }
    return direct;
}

bool SpriteAnimationSetEditModel::loadFromText(const std::string& text, const std::string& assetPath)
{
    std::string error;
    const auto  parsed = parseSpriteAnimationSetJson(text, error);
    if (!parsed) {
        _error = std::move(error);
        return false;
    }
    _working      = *parsed;
    _saved        = _working;
    _assetPath    = assetPath;
    _error.clear();
    _bDirty       = false;
    _selectedClip = _working.clips.empty() ? -1 : 0;
    return true;
}

bool SpriteAnimationSetEditModel::loadFromPath(const std::string& path)
{
    VirtualFileSystem* vfs = VirtualFileSystem::get();
    if (!vfs) {
        _error = "virtual file system is not available";
        return false;
    }
    const std::string key = canonicalAssetPath(path);
    if (key.empty() || std::filesystem::path(key).is_absolute() || !vfs->isFileExists(key)) {
        _error = "animation set file was not found";
        return false;
    }
    std::string text;
    if (!vfs->readFileToString(key, text)) {
        _error = "failed to read animation set";
        return false;
    }
    return loadFromText(text, key);
}

void SpriteAnimationSetEditModel::selectClip(int index)
{
    if (index < 0 || index >= static_cast<int>(_working.clips.size())) {
        _selectedClip = _working.clips.empty() ? -1 : 0;
        return;
    }
    _selectedClip = index;
}

bool SpriteAnimationSetEditModel::addClip()
{
    std::string name = "clip";
    int         suffix = 2;
    while (_working.findClip(name) != nullptr) {
        name = "clip" + std::to_string(suffix);
        ++suffix;
    }
    SpriteAnimationClip clip;
    clip.name   = std::move(name);
    clip.frames = {0};
    clip.fps    = 8.0f;
    clip.bLoop  = true;
    _working.clips.push_back(std::move(clip));
    _selectedClip = static_cast<int>(_working.clips.size()) - 1;
    _bDirty       = true;
    (void)validate();
    return true;
}

bool SpriteAnimationSetEditModel::removeClip(int index)
{
    if (index < 0 || index >= static_cast<int>(_working.clips.size())) {
        _error = "no clip selected";
        return false;
    }
    _working.clips.erase(_working.clips.begin() + index);
    if (_working.clips.empty()) {
        _selectedClip = -1;
    }
    else if (_selectedClip >= static_cast<int>(_working.clips.size())) {
        _selectedClip = static_cast<int>(_working.clips.size()) - 1;
    }
    else if (_selectedClip > index) {
        --_selectedClip;
    }
    _bDirty = true;
    (void)validate();
    return true;
}

bool SpriteAnimationSetEditModel::renameClip(int index, const std::string& name)
{
    if (index < 0 || index >= static_cast<int>(_working.clips.size())) {
        _error = "no clip selected";
        return false;
    }
    if (name.empty()) {
        _error = "clip name must be non-empty";
        return false;
    }
    for (int other = 0; other < static_cast<int>(_working.clips.size()); ++other) {
        if (other != index && _working.clips[static_cast<size_t>(other)].name == name) {
            _error = "duplicate clip name";
            return false;
        }
    }
    if (_working.clips[static_cast<size_t>(index)].name == name) {
        return true;
    }
    _working.clips[static_cast<size_t>(index)].name = name;
    _bDirty                                         = true;
    (void)validate();
    return true;
}

bool SpriteAnimationSetEditModel::appendFrame(int clipIndex, int32_t frame)
{
    if (clipIndex < 0 || clipIndex >= static_cast<int>(_working.clips.size())) {
        _error = "no clip selected";
        return false;
    }
    _working.clips[static_cast<size_t>(clipIndex)].frames.push_back(frame);
    _bDirty = true;
    (void)validate();
    return true;
}

bool SpriteAnimationSetEditModel::removeFrame(int clipIndex, int frameIndex)
{
    if (clipIndex < 0 || clipIndex >= static_cast<int>(_working.clips.size())) {
        _error = "no clip selected";
        return false;
    }
    auto& frames = _working.clips[static_cast<size_t>(clipIndex)].frames;
    if (frameIndex < 0 || frameIndex >= static_cast<int>(frames.size())) {
        _error = "frame index out of range";
        return false;
    }
    frames.erase(frames.begin() + frameIndex);
    _bDirty = true;
    (void)validate();
    return true;
}

bool SpriteAnimationSetEditModel::moveFrame(int clipIndex, int frameIndex, int delta)
{
    if (clipIndex < 0 || clipIndex >= static_cast<int>(_working.clips.size())) {
        _error = "no clip selected";
        return false;
    }
    auto&      frames = _working.clips[static_cast<size_t>(clipIndex)].frames;
    const int  dest   = frameIndex + delta;
    if (frameIndex < 0 || dest < 0 || frameIndex >= static_cast<int>(frames.size()) ||
        dest >= static_cast<int>(frames.size())) {
        _error = "frame index out of range";
        return false;
    }
    std::swap(frames[static_cast<size_t>(frameIndex)], frames[static_cast<size_t>(dest)]);
    _bDirty = true;
    (void)validate();
    return true;
}

bool SpriteAnimationSetEditModel::setFps(int clipIndex, float fps)
{
    if (clipIndex < 0 || clipIndex >= static_cast<int>(_working.clips.size())) {
        _error = "no clip selected";
        return false;
    }
    if (!(fps > 0.0f)) {
        _error = "fps must be > 0";
        return false;
    }
    SpriteAnimationClip& clip = _working.clips[static_cast<size_t>(clipIndex)];
    if (clip.fps == fps) {
        return true;
    }
    clip.fps = fps;
    _bDirty  = true;
    (void)validate();
    return true;
}

bool SpriteAnimationSetEditModel::setLoop(int clipIndex, bool bLoop)
{
    if (clipIndex < 0 || clipIndex >= static_cast<int>(_working.clips.size())) {
        _error = "no clip selected";
        return false;
    }
    SpriteAnimationClip& clip = _working.clips[static_cast<size_t>(clipIndex)];
    if (clip.bLoop == bLoop) {
        return true;
    }
    clip.bLoop = bLoop;
    _bDirty    = true;
    (void)validate();
    return true;
}

bool SpriteAnimationSetEditModel::setColumns(int32_t columns)
{
    if (columns < 1) {
        _error = "columns and rows must be >= 1";
        return false;
    }
    if (_working.columns == columns) {
        return true;
    }
    _working.columns = columns;
    _bDirty          = true;
    (void)validate();
    return true;
}

bool SpriteAnimationSetEditModel::setRows(int32_t rows)
{
    if (rows < 1) {
        _error = "columns and rows must be >= 1";
        return false;
    }
    if (_working.rows == rows) {
        return true;
    }
    _working.rows = rows;
    _bDirty       = true;
    (void)validate();
    return true;
}

bool SpriteAnimationSetEditModel::setAtlas(std::string path)
{
    if (_working.atlas == path) {
        return true;
    }
    _working.atlas = std::move(path);
    _bDirty        = true;
    (void)validate();
    return true;
}

bool SpriteAnimationSetEditModel::frameToCell(int32_t frame, int32_t columns, int32_t rows, int32_t& outColumn,
                                              int32_t& outRow)
{
    if (columns < 1 || rows < 1 || frame < 0 || frame >= columns * rows) {
        return false;
    }
    outColumn = frame % columns;
    outRow    = frame / columns;
    return true;
}

bool SpriteAnimationSetEditModel::validate()
{
    std::string error;
    const auto  parsed = parseSpriteAnimationSetJson(serialize(), error);
    if (!parsed) {
        _error = std::move(error);
        return false;
    }
    _error.clear();
    return true;
}

std::string SpriteAnimationSetEditModel::serialize() const
{
    return serializeSpriteAnimationSetJson(_working);
}

void SpriteAnimationSetEditModel::revert()
{
    _working = _saved;
    _bDirty  = false;
    _error.clear();
    if (_selectedClip >= static_cast<int>(_working.clips.size())) {
        _selectedClip = _working.clips.empty() ? -1 : static_cast<int>(_working.clips.size()) - 1;
    }
}

bool SpriteAnimationSetEditModel::save()
{
    if (!validate()) {
        return false;
    }
    if (_assetPath.empty() || std::filesystem::path(_assetPath).is_absolute()) {
        _error = "animation set has no asset path";
        return false;
    }
    VirtualFileSystem* vfs = VirtualFileSystem::get();
    if (!vfs) {
        _error = "virtual file system is not available";
        return false;
    }
    const std::string text = serialize();
    vfs->saveToFile(_assetPath, text);
    if (!vfs->isFileExists(_assetPath)) {
        _error = "failed to write animation set";
        return false;
    }
    IDocumentAssetStore<SpriteAnimationSet>* store = AssetTypeRegistry::get().store<SpriteAnimationSet>();
    if (!store) {
        _error = "sprite animation set store is not registered";
        return false;
    }
    store->registerAsset(_assetPath, std::make_shared<SpriteAnimationSet>(_working));
    _saved  = _working;
    _bDirty = false;
    _error.clear();
    YA_CORE_INFO("Saved sprite animation set '{}'", _assetPath);
    return true;
}

} // namespace ya
