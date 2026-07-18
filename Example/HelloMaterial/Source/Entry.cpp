

#include "HelloMaterial.h"
#include "Editor/App/EditorAppExtension.h"

#define YA_ENTRY_POINT
#include "Runtime/App/EntryPoint.h"


ya::App *createApp()
{
    YA_INFO("Creating HelloMaterial App");
    auto* app = new HelloMaterial();
    app->addExtension(ya::createEditorAppExtension());
    return app;
}
