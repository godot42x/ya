#include "GameEditor/UI/Shell/EditorProjectBrowserPage.h"

#include "GameEditor/EditorLayer.h"
#include "GameEditor/UI/Shell/EditorListRows.h"
#include "GameEditor/UI/Shell/EditorTheme.h"
#include "GUI/Declarative/Build.h"
#include "GUI/Layout/UICanvasLayout.h"
#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Border.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Container.h"
#include "GUI/Widgets/Controls/Image.h"
#include "GUI/Widgets/Controls/TextField.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <format>
#include <glm/glm.hpp>

namespace ya
{

namespace
{

/// The launcher card (UE/Godot-style compact chooser) centered in the window;
/// the selector is a dialog-scale surface, not a fullscreen page.
constexpr glm::vec2 kProjectBrowserCardSize{880.0f, 560.0f};

/// Breathing room kept around the card; a window smaller than that clamps it
/// to the floor instead of letting it overflow.
constexpr glm::vec2 kProjectBrowserMinCardSize{320.0f, 240.0f};
constexpr glm::vec2 kProjectBrowserWindowMargin{48.0f, 48.0f};

/// The canvas slot hosting `widget`, if the parent gave it one — the page
/// keeps the card slot to clamp it against the live window extent.
UICanvasSlot* canvasSlotOf(UIElement& parent, UIElement& widget)
{
    if (UISlot* slot = parent.getSlotForChild(widget)) {
        return dynamic_cast<UICanvasSlot*>(slot);
    }
    return nullptr;
}

} // namespace

void EditorProjectBrowserPage::build(EditorLayer& layer, WidgetTree& tree)
{
    _layer = &layer;
    _tree  = &tree;
    _roots = std::make_shared<ReactiveList<UITreeView::FNode>>();
    _selection = std::make_shared<SelectionModel>();

    auto exitBtn = labeledButton("ExitEditor", "Exit")
                       .setOnClick([this]() {
                           if (_layer) {
                               _layer->cmdRequestQuit();
                           }
                       });

    auto searchField = ui::textField("ProjectSearch")
                           .setStyleKey(editorStyle(StyleKey::TextField))
                           .setOnTextChanged([this](const std::string& text) {
                               _filter = text;
                               markRowsDirty();
                           });

    auto list = ui::treeView("ProjectList")
                    .bindData(_roots)
                    .bindSelection(_selection->primaryRef())
                    .setOnSelectionChanged([this](const std::string& id) {
                        _selection->select(id);
                        selectRow(id);
                    });
    _list = list.share();

    auto refreshBtn = labeledButton("RefreshProjects", "Refresh")
                          .setOnClick([this]() {
                              if (_layer) {
                                  _layer->requestRefreshProjectBrowser();
                              }
                              markRowsDirty();
                          });
    auto openBtn = labeledButton("OpenProject", "Open Project")
                       .setOnClick([this]() {
                           if (!onOpenRequested || !_layer) {
                               return;
                           }
                           const auto& projects = _layer->getDiscoveredProjects();
                           const int   index    = _layer->getProjectBrowserSelection();
                           if (index >= 0 && index < static_cast<int>(projects.size())) {
                               onOpenRequested(projects[static_cast<size_t>(index)]);
                           }
                       });

    auto errorText = ui::text("ProjectError").setStyleKey("text.error");
    _errorText = errorText.share();
    auto pathText = ui::text("SelectedProjectPath").setStyleKey("text.muted");
    _pathText = pathText.share();

    auto headerTitle = ui::column("ProjectBrowserTitle")
                           .setSpacing(2.0f)
                           .child(ui::text("ProjectTitle").setText("YA Editor").setStyleKey("text.header"))
                           .child(ui::text("ProjectSubtitle")
                                      .setText("Select a project to open")
                                      .setStyleKey("text.muted"));
    auto headerRow = ui::row("ProjectBrowserHeader")
                         .setSpacing(8.0f)
                         .child(std::move(headerTitle), ui::boxSlot().fillWidth())
                         .child(std::move(exitBtn), ui::boxSlot().preferredSize({96.0f, 26.0f}));

    auto statusColumn = ui::column("ProjectBrowserStatus")
                            .setSpacing(2.0f)
                            .child(std::move(errorText))
                            .child(std::move(pathText));
    auto footerRow = ui::row("ProjectBrowserFooter")
                         .setSpacing(8.0f)
                         .child(std::move(statusColumn), ui::boxSlot().fillWidth())
                         .child(std::move(refreshBtn), ui::boxSlot().preferredSize({110.0f, 26.0f}))
                         .child(std::move(openBtn), ui::boxSlot().preferredSize({150.0f, 26.0f}));

    auto card = ui::column("ProjectBrowserCard")
                    .setPadding({20.0f, 20.0f})
                    .setSpacing(10.0f)
                    .child(std::move(headerRow))
                    .child(std::move(searchField), ui::boxSlot().preferredSize({0.0f, 26.0f}))
                    .child(std::move(list), ui::boxSlot().fill())
                    .child(std::move(footerRow), ui::boxSlot().preferredSize({0.0f, 40.0f}));

    auto* contentLayer = tree.getLayer(WidgetTree::ELayer::Content);
    const auto cardSlot = ui::canvasSlot()
                              .anchor({0.5f, 0.5f}, {0.5f, 0.5f})
                              .pivot({0.5f, 0.5f})
                              .size(kProjectBrowserCardSize);
    (void)ui::attach(tree,
                     *contentLayer,
                     ui::border("ProjectBrowserBackdrop")
                         .setStyleKey("panel.canvas")
                         .setVisibility(EWidgetVisibility::HitTestInvisible)
                         .release(),
                     ui::canvasSlot().fill());
    (void)ui::attach(tree,
                     *contentLayer,
                     ui::border("ProjectBrowserCardFill")
                         .setStyleKey("panel.surface")
                         .release(),
                     cardSlot);
    auto cardWidget = std::shared_ptr<UIElement>(std::move(card).release());
    (void)ui::attach(tree, *contentLayer, cardWidget, cardSlot);
    _cardSlot = canvasSlotOf(*contentLayer, *cardWidget);

    markRowsDirty();
}

void EditorProjectBrowserPage::tick()
{
    if (!_layer || !_tree) {
        return;
    }
    // The card keeps its preferred size while the window is large enough and
    // shrinks (staying centered by its pivot) once it is not.
    if (_cardSlot) {
        const Extent2D extent = _tree->getLogicalExtent();
        const glm::vec2 usable = glm::max(glm::vec2{static_cast<float>(extent.width),
                                                    static_cast<float>(extent.height)} -
                                              kProjectBrowserWindowMargin,
                                          glm::vec2{0.0f});
        _cardSlot->setMaxSize(glm::clamp(usable, kProjectBrowserMinCardSize, kProjectBrowserCardSize));
    }
    if (_bRowsDirty) {
        flushRows();
    }
}

void EditorProjectBrowserPage::markRowsDirty()
{
    _bRowsDirty = true;
}

void EditorProjectBrowserPage::flushRows()
{
    _bRowsDirty = false;
    if (!_layer) {
        return;
    }

    const auto matchesFilter = [](const std::string& projectPath, const std::string& filter) {
        if (filter.empty()) {
            return true;
        }
        const auto containsFolded = [&filter](const std::string& haystack) {
            const auto folded = [](const std::string& text) {
                std::string lower;
                lower.reserve(text.size());
                for (char c : text) {
                    lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                }
                return lower;
            };
            return folded(haystack).find(folded(filter)) != std::string::npos;
        };
        return containsFolded(std::filesystem::path(projectPath).filename().string()) ||
               containsFolded(projectPath);
    };

    const auto& discovered = _layer->getDiscoveredProjects();
    std::vector<UITreeView::FNode> rows;
    rows.reserve(discovered.size());
    _rowToProject.clear();
    for (int i = 0; i < static_cast<int>(discovered.size()); ++i) {
        const std::string& projectPath = discovered[static_cast<size_t>(i)];
        if (!matchesFilter(projectPath, _filter)) {
            continue;
        }
        rows.push_back(UITreeView::FNode{
            .id    = std::to_string(rows.size()),
            .label = std::filesystem::path(projectPath).stem().string(),
            .icon  = {.resource = editor_icons::kFolder},
        });
        _rowToProject.push_back(i);
    }
    if (_roots) {
        _roots->replace(std::move(rows));
    }

    // Keep the selection pointing at the same project across filter changes;
    // when it left the filtered view (or is unset), take the first row.
    const auto& mapping      = _rowToProject;
    const int   current       = _layer->getProjectBrowserSelection();
    const auto  rowOfCurrent  = std::find(mapping.begin(), mapping.end(), current);
    if (!mapping.empty()) {
        const int row = rowOfCurrent != mapping.end()
                            ? static_cast<int>(rowOfCurrent - mapping.begin())
                            : 0;
        if (rowOfCurrent == mapping.end()) {
            _layer->setProjectBrowserSelection(mapping.front());
        }
        if (_selection) {
            _selection->select(std::to_string(row));
        }
    }
    else {
        _layer->setProjectBrowserSelection(-1);
        if (_selection) {
            _selection->select("");
        }
    }

    if (_errorText) {
        const std::string& error = _layer->getProjectBrowserError();
        _errorText->setText(error);
        _errorText->setVisibility(error.empty() ? EWidgetVisibility::Collapsed
                                                : EWidgetVisibility::Visible);
    }
    if (_pathText) {
        const int   selected      = _layer->getProjectBrowserSelection();
        const bool  bHasSelection = selected >= 0 && selected < static_cast<int>(discovered.size());
        _pathText->setText(bHasSelection ? discovered[static_cast<size_t>(selected)] : std::string{});
        _pathText->setVisibility(bHasSelection ? EWidgetVisibility::Visible
                                               : EWidgetVisibility::Collapsed);
    }
}

void EditorProjectBrowserPage::selectRow(const std::string& rowId)
{
    if (!_layer) {
        return;
    }
    int row = -1;
    if (auto [ptr, ec] = std::from_chars(rowId.data(), rowId.data() + rowId.size(), row);
        ec != std::errc{} || ptr != rowId.data() + rowId.size() || row < 0 ||
        row >= static_cast<int>(_rowToProject.size())) {
        return;
    }
    const auto& discovered = _layer->getDiscoveredProjects();
    const int   index      = _rowToProject[static_cast<size_t>(row)];
    _layer->setProjectBrowserSelection(index);
    if (_pathText && index >= 0 && index < static_cast<int>(discovered.size())) {
        _pathText->setText(discovered[static_cast<size_t>(index)]);
        _pathText->setVisibility(EWidgetVisibility::Visible);
    }
}

} // namespace ya
