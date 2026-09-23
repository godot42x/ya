#include "GameEditor/UI/Shell/EditorDocumentSession.h"
#include "GameEditor/UI/Shell/EditorWindowRegistry.h"
#include "GameEditor/UI/Shell/EditorWindowSession.h"

#include "GUI/Binding/UndoStack.h"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iterator>
#include <string>

namespace ya
{

namespace
{

std::string readEngineSource(const std::filesystem::path& relative)
{
    const auto path = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / relative;
    std::ifstream in(path);
    EXPECT_TRUE(in.good()) << "missing " << path.string();
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

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

TEST(EditorDocumentSessionTest, SurfaceAndWindowSessionDoNotOwnDocumentRegistry)
{
    const std::string surfaceH =
        readEngineSource("Source/Applications/GameEditor/include/GameEditor/UI/Shell/EditorSurface.h");
    const std::string sessionH =
        readEngineSource("Source/Applications/GameEditor/include/GameEditor/UI/Shell/EditorWindowSession.h");
    EXPECT_EQ(surfaceH.find("EditorDocumentRegistry _"), std::string::npos);
    EXPECT_NE(surfaceH.find("EditorDocumentRegistry*"), std::string::npos);
    EXPECT_EQ(sessionH.find("EditorDocumentRegistry _"), std::string::npos);
    EXPECT_NE(sessionH.find("EditorDocumentRegistry*"), std::string::npos);

    const std::string moduleCpp =
        readEngineSource("Source/Applications/GameEditor/EditorModule.cpp");
    EXPECT_NE(moduleCpp.find("EditorDocumentRegistry         _documents"), std::string::npos);
    EXPECT_NE(moduleCpp.find("window->bind(*_layer, &_tabSpawners, &_documents)"), std::string::npos);

    const std::string contentCpp =
        readEngineSource("Source/Applications/GameEditor/UI/Tabs/EditorContentBrowserTab.cpp");
    EXPECT_NE(contentCpp.find("EEditorDocumentKind::Script"), std::string::npos);
    EXPECT_NE(contentCpp.find("EEditorDocumentKind::Material"), std::string::npos);
    EXPECT_NE(contentCpp.find("openDocumentEditor"), std::string::npos);
    EXPECT_EQ(contentCpp.find("App::get()"), std::string::npos);
}

} // namespace ya
