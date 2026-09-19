#include "GameEditor/UI/Dialogs/EditorConfirmDialog.h"

#include "GUI/Widgets/WidgetTree.h"

#include <gtest/gtest.h>

namespace ya
{

TEST(EditorConfirmDialogTest, PrimarySecondaryAndCancelInvokeMatchingCallbacks)
{
    WidgetTree tree({.width = 800, .height = 600});
    EditorConfirmDialog dialog;

    int primary = 0;
    int secondary = 0;
    int cancel = 0;
    FEditorConfirmRequest request;
    request.title       = "Unsaved Changes";
    request.message     = "Save before continuing?";
    request.onPrimary   = [&]() { ++primary; };
    request.onSecondary = [&]() { ++secondary; };
    request.onCancel    = [&]() { ++cancel; };

    dialog.open(tree, request);
    ASSERT_TRUE(dialog.isOpen());
    tree.tick(1.0f / 60.0f);
    dialog.choosePrimary();
    EXPECT_EQ(primary, 1);
    EXPECT_EQ(secondary, 0);
    EXPECT_EQ(cancel, 0);
    EXPECT_FALSE(dialog.isOpen());

    dialog.open(tree, request);
    ASSERT_TRUE(dialog.isOpen());
    dialog.chooseSecondary();
    EXPECT_EQ(primary, 1);
    EXPECT_EQ(secondary, 1);
    EXPECT_EQ(cancel, 0);
    EXPECT_FALSE(dialog.isOpen());

    dialog.open(tree, request);
    ASSERT_TRUE(dialog.isOpen());
    dialog.chooseCancel();
    EXPECT_EQ(primary, 1);
    EXPECT_EQ(secondary, 1);
    EXPECT_EQ(cancel, 1);
    EXPECT_FALSE(dialog.isOpen());
}

TEST(EditorConfirmDialogTest, ChoosingTwiceDoesNotRefire)
{
    WidgetTree tree({.width = 640, .height = 480});
    EditorConfirmDialog dialog;
    int primary = 0;
    FEditorConfirmRequest request;
    request.onPrimary = [&]() { ++primary; };
    dialog.open(tree, std::move(request));
    dialog.choosePrimary();
    dialog.choosePrimary();
    EXPECT_EQ(primary, 1);
}

} // namespace ya
