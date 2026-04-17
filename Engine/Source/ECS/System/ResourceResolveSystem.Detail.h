#pragma once

#include "ResourceResolveSystem.h"

namespace ya::detail
{

void        retireTexture(stdptr<Texture>& texture);
void        retireTextureNow(stdptr<Texture>& texture);
EFormat::T  chooseSkyboxCubemapFormat(EFormat::T sourceFormat);
EFormat::T  chooseEnvironmentIrradianceFormat(EFormat::T sourceFormat);
uint32_t    computeSkyboxFaceSize(const Texture* sourceTexture);
uint32_t    computeEnvironmentIrradianceFaceSize(const Texture* sourceTexture, uint32_t requestedFaceSize);
stdptr<Texture> createRenderableSkyboxCubemap(IRender*           render,
                                              const std::string& label,
                                              uint32_t           faceSize,
                                              EFormat::T         format,
                                              int                mips = -1);
OffscreenJobState::CreateOutputFn makeCubemapOutputFn(const std::string& label,
                                                      uint32_t           faceSize,
                                                      EFormat::T         format,
                                                      int                mipLevels = 1);

void tryQueueJob(const std::shared_ptr<OffscreenJobState>& job);

void rebuildSkyboxViews(SkyboxRuntimeState& state);
void retireSkyboxResources(SkyboxRuntimeState& state);
void resetSkyboxPending(SkyboxRuntimeState& state);
void resetSkyboxState(SkyboxRuntimeState& state);

void retireEnvTextures(EnvironmentLightingRuntimeState& state);
void resetEnvPending(EnvironmentLightingRuntimeState& state);
void resetEnvState(EnvironmentLightingRuntimeState& state);

} // namespace ya::detail