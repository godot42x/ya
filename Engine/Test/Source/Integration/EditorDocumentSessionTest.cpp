#include "GameEditor/UI/Shell/EditorDocumentSession.h"
#include "GameEditor/UI/Shell/EditorWindowRegistry.h"
#include "GameEditor/UI/Shell/EditorWindowSession.h"

#include "GUI/Binding/UndoStack.h"

#include <gtest/gtest.h>

namespace ya
{

namespace
{

FUndoCommand makeUndo(std::string label)
{
    FUndoCommand command;
    command.label = std::move(label);
    command.undo  = [] {};
    command.redo  = [] {};
    return command;
}

} // namespace

TEST(EditorDocumentSessionTest, OpenSameIdIsSingleton)
{
    EditorDocumentRegistry documents;
    const FEditorDocumentId id = makeEditorSceneDocumentId("maps/town.yascene");
    EditorDocumentSession* a = documents.open(id, EEditorDocumentClosePolicy::Discard);
    EditorDocumentSession* b = documents.open(id, EEditorDocumentClosePolicy::Discard);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a, b);
    EXPECT_EQ(documents.size(), 1u);
    EXPECT_EQ(documents.find(id), a);
}

TEST(EditorDocumentSessionTest, RejectIfDirtyHonorsRequestAndSave)
{
    EditorDocumentRegistry documents;
    const FEditorDocumentId id = makeEditorUIDocumentId("panel-a");
    EditorDocumentSession* session = documents.open(id, EEditorDocumentClosePolicy::RejectIfDirty);
    ASSERT_NE(session, nullptr);
    session->markDirty();
    EXPECT_EQ(documents.close(id, EEditorDocumentCloseMode::Request),
              EEditorDocumentCloseResult::RejectedDirty);
    EXPECT_EQ(documents.find(id), session);
    session->clearDirty();
    EXPECT_EQ(documents.close(id, EEditorDocumentCloseMode::Request),
              EEditorDocumentCloseResult::Closed);
    EXPECT_EQ(documents.find(id), nullptr);
}

TEST(EditorDocumentSessionTest, DiscardCloseDropsDirty)
{
    EditorDocumentRegistry documents;
    const FEditorDocumentId id = makeEditorUIDocumentId("panel-b");
    EditorDocumentSession* session = documents.open(id, EEditorDocumentClosePolicy::RejectIfDirty);
    ASSERT_NE(session, nullptr);
    session->markDirty();
    EXPECT_EQ(documents.close(id, EEditorDocumentCloseMode::Discard),
              EEditorDocumentCloseResult::Closed);
    EXPECT_EQ(documents.find(id), nullptr);
}

TEST(EditorDocumentSessionTest, LockedRejectsUntilForce)
{
    EditorDocumentRegistry documents;
    const FEditorDocumentId id = makeEditorSceneDocumentId("locked");
    ASSERT_NE(documents.open(id, EEditorDocumentClosePolicy::Locked), nullptr);
    EXPECT_EQ(documents.close(id, EEditorDocumentCloseMode::Request),
              EEditorDocumentCloseResult::RejectedLocked);
    EXPECT_EQ(documents.close(id, EEditorDocumentCloseMode::Discard),
              EEditorDocumentCloseResult::RejectedLocked);
    EXPECT_EQ(documents.close(id, EEditorDocumentCloseMode::Force),
              EEditorDocumentCloseResult::Closed);
    EXPECT_EQ(documents.find(id), nullptr);
}

TEST(EditorDocumentSessionTest, CloseRejectsWhileBound)
{
    EditorDocumentRegistry documents;
    const FEditorDocumentId id = makeEditorSceneDocumentId("shared");
    EditorDocumentSession* session = documents.open(id, EEditorDocumentClosePolicy::Discard);
    ASSERT_NE(session, nullptr);
    session->addBind();
    session->addBind();
    EXPECT_EQ(documents.close(id, EEditorDocumentCloseMode::Request),
              EEditorDocumentCloseResult::RejectedLocked);
    session->releaseBind();
    EXPECT_EQ(documents.close(id, EEditorDocumentCloseMode::Request),
              EEditorDocumentCloseResult::RejectedLocked);
    session->releaseBind();
    EXPECT_EQ(documents.close(id, EEditorDocumentCloseMode::Request),
              EEditorDocumentCloseResult::Closed);
}

TEST(EditorDocumentSessionTest, PreviewClaimIsExclusivePerKind)
{
    EditorDocumentRegistry documents;
    const FEditorDocumentId uiA = makeEditorUIDocumentId("a");
    const FEditorDocumentId uiB = makeEditorUIDocumentId("b");
    const FEditorDocumentId mat = FEditorDocumentId{EEditorDocumentKind::Material, "m"};
    ASSERT_NE(documents.open(uiA, EEditorDocumentClosePolicy::Discard), nullptr);
    ASSERT_NE(documents.open(uiB, EEditorDocumentClosePolicy::Discard), nullptr);
    ASSERT_NE(documents.open(mat, EEditorDocumentClosePolicy::Discard), nullptr);

    EXPECT_TRUE(documents.claimPreview(uiA));
    EXPECT_TRUE(documents.claimPreview(uiA));
    EXPECT_FALSE(documents.claimPreview(uiB));
    EXPECT_TRUE(documents.claimPreview(mat));

    documents.releasePreview(uiA);
    EXPECT_TRUE(documents.claimPreview(uiB));
    EXPECT_FALSE(documents.claimPreview(uiA));
}

TEST(EditorDocumentSessionTest, TwoWindowsShareSceneDocumentUndo)
{
    EditorDocumentRegistry documents;
    EditorWindowRegistry   windows;
    windows.defaultSession().bindSceneDocument(documents, "maps/town.yascene");
    EditorWindowSession* extra = windows.create(2);
    ASSERT_NE(extra, nullptr);
    extra->bindSceneDocument(documents, "maps/town.yascene");

    EditorDocumentSession* shared = windows.defaultSession().activeRoot().document();
    ASSERT_NE(shared, nullptr);
    EXPECT_EQ(extra->activeRoot().document(), shared);
    EXPECT_EQ(&windows.defaultSession().activeRoot().undo(), &extra->activeRoot().undo());
    EXPECT_TRUE(windows.defaultSession().activeRoot().undo().push(makeUndo("edit")));
    EXPECT_TRUE(extra->activeRoot().undo().canUndo());

    extra->bindSceneDocument(documents, "maps/other.yascene");
    EXPECT_NE(extra->activeRoot().document(), shared);
    ASSERT_NE(documents.find(makeEditorSceneDocumentId("maps/town.yascene")), nullptr);
    EXPECT_EQ(documents.find(makeEditorSceneDocumentId("maps/town.yascene"))->bindCount(), 1);

    windows.defaultSession().bindSceneDocument(documents, "maps/other.yascene");
    EXPECT_EQ(windows.defaultSession().activeRoot().document(), extra->activeRoot().document());
    EXPECT_EQ(documents.find(makeEditorSceneDocumentId("maps/town.yascene")), nullptr);
}

} // namespace ya
