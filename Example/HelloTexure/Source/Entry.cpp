
#include "Core/App/EntryPoint.h"
#include "HelloTexture.h"


#define YA_ENTRY_POINT

ya::App *createApp()
{
    return new HelloTexture();
}
