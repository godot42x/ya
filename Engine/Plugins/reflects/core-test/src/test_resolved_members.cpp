#include "test_common.h"

namespace
{

struct RR6Grand
{
    int fromGrand = 1;
};

struct RR6Mid : RR6Grand
{
    int fromMid = 2;
};

struct RR6Leaf : RR6Mid
{
    int fromLeaf = 3;
    int fromMid  = 4;
};

struct RR6Left
{
    int shared   = 1;
    int onlyLeft = 2;
};

struct RR6Right
{
    int shared    = 3;
    int onlyRight = 4;
};

struct RR6Child : RR6Left, RR6Right
{
    int own = 5;
};

struct RR6NameBase
{
    int text = 1;
};

struct RR6NameMid : RR6NameBase
{
    int _text = 2;
};

struct RR6NameLeaf : RR6NameMid
{
    int text = 3;
};

Class* registerTree()
{
    static bool bRegistered = false;
    if (!bRegistered) {
        bRegistered = true;
        Register<RR6Grand>("RR6Grand").property("fromGrand", &RR6Grand::fromGrand);
        Register<RR6Mid>("RR6Mid").parentClass<RR6Grand>().property("fromMid", &RR6Mid::fromMid);
        Register<RR6Leaf>("RR6Leaf")
            .parentClass<RR6Mid>()
            .property("fromLeaf", &RR6Leaf::fromLeaf)
            .property("fromMid", &RR6Leaf::fromMid);
        Register<RR6Left>("RR6Left")
            .property("shared", &RR6Left::shared)
            .property("onlyLeft", &RR6Left::onlyLeft);
        Register<RR6Right>("RR6Right")
            .property("shared", &RR6Right::shared)
            .property("onlyRight", &RR6Right::onlyRight);
        Register<RR6Child>("RR6Child")
            .parentClass<RR6Left>()
            .parentClass<RR6Right>()
            .property("own", &RR6Child::own);
        Register<RR6NameBase>("RR6NameBase").property("text", &RR6NameBase::text);
        Register<RR6NameMid>("RR6NameMid").parentClass<RR6NameBase>().property("_text", &RR6NameMid::_text);
        Register<RR6NameLeaf>("RR6NameLeaf").parentClass<RR6NameMid>().property("text", &RR6NameLeaf::text);
    }
    return ClassRegistry::instance().getClass(refl::type_index_v<RR6Leaf>);
}

void* ownerOf(void* object, refl::type_index_t type, const std::vector<refl::type_index_t>& path)
{
    for (const refl::type_index_t parent : path) {
        const Class* cls = object ? ClassRegistry::instance().getClass(type) : nullptr;
        object           = cls ? cls->getParentPointer(object, parent) : nullptr;
        type             = parent;
    }
    return object;
}

} // namespace

TEST(ResolvedMembers, DerivedNameReplacesInheritedAndPathReachesBase)
{
    Class* leaf = registerTree();
    ASSERT_NE(leaf, nullptr);
    const ResolvedMembers members = leaf->resolveMembers();

    const auto mid = members.properties.find("fromMid");
    ASSERT_NE(mid, members.properties.end());
    EXPECT_TRUE(mid->second.ownerPath.empty());
    EXPECT_EQ(mid->second.property, leaf->getProperty("fromMid"));

    const auto grand = members.properties.find("fromGrand");
    ASSERT_NE(grand, members.properties.end());
    ASSERT_EQ(grand->second.ownerPath.size(), 2u);
    EXPECT_EQ(grand->second.ownerPath[0], refl::type_index_v<RR6Mid>);
    EXPECT_EQ(grand->second.ownerPath[1], refl::type_index_v<RR6Grand>);

    RR6Leaf object;
    object.fromGrand = 11;
    object.RR6Mid::fromMid = 12;
    object.fromMid = 13;
    void* grandObject = ownerOf(&object, leaf->typeIndex, grand->second.ownerPath);
    ASSERT_NE(grandObject, nullptr);
    EXPECT_EQ(static_cast<RR6Grand*>(grandObject)->fromGrand, 11);
    EXPECT_EQ(static_cast<RR6Grand*>(grandObject), static_cast<RR6Grand*>(&object));
}

TEST(ResolvedMembers, LaterParentWinsASharedName)
{
    registerTree();
    Class* child = ClassRegistry::instance().getClass(refl::type_index_v<RR6Child>);
    ASSERT_NE(child, nullptr);
    const ResolvedMembers members = child->resolveMembers();

    const auto shared = members.properties.find("shared");
    ASSERT_NE(shared, members.properties.end());
    ASSERT_EQ(shared->second.ownerPath.size(), 1u);
    EXPECT_EQ(shared->second.ownerPath[0], refl::type_index_v<RR6Right>);

    const auto left = members.properties.find("onlyLeft");
    ASSERT_NE(left, members.properties.end());
    ASSERT_EQ(left->second.ownerPath.size(), 1u);
    EXPECT_EQ(left->second.ownerPath[0], refl::type_index_v<RR6Left>);

    EXPECT_TRUE(members.properties.at("own").ownerPath.empty());
}

TEST(ResolvedMembers, LaterScriptNameCollisionStaysLast)
{
    registerTree();
    Class* leaf = ClassRegistry::instance().getClass(refl::type_index_v<RR6NameLeaf>);
    ASSERT_NE(leaf, nullptr);
    const ResolvedMembers members = leaf->resolveMembers();

    ASSERT_EQ(members.propertyOrder.size(), 2u);
    EXPECT_EQ(members.propertyOrder[0], "_text");
    EXPECT_EQ(members.propertyOrder[1], "text");
    EXPECT_EQ(members.properties.at("text").property, leaf->getProperty("text"));
    EXPECT_TRUE(members.properties.at("text").ownerPath.empty());
    ASSERT_EQ(members.properties.at("_text").ownerPath.size(), 1u);
    EXPECT_EQ(members.properties.at("_text").ownerPath[0], refl::type_index_v<RR6NameMid>);
}
