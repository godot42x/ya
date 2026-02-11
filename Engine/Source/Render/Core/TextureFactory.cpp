#include "TextureFactory.h"

#include "Core/App/App.h"
#include "Render/Render.h"

namespace ya
{

ITextureFactory *TextureFactoryHelper::s_currentFactory = nullptr;

ITextureFactory *TextureFactoryHelper::get()
{
    return s_currentFactory;
}

void TextureFactoryHelper::set(ITextureFactory *factory)
{
    s_currentFactory = factory;
}

bool TextureFactoryHelper::isAvailable()
{
    return s_currentFactory != nullptr && s_currentFactory->isValid();
}

} // namespace ya