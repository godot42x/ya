#include "Render3D/Common/RuntimeServices.h"

namespace ya
{

namespace
{
IRenderRuntimeHostServices* g_renderRuntimeHost = nullptr;
}

void RuntimeServices::setRenderRuntimeHost(IRenderRuntimeHostServices* services)
{
    g_renderRuntimeHost = services;
}

IRenderRuntimeHostServices* RuntimeServices::getRenderRuntimeHost()
{
    return g_renderRuntimeHost;
}

} // namespace ya
