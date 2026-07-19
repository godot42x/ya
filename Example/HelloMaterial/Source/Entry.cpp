

#include "HelloMaterial.h"
#include "Editor/App/EditorAppExtension.h"
#include "Runtime/App/Bootstrap/AutomationSceneBootstrapExtension.h"

#define YA_ENTRY_POINT
#include "Runtime/App/EntryPoint.h"

namespace
{

constexpr const char* HELLO_MATERIAL_SMOKE_SCENE = "Example/HelloMaterial/Content/Scenes/HelloMaterial.scene.json";

}

ya::App *createApp()
{
    YA_INFO("Creating HelloMaterial App");
    auto* app = new HelloMaterial();
    app->addExtension(ya::createAutomationSceneBootstrapExtension(HELLO_MATERIAL_SMOKE_SCENE));
    app->addExtension(ya::createEditorAppExtension());
    return app;
}
