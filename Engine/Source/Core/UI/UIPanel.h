
#include "UElement.h"

struct UIPanel : public UIElement
{
    std::string title;

  public:
    UIPanel(const std::string &title) : title(title) {}

    void render() override
    {
    }

    void update(float dt) override
    {
        for (const auto &child : _children) {
            child->update(dt);
        }
    }

    EventProcessState handleEvent(const Event &event) override
    {
        for (const auto &child : _children) {
            auto state = child->handleEvent(event);
            if (state == EventProcessState::Handled) {
                return state;
            }
        }
        return EventProcessState(EventProcessState::Continue);
    }
};