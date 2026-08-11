#include "AppRuntime/AppBootstrap.h"

#include "Core/Reflection/DeferredInitializer.h"
#include "Core/System/VirtualFileSystem.h"

#include <filesystem>
#include <string>

#if defined(__APPLE__)
    #include <mach-o/dyld.h>
#endif

namespace ya
{

namespace
{

#if defined(__APPLE__)
std::filesystem::path getExecutableDir()
{
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return {};
    }
    return std::filesystem::weakly_canonical(std::filesystem::path(buffer.c_str())).parent_path();
}
#endif

} // namespace

void AppBootstrap::initializeProcess(const std::optional<std::string>& contentRoot)
{
    configureBundledGraphicsRuntimeEnv();
    ::ya::reflection::DeferredInitializerQueue::instance().executeAll();
    VirtualFileSystem::init();
    if (contentRoot) {
        VirtualFileSystem::get()->setContentRoot(*contentRoot);
    }
}

void AppBootstrap::configureBundledGraphicsRuntimeEnv()
{
#if defined(__APPLE__)
    if (std::getenv("VK_ICD_FILENAMES") != nullptr) {
        return;
    }

    const auto executableDir = getExecutableDir();
    if (executableDir.empty()) {
        return;
    }

    std::filesystem::path sdkRoot;
    for (auto current = executableDir; !current.empty(); current = current.parent_path()) {
        const auto candidate = current / "Engine" / "ThirdParty" / "VulkanSDK";
        if (std::filesystem::is_directory(candidate)) {
            sdkRoot = candidate;
            break;
        }
    }
    if (sdkRoot.empty()) {
        return;
    }

    std::filesystem::path selectedSdkDir;
    for (const auto& entry : std::filesystem::directory_iterator(sdkRoot)) {
        if (!entry.is_directory()) {
            continue;
        }
        const auto sdkDir   = entry.path() / "macOS";
        const auto icdJson  = sdkDir / "share" / "vulkan" / "icd.d" / "MoltenVK_icd.json";
        const auto moltenVk = sdkDir / "lib" / "libMoltenVK.dylib";
        if (std::filesystem::is_regular_file(icdJson) && std::filesystem::is_regular_file(moltenVk)) {
            if (selectedSdkDir.empty() || entry.path().filename().string() > selectedSdkDir.parent_path().filename().string()) {
                selectedSdkDir = sdkDir;
            }
        }
    }

    if (selectedSdkDir.empty()) {
        return;
    }

    const auto icdJson  = selectedSdkDir / "share" / "vulkan" / "icd.d" / "MoltenVK_icd.json";
    const auto layerDir = selectedSdkDir / "share" / "vulkan" / "explicit_layer.d";
    const auto sdkPath  = selectedSdkDir.string();
    const auto libPath  = (selectedSdkDir / "lib").string();

    setenv("VULKAN_SDK", sdkPath.c_str(), 0);
    setenv("DYLD_LIBRARY_PATH", libPath.c_str(), 0);
    setenv("VK_ICD_FILENAMES", icdJson.string().c_str(), 0);
    if (std::filesystem::is_directory(layerDir)) {
        setenv("VK_LAYER_PATH", layerDir.string().c_str(), 0);
    }
#endif
}

} // namespace ya
