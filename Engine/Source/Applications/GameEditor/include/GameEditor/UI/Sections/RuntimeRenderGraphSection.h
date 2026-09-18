#pragma once
#include "GUI/Widgets/CompoundWidget.h"
#include <memory>
namespace ya { struct App; struct UIText; class RuntimeRenderGraphSection final : public UICompoundWidget { public: explicit RuntimeRenderGraphSection(std::string name="RuntimeRenderGraph"); void sync(const App* app); protected: void construct() override; private: std::shared_ptr<UIText> _pipeline; std::shared_ptr<UIText> _passes; std::shared_ptr<UIText> _dependencies; std::shared_ptr<UIText> _status; }; }
