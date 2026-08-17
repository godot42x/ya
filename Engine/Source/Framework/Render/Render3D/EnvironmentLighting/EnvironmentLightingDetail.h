#pragma once

#include "EnvironmentLightingProcessor.h"

namespace ya::detail
{

void        retireTexture(stdptr<Texture>& texture);
void        retireTextureNow(stdptr<Texture>& texture);
void        retireRenderTexture(std::shared_ptr<RenderTexture>& texture);
void        retireRenderTextureNow(std::shared_ptr<RenderTexture>& texture);
[[nodiscard]] std::shared_ptr<ImageResource> ownerResourceOf(const std::shared_ptr<RenderTexture>& renderImage, const stdptr<Texture>& texture);
[[nodiscard]] std::shared_ptr<IImage> getImageShared(const std::shared_ptr<RenderTexture>& texture, const stdptr<Texture>& fallbackTexture);
[[nodiscard]] IImageView*             getImageView(const std::shared_ptr<RenderTexture>& texture, const stdptr<Texture>& fallbackTexture);
[[nodiscard]] std::shared_ptr<IImageView> getImageViewShared(const std::shared_ptr<RenderTexture>& texture, const stdptr<Texture>& fallbackTexture);
EFormat::T  chooseSkyboxCubemapFormat(EFormat::T sourceFormat);
EFormat::T  chooseEnvironmentIrradianceFormat(EFormat::T sourceFormat);
uint32_t    computeSkyboxFaceSize(const Texture* sourceTexture);
uint32_t    computeEnvironmentIrradianceFaceSize(const Texture* sourceTexture, uint32_t requestedFaceSize);
std::shared_ptr<RenderTexture> createRenderableSkyboxCubemap(IRender*           render,
                                                             const std::string& label,
                                                             uint32_t           faceSize,
                                                             EFormat::T         format,
                                                             int                mips = -1);
OffscreenJobState::CreateOutputFn makeCubemapOutputFn(const std::string& label,
                                                      uint32_t           faceSize,
                                                      EFormat::T         format,
                                                      int                mipLevels = 1);

void tryQueueJob(const OffscreenJobQueueService& queueService, IRender* render, const std::shared_ptr<OffscreenJobState>& job);

void rebuildSkyboxViews(IRender* render, SkyboxRuntimeState& state);
void retireSkyboxResources(SkyboxRuntimeState& state);
void resetSkyboxPending(SkyboxRuntimeState& state);
void resetSkyboxState(SkyboxRuntimeState& state);

void rebuildEnvironmentCubemapViews(IRender* render, EnvironmentLightingRuntimeState& state);
void rebuildEnvironmentIrradianceViews(IRender* render, EnvironmentLightingRuntimeState& state);
void rebuildPrefilterViews(IRender* render, EnvironmentLightingRuntimeState& state);
void retireEnvTextures(EnvironmentLightingRuntimeState& state);
void resetEnvPending(EnvironmentLightingRuntimeState& state);
void resetEnvState(EnvironmentLightingRuntimeState& state);

} // namespace ya::detail
