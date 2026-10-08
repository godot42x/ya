#include "reflects-core/lib.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace
{

const refl::TypeOps *mustFind(std::string_view name)
{
    const refl::TypeOps *ops = refl::TypeRegistry::instance().findByName(name);
    EXPECT_NE(ops, nullptr) << name;
    return ops;
}

struct RR1Obj
{
    int value = 0;
};

struct RR1Mismatch
{
    int value = 0;
};

struct RR1NameA
{
};
struct RR1NameB
{
};
struct RR1ClaimInt
{
};

struct RR1Once
{
    int value = 0;
};

struct RR1CompilerLeft
{
};
struct RR1CompilerRight
{
};

enum class RR1SignedEnum : int
{
    A = -1
};
enum class RR1UnsignedEnum : unsigned
{
    A = 1
};

struct ErrorFacet
{
    static constexpr refl::EFacetMissing missing = refl::EFacetMissing::Error;
    int                                   value  = 0;
};

struct FallbackFacet
{
    static constexpr refl::EFacetMissing missing = refl::EFacetMissing::Fallback;
    int                                   value  = 0;

    static const FallbackFacet &fallback()
    {
        static const FallbackFacet instance{7};
        return instance;
    }
};

struct SkipFacet
{
    static constexpr refl::EFacetMissing missing = refl::EFacetMissing::Skip;
    int                                   value  = 0;
};

struct CountingFacet
{
    static constexpr refl::EFacetMissing missing = refl::EFacetMissing::Skip;
    static int                           alive;

    CountingFacet() { ++alive; }
    CountingFacet(const CountingFacet &) { ++alive; }
    CountingFacet(CountingFacet &&) noexcept { ++alive; }
    ~CountingFacet() { --alive; }
};

int CountingFacet::alive = 0;

struct HookReset
{
    ~HookReset() { refl::TypeRegistry::instance().missingFacetHook = nullptr; }
};

} // namespace

TEST(TypeOps, BuiltinFundamentals)
{
    auto &reg = refl::TypeRegistry::instance();

    struct Row
    {
        const char      *name;
        refl::EValueKind kind;
        bool             bSigned;
        size_t           size;
    };
    const Row rows[] = {
        {"bool", refl::EValueKind::Bool, false, sizeof(bool)},
        {"signed char", refl::EValueKind::Int, true, sizeof(signed char)},
        {"unsigned char", refl::EValueKind::Int, false, sizeof(unsigned char)},
        {"short", refl::EValueKind::Int, true, sizeof(short)},
        {"unsigned short", refl::EValueKind::Int, false, sizeof(unsigned short)},
        {"int", refl::EValueKind::Int, true, sizeof(int)},
        {"unsigned int", refl::EValueKind::Int, false, sizeof(unsigned int)},
        {"long", refl::EValueKind::Int, true, sizeof(long)},
        {"unsigned long", refl::EValueKind::Int, false, sizeof(unsigned long)},
        {"long long", refl::EValueKind::Int, true, sizeof(long long)},
        {"unsigned long long", refl::EValueKind::Int, false, sizeof(unsigned long long)},
        {"float", refl::EValueKind::Float, false, sizeof(float)},
        {"double", refl::EValueKind::Float, false, sizeof(double)},
        {"std::string", refl::EValueKind::String, false, sizeof(std::string)},
    };

    for (const Row &row : rows) {
        const refl::TypeOps *ops = mustFind(row.name);
        ASSERT_NE(ops, nullptr) << row.name;
        EXPECT_EQ(ops->kind, row.kind) << row.name;
        EXPECT_EQ(ops->bSigned, row.bSigned) << row.name;
        EXPECT_EQ(ops->life.size, row.size) << row.name;
        EXPECT_GT(ops->life.align, 0u) << row.name;
        EXPECT_EQ(reg.find(ops->type), ops) << row.name;
    }

    const refl::TypeOps *charOps = mustFind("char");
    ASSERT_NE(charOps, nullptr);
    EXPECT_EQ(charOps->kind, refl::EValueKind::Int);
    EXPECT_EQ(charOps->life.size, sizeof(char));
    EXPECT_EQ(charOps->bSigned, std::is_signed_v<char>);
    EXPECT_EQ(charOps->bTriviallyCopyable, true);

    const refl::TypeOps *stringOps = mustFind("std::string");
    ASSERT_NE(stringOps, nullptr);
    EXPECT_FALSE(stringOps->bTriviallyCopyable);
    EXPECT_NE(stringOps->life.copyConstruct, nullptr);
    EXPECT_NE(stringOps->life.defaultConstruct, nullptr);
    EXPECT_NE(stringOps->life.destroy, nullptr);

    EXPECT_NE(refl::type_index_v<char>, refl::type_index_v<signed char>);
    EXPECT_NE(refl::type_index_v<char>, refl::type_index_v<unsigned char>);
    EXPECT_NE(refl::type_index_v<signed char>, refl::type_index_v<unsigned char>);
    EXPECT_NE(refl::type_index_v<int>, refl::type_index_v<long>);
    EXPECT_EQ(refl::type_index_v<short>, refl::type_index_v<short int>);
    EXPECT_EQ(reg.find(refl::type_index_v<short int>)->name, "short");
    EXPECT_EQ(refl::type_index_v<unsigned>, refl::type_index_v<unsigned int>);
    EXPECT_EQ(reg.find(refl::type_index_v<unsigned>)->name, "unsigned int");

    if constexpr (refl::type_index_v<int> == refl::type_index_v<int32_t>) {
        EXPECT_EQ(reg.find(refl::type_index_v<int32_t>)->name, "int");
    }
    if constexpr (refl::type_index_v<long> == refl::type_index_v<int64_t>) {
        EXPECT_EQ(reg.find(refl::type_index_v<int64_t>)->name, "long");
    }
    else if constexpr (refl::type_index_v<long long> == refl::type_index_v<int64_t>) {
        EXPECT_EQ(reg.find(refl::type_index_v<int64_t>)->name, "long long");
    }
}

