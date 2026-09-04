#include "GUI/Host/OsClipboard.h"

#include "GUI/Widgets/WidgetTree.h"

#include <SDL3/SDL.h>

namespace ya
{

void bindSdlClipboard(WidgetTree& tree)
{
    tree.setClipboardHooks(
        []() -> std::string {
            char* text = SDL_GetClipboardText();
            if (text == nullptr) {
                return {};
            }
            std::string out(text);
            SDL_free(text);
            return out;
        },
        [](const std::string& text) { SDL_SetClipboardText(text.c_str()); });
}

} // namespace ya
