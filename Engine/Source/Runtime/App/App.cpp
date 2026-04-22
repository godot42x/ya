#include "Runtime/App/App.h"

namespace ya
{

App*     App::_instance        = nullptr;
uint32_t App::App::_frameIndex = 0;

ClearValue colorClearValue = ClearValue(0.0f, 0.0f, 0.0f, 1.0f);
ClearValue depthClearValue = ClearValue(1.0f, 0);

App::~App() = default;

IRender* App::getRender() const
{
    return _renderRuntime ? _renderRuntime->getRender() : nullptr;
}

std::shared_ptr<ShaderStorage> App::getShaderStorage() const
{
    return _renderRuntime ? _renderRuntime->getShaderStorage() : nullptr;
}

ForwardRenderPipeline* App::getForwardPipeline() const
{
    return _renderRuntime ? _renderRuntime->getForwardPipeline() : nullptr;
}

bool App::isShadowMappingEnabled() const
{
    return _renderRuntime && _renderRuntime->isShadowMappingEnabled();
}

bool App::isMirrorRenderingEnabled() const
{
    return _renderRuntime && _renderRuntime->isMirrorRenderingEnabled();
}

bool App::hasMirrorRenderResult() const
{
    return _renderRuntime && _renderRuntime->hasMirrorRenderResult();
}

IRenderTarget* App::getShadowDepthRT() const
{
    return _renderRuntime ? _renderRuntime->getShadowDepthRT() : nullptr;
}

IImageView* App::getShadowDirectionalDepthIV() const
{
    return _renderRuntime ? _renderRuntime->getShadowDirectionalDepthIV() : nullptr;
}

IImageView* App::getShadowPointFaceDepthIV(uint32_t pointLightIndex, uint32_t faceIndex) const
{
    return _renderRuntime ? _renderRuntime->getShadowPointFaceDepthIV(pointLightIndex, faceIndex) : nullptr;
}

Texture* App::getPostprocessOutputTexture() const
{
    return _renderRuntime ? _renderRuntime->getPostprocessOutputTexture() : nullptr;
}

bool App::isPostprocessingEnabled() const
{
    return _renderRuntime && _renderRuntime->isPostprocessingEnabled();
}

} // namespace ya