TEST(TypeOps, CollisionSameTypeDifferentName)
{
    auto &reg = refl::TypeRegistry::instance();
    EXPECT_THROW(reg.registerType<int>("RR1NotInt"), std::runtime_error);
    EXPECT_EQ(reg.find(refl::type_index_v<int>)->name, "int");

    auto &first = reg.registerType<RR1Once>("RR1Once");
    EXPECT_THROW(reg.registerType<RR1Once>("RR1OnceOther"), std::runtime_error);
    EXPECT_EQ(reg.find(refl::type_index_v<RR1Once>), &first);
    EXPECT_EQ(first.name, "RR1Once");
}

TEST(TypeOps, CollisionDifferentTypeSameName)
{
    auto &reg = refl::TypeRegistry::instance();

    refl::TypeOps ops;
    ops.type       = refl::type_index_v<RR1ClaimInt>;
    ops.name       = "int";
    ops.life.size  = sizeof(RR1ClaimInt);
    ops.life.align = alignof(RR1ClaimInt);
    ops.kind  = refl::EValueKind::Object;
    EXPECT_THROW(reg.registerTypeRaw(std::move(ops)), std::runtime_error);
    EXPECT_EQ(reg.findByName("int")->type, refl::type_index_v<int>);
    EXPECT_EQ(reg.find(refl::type_index_v<RR1ClaimInt>), nullptr);
}

TEST(TypeOps, CollisionSameTypeSameNameReturnsExisting)
{
    auto       &reg      = refl::TypeRegistry::instance();
    const auto *existing = reg.find(refl::type_index_v<int>);
    ASSERT_NE(existing, nullptr);

    refl::TypeOps ops;
    ops.type       = refl::type_index_v<int>;
    ops.name       = "int";
    ops.life.size  = 123;
    ops.life.align = 1;
    ops.kind       = refl::EValueKind::Object;
    refl::TypeOps &row = reg.registerTypeRaw(std::move(ops));
    EXPECT_EQ(&row, existing);
    EXPECT_EQ(row.life.size, sizeof(int));
    EXPECT_EQ(row.kind, refl::EValueKind::Int);

    refl::TypeOps &again = reg.registerType<RR1Once>("RR1Once");
    EXPECT_EQ(&again, reg.find(refl::type_index_v<RR1Once>));
    EXPECT_EQ(again.life.size, sizeof(RR1Once));
}

