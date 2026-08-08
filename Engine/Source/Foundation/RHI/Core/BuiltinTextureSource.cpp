#include "RHI/Core/BuiltinTextureSource.h"

namespace ya
{

namespace
{
IBuiltinTextureSource* s_builtinTextureSource = nullptr;
}

void setBuiltinTextureSource(IBuiltinTextureSource* source)
{
    s_builtinTextureSource = source;
}

IBuiltinTextureSource* getBuiltinTextureSource()
{
    return s_builtinTextureSource;
}

} // namespace ya
