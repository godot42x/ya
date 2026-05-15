#include "Render/Shader.h"

#include "Render/Shader/ShaderInternal.h"

#include "Core/System/VirtualFileSystem.h"

#include "utility.cc/string_utils.h"

#include <array>
#include <atomic>
#include <cstring>
#include <filesystem>

#include <slang/slang-com-ptr.h>
#include <slang/slang.h>

namespace ya
{
namespace
{

struct SlangStringBlob : public ISlangBlob
{
    std::string           data;
    std::atomic<uint32_t> rc{1};

    uint32_t addRef() override { return ++rc; }
    uint32_t release() override
    {
        uint32_t remaining = --rc;
        if (remaining == 0) {
            delete this;
        }
        return remaining;
    }

    SlangResult queryInterface(const SlangUUID& uuid, void** out) override
    {
        if (uuid == ISlangBlob::getTypeGuid() || uuid == ISlangUnknown::getTypeGuid()) {
            addRef();
            *out = static_cast<ISlangBlob*>(this);
            return SLANG_OK;
        }
        *out = nullptr;
        return SLANG_E_NO_INTERFACE;
    }

    const void* getBufferPointer() override { return data.data(); }
    size_t      getBufferSize() override { return data.size(); }
};

struct SlangVfsFileSystem : public ISlangFileSystem
{
    virtual ~SlangVfsFileSystem() = default;

    uint32_t addRef() override { return ++_refCount; }
    uint32_t release() override
    {
        uint32_t remaining = --_refCount;
        if (remaining == 0) {
            delete this;
        }
        return remaining;
    }

    SlangResult queryInterface(const SlangUUID& uuid, void** outObject) override
    {
        if (uuid == ISlangFileSystem::getTypeGuid() || uuid == ISlangUnknown::getTypeGuid()) {
            addRef();
            *outObject = static_cast<ISlangFileSystem*>(this);
            return SLANG_OK;
        }
        *outObject = nullptr;
        return SLANG_E_NO_INTERFACE;
    }

    void* castAs(const SlangUUID& guid) override
    {
        if (guid == ISlangFileSystem::getTypeGuid() || guid == ISlangUnknown::getTypeGuid()) {
            return static_cast<ISlangFileSystem*>(this);
        }
        return nullptr;
    }

    SlangResult loadFile(const char* path, ISlangBlob** outBlob) override
    {
        std::string content;
        if (!VFS::get()->isFileExists(path)) {
            return SLANG_E_NOT_FOUND;
        }
        if (!VirtualFileSystem::get()->readFileToString(path, content)) {
            YA_CORE_ERROR("[SlangVFS] Failed to load: {}", path);
            return SLANG_E_NOT_FOUND;
        }

        auto* blob = new SlangStringBlob();
        blob->data = std::move(content);
        *outBlob   = blob;
        return SLANG_OK;
    }

