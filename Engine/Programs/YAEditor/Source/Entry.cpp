#include "Editor/EditorAppExtension.h"

#define YA_ENTRY_POINT
#include "Runtime/App/EntryPoint.h"

ya::App* createApp()
{
    auto* app = new ya::App();
    app->addExtension(ya::createEditorAppExtension());
    return app;
}
