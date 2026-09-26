#include "GameEditor/UI/Dock/EditorLayoutLibrary.h"

#include "Core/Log.h"
#include "Core/System/VirtualFileSystem.h"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace ya
{

namespace
{

/// The editor profile's tree. These are the product's own paths, not the
/// framework's: a standalone GUI app binds its own pair before building chrome.
constexpr std::string_view kEditorLayoutDefaultsRoot = "Engine/Config/Layout/Editor";
constexpr std::string_view kEditorLayoutOverridesRoot = "Engine/Saved/Layout/Editor";

/// Where a relative root may live, in order. The VFS is the product's answer
/// (a packaged build mounts its own tree); the process working directory is the
/// fallback, and it is what keeps a relative root resolvable in a process whose
/// VFS was re-rooted by something else -- a test fixture, a nested run.
std::vector<std::filesystem::path> candidatePathsOf(const std::string& virtualPath)
{
    std::vector<std::filesystem::path> candidates;
    if (auto* vfs = VirtualFileSystem::get()) {
        candidates.push_back(vfs->translatePath(virtualPath));
    }
    const std::filesystem::path direct(virtualPath);
    if (direct.is_absolute()) {
        return candidates.empty() ? std::vector<std::filesystem::path>{direct} : candidates;
    }
    std::error_code error;
    const std::filesystem::path fromCwd = std::filesystem::current_path(error) / direct;
    if (!error) {
        candidates.push_back(fromCwd.lexically_normal());
    }
    return candidates;
}

} // namespace

EditorLayoutLibrary& EditorLayoutLibrary::get()
{
    static EditorLayoutLibrary instance{
        FEditorLayoutRoots{
            .defaults  = std::string(kEditorLayoutDefaultsRoot),
            .overrides = std::string(kEditorLayoutOverridesRoot),
        },
    };
    return instance;
}

void EditorLayoutLibrary::bindRoots(FEditorLayoutRoots roots)
{
    if (roots.defaults.empty() && roots.overrides.empty()) {
        return;
    }
    _roots = std::move(roots);
}

nlohmann::json EditorLayoutLibrary::readFrom(const std::string& root, std::string_view name) const
{
    if (root.empty() || name.empty()) {
        return nlohmann::json::object();
    }
    const std::string virtualPath = root + "/" + std::string(name) + ".json";

    std::error_code error;
    std::filesystem::path physical;
    for (const std::filesystem::path& candidate : candidatePathsOf(virtualPath)) {
        if (std::filesystem::is_regular_file(candidate, error)) {
            physical = candidate;
            break;
        }
    }
    // Existence first: a missing file is the ordinary "no override yet" case,
    // not an error, so it must not log one on every frame that reads layouts.
    if (physical.empty()) {
        return nlohmann::json::object();
    }
    std::ifstream in(physical);
    if (!in.is_open()) {
        return nlohmann::json::object();
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (text.empty()) {
        return nlohmann::json::object();
    }
    try {
        return nlohmann::json::parse(text);
    }
    catch (const nlohmann::json::exception& e) {
        YA_CORE_WARN("EditorLayoutLibrary: '{}' is not valid JSON: {}", virtualPath, e.what());
        return nlohmann::json::object();
    }
}

nlohmann::json EditorLayoutLibrary::document(std::string_view name) const
{
    nlohmann::json override_ = readFrom(_roots.overrides, name);
    if (override_.is_object() && !override_.empty()) {
        return override_;
    }
    nlohmann::json shipped = readFrom(_roots.defaults, name);
    return shipped.is_object() ? std::move(shipped) : nlohmann::json::object();
}

bool EditorLayoutLibrary::saveOverride(std::string_view name, const nlohmann::json& document) const
{
    if (_roots.overrides.empty() || name.empty() || !document.is_object()) {
        return false;
    }
    const std::string virtualPath = _roots.overrides + "/" + std::string(name) + ".json";
    const std::string text        = document.dump(4, ' ', false);

    if (auto* vfs = VirtualFileSystem::get()) {
        vfs->saveToFile(virtualPath, text);
        return true;
    }
    std::error_code error;
    const std::vector<std::filesystem::path> candidates = candidatePathsOf(virtualPath);
    for (const std::filesystem::path& candidate : candidates) {
        std::filesystem::create_directories(candidate.parent_path(), error);
        std::ofstream out(candidate);
        if (out.is_open()) {
            out << text;
            return true;
        }
    }
    YA_CORE_WARN("EditorLayoutLibrary: cannot write '{}'", virtualPath);
    return false;
}

} // namespace ya
