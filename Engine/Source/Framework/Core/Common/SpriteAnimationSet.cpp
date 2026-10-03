#include "Core/Common/SpriteAnimationSet.h"

#include "Core/Common/AssetDocumentManager.h"
#include "Core/Common/JsonFormat.h"
#include "Core/Log.h"
#include "Core/Reflection/DeferredInitializer.h"
#include "Core/Reflection/ReflectionSerializer.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <limits>
#include <unordered_set>

namespace ya
{

glm::vec4 SpriteAnimationSet::frameRect(int32_t frame) const
{
    if (!isValid() || frame < 0 || frame >= columns * rows) {
        return glm::vec4(0.0f);
    }
    const int32_t column = frame % columns;
    const int32_t row    = frame / columns;
    const float   cw     = 1.0f / static_cast<float>(columns);
    const float   rh     = 1.0f / static_cast<float>(rows);
    return glm::vec4(static_cast<float>(column) * cw, static_cast<float>(row) * rh,
                     static_cast<float>(column + 1) * cw, static_cast<float>(row + 1) * rh);
}

const SpriteAnimationClip* SpriteAnimationSet::findClip(const std::string& name) const
{
    for (const SpriteAnimationClip& clip : clips) {
        if (clip.name == name) {
            return &clip;
        }
    }
    return nullptr;
}

namespace
{

bool readGridAxis(const nlohmann::json& json, const char* key, int32_t& out, std::string& outError)
{
    const auto it = json.find(key);
    if (it == json.end() || !it->is_number_integer()) {
        outError = std::string("missing or invalid field '") + key + "'";
        return false;
    }
    const int64_t raw = it->get<int64_t>();
    if (raw < 1 || raw > static_cast<int64_t>(std::numeric_limits<int32_t>::max())) {
        outError = "columns and rows must be >= 1";
        return false;
    }
    out = static_cast<int32_t>(raw);
    return true;
}

bool validateClips(const nlohmann::json& json, int32_t columns, int32_t rows, std::string& outError)
{
    const auto clipsIt = json.find("clips");
    if (clipsIt == json.end() || !clipsIt->is_array()) {
        outError = "missing or invalid field 'clips'";
        return false;
    }

    const int64_t                frameCount = static_cast<int64_t>(columns) * static_cast<int64_t>(rows);
    std::unordered_set<std::string> names;
    for (const nlohmann::json& clip : *clipsIt) {
        if (!clip.is_object()) {
            outError = "clip must be an object";
            return false;
        }

        const auto nameIt = clip.find("name");
        if (nameIt == clip.end() || !nameIt->is_string() || nameIt->get<std::string>().empty()) {
            outError = "clip name must be non-empty";
            return false;
        }
        const std::string name = nameIt->get<std::string>();
        if (!names.insert(name).second) {
            outError = "duplicate clip name '" + name + "'";
            return false;
        }

        const auto framesIt = clip.find("frames");
        if (framesIt == clip.end() || !framesIt->is_array()) {
            outError = "missing or invalid field 'frames'";
            return false;
        }
        if (framesIt->empty()) {
            outError = "frames must be non-empty";
            return false;
        }
        for (const nlohmann::json& frame : *framesIt) {
            if (!frame.is_number_integer()) {
                outError = "frame index must be an integer";
                return false;
            }
            const int64_t index = frame.get<int64_t>();
            if (index < 0 || index >= frameCount) {
                outError = "frame out of range";
                return false;
            }
        }

        // Omitted fps / bLoop keep the struct defaults (8.0 / true), matching
        // scene files that drop default fields. A present value of the wrong
        // type is still an error.
        const auto fpsIt = clip.find("fps");
        if (fpsIt != clip.end()) {
            if (!fpsIt->is_number()) {
                outError = "invalid field 'fps'";
                return false;
            }
            if (fpsIt->get<double>() <= 0.0) {
                outError = "fps must be > 0";
                return false;
            }
        }

        const auto loopIt = clip.find("bLoop");
        if (loopIt != clip.end() && !loopIt->is_boolean()) {
            outError = "invalid field 'bLoop'";
            return false;
        }
    }
    return true;
}

} // namespace

std::shared_ptr<SpriteAnimationSet> parseSpriteAnimationSetJson(const std::string& text, std::string& outError)
{
    reflection::DeferredInitializerQueue::instance().executeAll();

    nlohmann::json json;
    try {
        json = nlohmann::json::parse(text);
    }
    catch (const nlohmann::json::exception& error) {
        outError = error.what();
        return nullptr;
    }
    if (!json.is_object()) {
        outError = "sprite animation set root must be an object";
        return nullptr;
    }

    int32_t columns = 0;
    int32_t rows    = 0;
    if (!readGridAxis(json, "columns", columns, outError) || !readGridAxis(json, "rows", rows, outError)) {
        return nullptr;
    }
    if (!validateClips(json, columns, rows, outError)) {
        return nullptr;
    }

    auto set = std::make_shared<SpriteAnimationSet>();
    ReflectionSerializer::deserializeByRuntimeReflection(*set, json, "SpriteAnimationSet");
    return set;
}

std::string serializeSpriteAnimationSetJson(const SpriteAnimationSet& set)
{
    reflection::DeferredInitializerQueue::instance().executeAll();
    return dumpJsonCompactLeaves(ReflectionSerializer::serializeByRuntimeReflection(set, "SpriteAnimationSet"));
}

namespace
{

struct SpriteAnimationSetDocumentTraits
{
    static std::shared_ptr<SpriteAnimationSet> parse(const std::string& text, std::string& error)
    {
        return parseSpriteAnimationSetJson(text, error);
    }

    static void logCannotRead(const std::string& path)
    {
        YA_CORE_ERROR("AssetSpriteAnimationSetManager: cannot read sprite animation set '{}'", path);
    }

    static void logInvalid(const std::string& path, const std::string& error)
    {
        YA_CORE_ERROR("AssetSpriteAnimationSetManager: '{}' is not a valid sprite animation set: {}", path, error);
    }
};

struct SpriteAnimationSetAssetRegistration
{
    SpriteAnimationSetAssetRegistration()
    {
        AssetTypeRegistry::registerDocument<SpriteAnimationSet, SpriteAnimationSetDocumentTraits, SpriteAnimationSetRef>(
            "SpriteAnimationSet", "Select Sprite Animation Set", {".yaanim.json"});
    }
};

const SpriteAnimationSetAssetRegistration g_spriteAnimationSetAssetRegistration;

} // namespace

} // namespace ya
