#pragma once
#include "GUI/Widgets/CompoundWidget.h"
#include <memory>
namespace ya { struct App; struct UIText; class RuntimeRenderTargetSection final : public UICompoundWidget { public: explicit RuntimeRenderTargetSection(std::string name="RuntimeRenderTargets"); void sync(const App* app); protected: void construct() override; private: std::shared_ptr<UIText> _summary; std::shared_ptr<UIText> _details; }; }
