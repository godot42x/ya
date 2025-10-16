

#include "HelloMaterial.h"

#define YA_ENTRY_POINT
#include "Core/App/EntryPoint.h"


ya::App *createApp()
{
    YA_INFO("Creating HelloMaterial App");
    return new HelloMaterial();
}
