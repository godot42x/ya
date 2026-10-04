#include "ECS/Systems/WorldScriptRuntime.h"

namespace ya
{
namespace
{

WorldScriptRuntimeFactory gWorldScriptRuntimeFactory = nullptr;

} // namespace

void setWorldScriptRuntimeFactory(WorldScriptRuntimeFactory factory)
{
    gWorldScriptRuntimeFactory = factory;
}

IWorldScriptRuntime* createWorldScriptRuntime()
{
    return gWorldScriptRuntimeFactory ? gWorldScriptRuntimeFactory() : nullptr;
}

} // namespace ya