TEST(TypeOps, RegisterClassCreatesObjectRow)
{
    auto cls = ClassRegistry::instance().registerClass<RR1Obj>("RR1Obj", new Class("RR1Obj"));
    ASSERT_NE(cls, nullptr);
    EXPECT_EQ(cls->name, "RR1Obj");

    const refl::TypeOps *ops = refl::TypeRegistry::instance().find(refl::type_index_v<RR1Obj>);
    ASSERT_NE(ops, nullptr);
    EXPECT_EQ(ops->kind, refl::EValueKind::Object);
    EXPECT_EQ(ops->name, "RR1Obj");
    EXPECT_EQ(ops->life.size, sizeof(RR1Obj));
    EXPECT_NE(ops->life.defaultConstruct, nullptr);
    EXPECT_NE(ops->life.copyConstruct, nullptr);

    auto again = ClassRegistry::instance().registerClass<RR1Obj>("RR1Obj", new Class("RR1Obj"));
    EXPECT_EQ(again, cls);
    EXPECT_EQ(refl::TypeRegistry::instance().find(refl::type_index_v<RR1Obj>), ops);
}

TEST(TypeOps, RegisterClassNameMismatchThrows)
{
    auto &reg = refl::TypeRegistry::instance();
    reg.registerType<RR1Mismatch>("RR1MismatchOps");
    EXPECT_THROW(ClassRegistry::instance().registerClass<RR1Mismatch>("RR1Mismatch", new Class("RR1Mismatch")),
                 std::runtime_error);
    EXPECT_EQ(reg.find(refl::type_index_v<RR1Mismatch>)->name, "RR1MismatchOps");

    ClassRegistry::instance().registerClass<RR1NameA>("RR1Shared", new Class("RR1Shared"));
    EXPECT_THROW(ClassRegistry::instance().registerClass<RR1NameB>("RR1Shared", new Class("RR1Shared")),
                 std::runtime_error);
    EXPECT_EQ(reg.findByName("RR1Shared")->type, refl::type_index_v<RR1NameA>);
}

TEST(TypeOps, CollisionMessageIncludesCompilerNames)
{
    auto             &reg         = refl::TypeRegistry::instance();
    const std::string intCompiler(refl::detail::compilerTypeName<int>());
    try {
        reg.registerType<int>("RR1NotIntNamed");
        FAIL() << "expected id collision";
    }
    catch (const std::runtime_error &ex) {
        const std::string msg = ex.what();
        EXPECT_NE(msg.find("Reflection type id collision between int and RR1NotIntNamed"), std::string::npos);
        EXPECT_NE(msg.find(intCompiler + " vs " + intCompiler), std::string::npos);
    }
    EXPECT_EQ(reg.find(refl::type_index_v<int>)->name, "int");

    reg.registerType<RR1CompilerLeft>("RR1CompilerShared");
    const std::string left(refl::detail::compilerTypeName<RR1CompilerLeft>());
    const std::string right(refl::detail::compilerTypeName<RR1CompilerRight>());
    try {
        reg.registerType<RR1CompilerRight>("RR1CompilerShared");
        FAIL() << "expected name collision";
    }
    catch (const std::runtime_error &ex) {
        const std::string msg = ex.what();
        EXPECT_NE(msg.find("Reflection type name collision on 'RR1CompilerShared'"), std::string::npos);
        EXPECT_NE(msg.find(left + " vs " + right), std::string::npos);
    }
    EXPECT_EQ(reg.find(refl::type_index_v<RR1CompilerRight>), nullptr);

    try {
        ClassRegistry::instance().registerClass<RR1CompilerRight>("RR1CompilerShared", new Class("RR1CompilerShared"));
        FAIL() << "expected registerClass name collision";
    }
    catch (const std::runtime_error &ex) {
        const std::string msg = ex.what();
        EXPECT_NE(msg.find(left), std::string::npos);
        EXPECT_NE(msg.find(right), std::string::npos);
    }
    EXPECT_EQ(ClassRegistry::instance().getClass("RR1CompilerShared"), nullptr);
    EXPECT_EQ(reg.findByName("RR1CompilerShared")->type, refl::type_index_v<RR1CompilerLeft>);
}