  private:
    std::atomic<uint32_t> _refCount{1};
};

} // namespace

bool SlangProcessor::compileToSpv(std::string_view                source,
                                  std::string_view                filePath,
                                  std::string_view                entryName,
                                  EShaderStage::T                 stage,
                                  const std::vector<std::string>& defines,
                                  std::vector<ir_t>&              outSpv)
{
    (void)stage;

    Slang::ComPtr<slang::IGlobalSession> globalSession;
    if (SLANG_FAILED(slang::createGlobalSession(globalSession.writeRef()))) {
        YA_CORE_ERROR("[Slang] Failed to create global session");
        return false;
    }

    slang::TargetDesc targetDesc{};
    targetDesc.format  = SLANG_SPIRV;
    targetDesc.profile = globalSession->findProfile("spirv_1_3");

    std::vector<slang::PreprocessorMacroDesc> macros;
    macros.reserve(defines.size() + 1);
    macros.push_back({.name = "YA_PLATFORM_VULKAN", .value = "1"});

    std::vector<std::pair<std::string, std::string>> macroStorage;
    macroStorage.reserve(defines.size());
    for (const auto& def : defines) {
        auto eq = def.find('=');
        if (eq != std::string::npos) {
            macroStorage.push_back({def.substr(0, eq), def.substr(eq + 1)});
        }
        else {
            auto ws = def.find_first_of(" \t");
            if (ws != std::string::npos) {
                auto name = ut::str::trim(std::string_view(def).substr(0, ws));
                auto val  = ut::str::trim(std::string_view(def).substr(ws + 1));
                macroStorage.push_back({std::string(name), std::string(val.empty() ? "1" : val)});
            }
            else {
                auto name = ut::str::trim(std::string_view(def));
                macroStorage.push_back({std::string(name), "1"});
            }
        }

        macros.push_back({
            .name  = macroStorage.back().first.c_str(),
            .value = macroStorage.back().second.c_str(),
        });
    }

    auto* slangVfs = new SlangVfsFileSystem();

    std::string searchPath0   = shaderStoragePath.string();
    std::string searchPath1   = "Engine/Shader/Slang";
    const char* searchPaths[] = {searchPath0.c_str(), searchPath1.c_str()};

    slang::SessionDesc sessionDesc{};
    sessionDesc.targets                 = &targetDesc;
    sessionDesc.targetCount             = 1;
    sessionDesc.preprocessorMacros      = macros.data();
    sessionDesc.preprocessorMacroCount  = static_cast<SlangInt>(macros.size());
    sessionDesc.fileSystem              = slangVfs;
    sessionDesc.searchPaths             = searchPaths;
    sessionDesc.searchPathCount         = static_cast<SlangInt>(sizeof(searchPaths) / sizeof(searchPaths[0]));
    sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;

    Slang::ComPtr<slang::ISession> session;
    if (SLANG_FAILED(globalSession->createSession(sessionDesc, session.writeRef()))) {
        YA_CORE_ERROR("[Slang] Failed to create session for: {}", filePath);
        slangVfs->release();
        return false;
    }
    slangVfs->release();

    auto buildModuleName = [&](std::string_view sourceFilePath) -> std::string
    {
        const auto sourcePath     = std::filesystem::path(sourceFilePath);
        auto       relative       = sourcePath.lexically_relative(shaderStoragePath);
        const auto relativeString = relative.generic_string();
        if (relative.empty() || relativeString == ".." || relativeString.starts_with("../")) {
            relative = sourcePath.filename();
        }

        relative.replace_extension();
        return relative.generic_string();
    };

    Slang::ComPtr<slang::IBlob> diagBlob;
    auto*                       srcBlob = new SlangStringBlob();
    srcBlob->data                       = std::string(source);

    const std::string moduleName = buildModuleName(filePath);

    Slang::ComPtr<slang::IModule> slangModule;
    slangModule = session->loadModuleFromSource(
        moduleName.c_str(),
        std::string(filePath).c_str(),
        srcBlob,
        diagBlob.writeRef());
    srcBlob->release();

    if (diagBlob && diagBlob->getBufferSize() > 0) {
        std::string_view diagStr(static_cast<const char*>(diagBlob->getBufferPointer()), diagBlob->getBufferSize());
        YA_CORE_WARN("[Slang] Diagnostics for {}:\n{}", filePath, diagStr);
    }

    if (!slangModule) {
        YA_CORE_ERROR("[Slang] Failed to load module: {}", filePath);
        return false;
    }

    Slang::ComPtr<slang::IEntryPoint> entryPoint;
    if (SLANG_FAILED(slangModule->findEntryPointByName(std::string(entryName).c_str(), entryPoint.writeRef()))) {
        return false;
    }

    slang::IComponentType*               components[] = {slangModule, entryPoint};
    Slang::ComPtr<slang::IComponentType> composedProgram;
    Slang::ComPtr<slang::IBlob>          linkDiag;
    if (SLANG_FAILED(session->createCompositeComponentType(components, 2, composedProgram.writeRef(), linkDiag.writeRef()))) {
        if (linkDiag && linkDiag->getBufferSize() > 0) {
            std::string_view diag(static_cast<const char*>(linkDiag->getBufferPointer()), linkDiag->getBufferSize());
            YA_CORE_ERROR("[Slang] Link error for {}:\n{}", filePath, diag);
        }
        return false;
    }

    Slang::ComPtr<slang::IComponentType> linkedProgram;
    Slang::ComPtr<slang::IBlob>          programDiag;
    if (SLANG_FAILED(composedProgram->link(linkedProgram.writeRef(), programDiag.writeRef()))) {
        if (programDiag && programDiag->getBufferSize() > 0) {
            std::string_view diag(static_cast<const char*>(programDiag->getBufferPointer()), programDiag->getBufferSize());
            YA_CORE_ERROR("[Slang] Link error for {}:\n{}", filePath, diag);
        }
        return false;
    }

    Slang::ComPtr<slang::IBlob> spvBlob;
    Slang::ComPtr<slang::IBlob> codeDiag;
    if (SLANG_FAILED(linkedProgram->getEntryPointCode(0, 0, spvBlob.writeRef(), codeDiag.writeRef()))) {
        if (codeDiag && codeDiag->getBufferSize() > 0) {
            std::string_view diag(static_cast<const char*>(codeDiag->getBufferPointer()), codeDiag->getBufferSize());
            YA_CORE_ERROR("[Slang] Code gen error for {}:\n{}", filePath, diag);
        }
        return false;
    }

    const size_t byteSize = spvBlob->getBufferSize();
    if (byteSize == 0 || byteSize % sizeof(ir_t) != 0) {
        YA_CORE_ERROR("[Slang] Invalid SPIR-V output size ({} bytes) for: {}", byteSize, filePath);
        return false;
    }

    outSpv.resize(byteSize / sizeof(ir_t));
    std::memcpy(outSpv.data(), spvBlob->getBufferPointer(), byteSize);
    return true;
}

std::optional<SlangProcessor::stage2spirv_t> SlangProcessor::process(const ShaderDesc& ci, EShaderProcessMode mode)
{
    const auto cacheKey = ci.cacheKey();
    YA_CORE_ASSERT(!cacheKey.empty(), "ShaderDesc cache key cannot be empty");

    stage2spirv_t ret;

    auto compileStage = [&](EShaderStage::T stage, const std::string& stagePath, const std::string& entryName) -> bool
    {
        std::string source;
        if (!VirtualFileSystem::get()->readFileToString(stagePath, source)) {
            YA_CORE_ERROR("[Slang] Failed to read shader source: {}", stagePath);
            return false;
        }

        std::vector<ir_t> spv;
        if (!compileToSpv(source, stagePath, entryName, stage, ci.defines, spv)) {
            YA_CORE_ERROR("[Slang] Failed to compile stage {} of: {}", EShaderStage::T2Strings[stage], stagePath);
            return false;
        }

        ret[stage] = std::move(spv);
        return true;
    };

    auto getCachePath = [&]() -> std::filesystem::path
    {
        auto cacheDir = stdpath(intermediateStoragePath) / "Slang";
        auto key      = curFileName.empty() ? curFilePath.generic_string() : curFileName;
        return cacheDir / shader_internal::makeCacheFileName(key);
    };

    if (ci.sourceMode == ShaderDesc::ESourceMode::StageFiles) {
        static const std::unordered_map<EShaderStage::T, std::string> stageEntryNames = {
            {EShaderStage::Vertex, "vertMain"},
            {EShaderStage::Fragment, "fragMain"},
            {EShaderStage::Geometry, "geomMain"},
            {EShaderStage::Compute, "compMain"},
            {EShaderStage::Task, "taskMain"},
            {EShaderStage::Mesh, "meshMain"},
        };

        std::vector<shader_internal::ShaderStageSource> stageSources;
        stageSources.reserve(ci.stageFiles.size());

        for (const auto& stageFile : ci.stageFiles) {
            if (ret.contains(stageFile.stage)) {
                YA_CORE_ERROR("[Slang] Duplicate stage in stageFiles, stage={}", static_cast<int>(stageFile.stage));
                return {};
            }

            std::filesystem::path stagePath(stageFile.file);
            if (!stagePath.is_absolute()) {
                stagePath = stdpath(shaderStoragePath) / stagePath;
            }

            std::string stageSource;
            if (!VirtualFileSystem::get()->readFileToString(stagePath.generic_string(), stageSource)) {
                YA_CORE_ERROR("[Slang] Failed to read stage-file shader source: {}", stagePath.generic_string());
                return {};
            }

            stageSources.push_back(shader_internal::ShaderStageSource{
                .stage  = stageFile.stage,
                .path   = stagePath.generic_string(),
                .source = std::move(stageSource),
            });
            ret[stageFile.stage] = {};
        }

        ret.clear();
        curFileName = cacheKey;
        curFilePath = stdpath(shaderStoragePath) / cacheKey;

        const uint64_t sourceHash = shader_internal::buildShaderSourceHash(stageSources, ci.defines);
        const auto     cachePath  = getCachePath();
        if (mode == EShaderProcessMode::UseCache && shader_internal::loadShaderDiskCache(cachePath, sourceHash, ret)) {
            YA_CORE_INFO("[Slang] Loaded shader disk cache for {}", cacheKey);
            return {std::move(ret)};
        }

        for (const auto& stageFile : ci.stageFiles) {
            std::filesystem::path stagePath(stageFile.file);
            if (!stagePath.is_absolute()) {
                stagePath = stdpath(shaderStoragePath) / stagePath;
            }

            const auto entryIt   = stageEntryNames.find(stageFile.stage);
            const auto entryName = entryIt != stageEntryNames.end() ? entryIt->second : std::string{"main"};
            if (!compileStage(stageFile.stage, stagePath.generic_string(), entryName)) {
                return {};
            }
        }

        if (!ret.contains(EShaderStage::Vertex) || !ret.contains(EShaderStage::Fragment)) {
            YA_CORE_ERROR("[Slang] StageFiles mode requires at least vertex and fragment stages: {}", cacheKey);
            return {};
        }

        if (!shader_internal::saveShaderDiskCache(cachePath, sourceHash, ret)) {
            YA_CORE_WARN("[Slang] Failed to write shader disk cache: {}", cachePath.generic_string());
        }
        YA_CORE_INFO("[Slang] Compiled {} stages for: {}", ret.size(), cacheKey);
    }
    else {
        std::string shaderName = ci.shaderName;
        YA_CORE_ASSERT(!shaderName.empty(), "SingleShader mode requires shaderName");
        if (!shaderName.ends_with(".slang")) {
            shaderName += ".slang";
        }

        curFileName = cacheKey;
        curFilePath = stdpath(shaderStoragePath) / shaderName;

        std::string source;
        if (!VirtualFileSystem::get()->readFileToString(curFilePath.generic_string(), source)) {
            YA_CORE_ERROR("[Slang] Failed to read shader: {}", curFilePath.generic_string());
            return {};
        }

        const uint64_t sourceHash = shader_internal::buildShaderSourceHash(
            {shader_internal::ShaderStageSource{
                .stage  = EShaderStage::Vertex,
                .path   = curFilePath.generic_string(),
                .source = source,
            }},
            ci.defines);
        const auto cachePath = getCachePath();
        if (mode == EShaderProcessMode::UseCache && shader_internal::loadShaderDiskCache(cachePath, sourceHash, ret)) {
            YA_CORE_INFO("[Slang] Loaded shader disk cache for {}", shaderName);
            return {std::move(ret)};
        }

        struct StageEntryCandidate
        {
            EShaderStage::T stage;
            const char*     entryName;
            bool            required;
        };

        const auto isStageSuffixShader = [&](std::string_view suffix)
        {
            return shaderName.ends_with(std::string(suffix) + ".slang");
        };

        const bool isComputeOnlyShader = isStageSuffixShader(".comp");
        const bool isTaskOnlyShader    = isStageSuffixShader(".task");
        const bool isMeshOnlyShader    = isStageSuffixShader(".mesh");

        const std::array<StageEntryCandidate, 6> stageCandidates = {{
            {.stage = EShaderStage::Vertex, .entryName = "vertMain", .required = !(isComputeOnlyShader || isTaskOnlyShader || isMeshOnlyShader)},
            {.stage = EShaderStage::Fragment, .entryName = "fragMain", .required = !(isComputeOnlyShader || isTaskOnlyShader || isMeshOnlyShader)},
            {.stage = EShaderStage::Geometry, .entryName = "geomMain", .required = false},
            {.stage = EShaderStage::Compute, .entryName = "compMain", .required = isComputeOnlyShader},
            {.stage = EShaderStage::Task, .entryName = "taskMain", .required = isTaskOnlyShader},
            {.stage = EShaderStage::Mesh, .entryName = "meshMain", .required = isMeshOnlyShader},
        }};

        for (const auto& candidate : stageCandidates) {
            std::vector<ir_t> spv;
            if (compileToSpv(source, curFilePath.generic_string(), candidate.entryName, candidate.stage, ci.defines, spv)) {
                ret[candidate.stage] = std::move(spv);
                continue;
            }

            if (candidate.required) {
                YA_CORE_ERROR("[Slang] Failed to compile required stage {} for: {}",
                              EShaderStage::T2Strings[candidate.stage],
                              shaderName);
                return {};
            }
        }

        if (!shader_internal::saveShaderDiskCache(cachePath, sourceHash, ret)) {
            YA_CORE_WARN("[Slang] Failed to write shader disk cache: {}", cachePath.generic_string());
        }
        YA_CORE_INFO("[Slang] Compiled {} stages for: {}", ret.size(), shaderName);
    }

    for (const auto& [stage, spirv] : ret) {
        if (!shader_internal::isValidSpirvModule(spirv)) {
            YA_CORE_ERROR("[Slang] Invalid SPIR-V magic for stage {}: {}", EShaderStage::T2Strings[stage], cacheKey);
            YA_CORE_ASSERT(false, "SPIR-V validation failed");
        }
    }

    return {std::move(ret)};
}

} // namespace ya
