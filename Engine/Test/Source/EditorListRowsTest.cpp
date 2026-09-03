#include "GameEditor/UI/EditorListRows.h"
#include "GUI/Widgets/Controls/Text.h"

#include <gtest/gtest.h>

namespace ya
{

TEST(EditorListRowsTest, ContentRowUpdaterRefreshesSelectionWithoutRebuildingInstance)
{
    auto row = contentRow("Entry", "Alpha/", "Alpha", {}, {}).share();
    ASSERT_FALSE(row->_bSelected);

    bool selected = false;
    updateContentRow(*row, "Alpha/", "Alpha", true, [&](const std::string& id) {
        EXPECT_EQ(id, "Alpha");
        selected = true;
    }, {});
    EXPECT_TRUE(row->_bSelected);

    selected = false;
    if (row->_onSelect) {
        row->_onSelect("Alpha");
    }
    EXPECT_TRUE(selected);
    EXPECT_EQ(dynamic_cast<UIText*>(row->getChildren().front().get())->getText(), "Alpha/");
}

} // namespace ya