TEST(TypeOps, RegisterTypeDeduceEnumKind)
{
    auto &reg = refl::TypeRegistry::instance();
    auto &ops = reg.registerType<RR1SignedEnum>("RR1SignedEnum");
    EXPECT_EQ(ops.kind, refl::EValueKind::Enum);
    EXPECT_TRUE(ops.bSigned);
    EXPECT_EQ(ops.life.size, sizeof(RR1SignedEnum));

    auto &uns = reg.registerType<RR1UnsignedEnum>("RR1UnsignedEnum");
    EXPECT_EQ(uns.kind, refl::EValueKind::Enum);
    EXPECT_FALSE(uns.bSigned);
}

TEST(TypeOps, FacetSetGetAndReplace)
{
    auto &reg = refl::TypeRegistry::instance();
    reg.registerType<RR1Obj>("RR1Obj");
    const auto type = refl::type_index_v<RR1Obj>;

    EXPECT_EQ(reg.facet<ErrorFacet>(type), nullptr);
    reg.setFacet(type, ErrorFacet{3});
    ASSERT_NE(reg.facet<ErrorFacet>(type), nullptr);
    EXPECT_EQ(reg.facet<ErrorFacet>(type)->value, 3);

    reg.setFacet(type, SkipFacet{9});
    EXPECT_EQ(reg.facet<ErrorFacet>(type)->value, 3);
    EXPECT_EQ(reg.facet<SkipFacet>(type)->value, 9);

    reg.setFacet(type, ErrorFacet{4});
    EXPECT_EQ(reg.facet<ErrorFacet>(type)->value, 4);

    CountingFacet::alive = 0;
    reg.setFacet(type, CountingFacet{});
    EXPECT_EQ(CountingFacet::alive, 1);
    reg.setFacet(type, CountingFacet{});
    EXPECT_EQ(CountingFacet::alive, 1);
    EXPECT_NE(reg.resolveFacet<CountingFacet>(type), nullptr);

    struct Unregistered
    {
    };
    EXPECT_THROW(reg.setFacet(refl::type_index_v<Unregistered>, ErrorFacet{}), std::runtime_error);
}

TEST(TypeOps, FacetMissingErrorCallsHook)
{
    HookReset reset;
    auto     &reg = refl::TypeRegistry::instance();
    reg.registerType<RR1NameB>("RR1NameB");
    const auto type = refl::type_index_v<RR1NameB>;

    int         calls = 0;
    std::string seen;
    reg.missingFacetHook = [&](refl::type_index_t got, std::string_view facetName) {
        ++calls;
        EXPECT_EQ(got, type);
        seen = std::string(facetName);
    };

    EXPECT_EQ(reg.facet<ErrorFacet>(type), nullptr);
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(reg.resolveFacet<ErrorFacet>(type), nullptr);
    EXPECT_EQ(calls, 1);
    EXPECT_FALSE(seen.empty());

    reg.setFacet(type, ErrorFacet{11});
    ASSERT_NE(reg.resolveFacet<ErrorFacet>(type), nullptr);
    EXPECT_EQ(reg.resolveFacet<ErrorFacet>(type)->value, 11);
    EXPECT_EQ(calls, 1);
}

TEST(TypeOps, FacetMissingFallback)
{
    auto &reg = refl::TypeRegistry::instance();
    reg.registerType<RR1Once>("RR1Once");
    const auto type = refl::type_index_v<RR1Once>;

    const FallbackFacet *missing = reg.resolveFacet<FallbackFacet>(type);
    ASSERT_NE(missing, nullptr);
    EXPECT_EQ(missing, &FallbackFacet::fallback());
    EXPECT_EQ(missing->value, 7);

    reg.setFacet(type, FallbackFacet{1});
    EXPECT_EQ(reg.resolveFacet<FallbackFacet>(type)->value, 1);
}

TEST(TypeOps, FacetMissingSkip)
{
    HookReset reset;
    auto     &reg  = refl::TypeRegistry::instance();
    int       calls = 0;
    reg.missingFacetHook = [&](refl::type_index_t, std::string_view) { ++calls; };

    const auto type = refl::type_index_v<int>;
    EXPECT_EQ(reg.resolveFacet<SkipFacet>(type), nullptr);
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(reg.facet<SkipFacet>(type), nullptr);
}
