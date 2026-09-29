// Reflection -> script export shared by every script language (rpg-prototype B1).

#include "Core/Reflection/Reflection.h"
#include "Core/Scripting/ScriptBindings.h"
#include "ECS/Entity.h"
#include "ECS/Systems/Components/CameraComponent.h"
#include "Scene/Core/Scene.h"
#include "Scene/Core/SceneScriptBindings.h"
#include "Scene3D/TransformComponent.h"

#include <gtest/gtest.h>

#include <string>
#include <unordered_map>

namespace ya::script_binding_test
{

enum class EProbeMood : uint8_t
{
    Calm,
    Angry,
};

struct FProbeBase
{
    YA_REFLECT_BEGIN(FProbeBase)
    YA_REFLECT_FIELD(baseValue)
    YA_REFLECT_END()

    int baseValue = 1;
};

struct FProbe : FProbeBase
{
    YA_REFLECT_BEGIN(FProbe, FProbeBase)
    YA_REFLECT_FIELD(_speed)
    YA_REFLECT_FIELD(_label)
    YA_REFLECT_FIELD(bFlag)
    YA_REFLECT_FIELD(_mood)
    YA_REFLECT_FIELD(_matrix)
    YA_REFLECT_METHOD(scaled)
    YA_REFLECT_METHOD(setMood)
    YA_REFLECT_METHOD(getLabel)
    YA_REFLECT_METHOD(getMatrix)
    YA_REFLECT_END()

    float             _speed  = 2.0f;
    const std::string _label  = "probe";
    bool              bFlag   = false;
    EProbeMood        _mood   = EProbeMood::Calm;
    glm::mat4         _matrix = glm::mat4(1.0f);

    [[nodiscard]] float              scaled(float factor) const { return _speed * factor; }
    void                             setMood(EProbeMood mood) { _mood = mood; }
    [[nodiscard]] const std::string& getLabel() const { return _label; }
    [[nodiscard]] const glm::mat4&   getMatrix() const { return _matrix; }
};

} // namespace ya::script_binding_test

YA_REFLECT_ENUM_BEGIN(ya::script_binding_test::EProbeMood)
YA_REFLECT_ENUM_VALUE(Calm)
YA_REFLECT_ENUM_VALUE(Angry)
YA_REFLECT_ENUM_END()

namespace ya::script_binding_test
{
namespace
{

using script::ScriptError;
using script::ScriptRef;
using script::ScriptValue;

/// Probes addressed by id, the way a real provider finds its objects again.
struct FProbeWorld
{
    std::unordered_map<uint64_t, FProbe> probes;
    int                                  writes = 0;

    static FProbeWorld& get()
    {
        static FProbeWorld world;
        return world;
    }

    static uint32_t kind()
    {
        static const uint32_t id = script::registerRefKind(script::ScriptRefKind{
            .name    = "probe",
            .resolve = [](const ScriptRef& ref) -> void* {
                auto& probes = get().probes;
                auto  it     = probes.find(ref.a);
                return it != probes.end() ? &it->second : nullptr;
            },
            .afterWrite = [](const ScriptRef&, void*) { ++get().writes; },
        });
        return id;
    }

    ScriptRef add(uint64_t id)
    {
        probes.erase(id);
        probes.try_emplace(id);
        return ScriptRef{.type = type_index_v<FProbe>, .kind = kind(), .a = id};
    }
};

class ScriptBindingTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        FProbeWorld::get().probes.clear();
        FProbeWorld::get().writes = 0;
    }
};

TEST_F(ScriptBindingTest, ReflectedMembersThatCrossAreVisible)
{
    const type_index_t type = type_index_v<FProbe>;
    EXPECT_NE(script::findField(type, "speed"), nullptr);
    EXPECT_NE(script::findField(type, "label"), nullptr);
    EXPECT_NE(script::findField(type, "bFlag"), nullptr);
    EXPECT_NE(script::findField(type, "baseValue"), nullptr);
    EXPECT_NE(script::findMethod(type, "scaled"), nullptr);
    EXPECT_NE(script::findMethod(type, "getLabel"), nullptr);

    EXPECT_EQ(script::findField(type, "_speed"), nullptr);
    EXPECT_EQ(script::findField(type, "matrix"), nullptr);
    EXPECT_EQ(script::findMethod(type, "getMatrix"), nullptr);
    EXPECT_EQ(script::findMethod(type, "speed"), nullptr);

    EXPECT_EQ(script::findField(type, "speed"), script::findField(type, "speed"));
}

TEST_F(ScriptBindingTest, FieldsReadAndWriteThroughReflection)
{
    const ScriptRef ref   = FProbeWorld::get().add(1);
    FProbe&         probe = FProbeWorld::get().probes.at(1);

    EXPECT_EQ(script::readField(ref, "speed"), ScriptValue{2.0});
    EXPECT_EQ(script::readField(ref, "label"), ScriptValue{std::string("probe")});
    EXPECT_EQ(script::readField(ref, "baseValue"), ScriptValue{int64_t{1}});

    script::writeField(ref, "speed", int64_t{3});
    script::writeField(ref, "bFlag", true);
    script::writeField(ref, "baseValue", int64_t{9});
    EXPECT_FLOAT_EQ(probe._speed, 3.0f);
    EXPECT_TRUE(probe.bFlag);
    EXPECT_EQ(probe.baseValue, 9);
    EXPECT_EQ(FProbeWorld::get().writes, 3);

    EXPECT_THROW(script::writeField(ref, "label", std::string("x")), ScriptError);
    EXPECT_THROW(script::writeField(ref, "speed", std::string("fast")), ScriptError);
    EXPECT_THROW(script::writeField(ref, "matrix", int64_t{1}), ScriptError);
    EXPECT_FLOAT_EQ(probe._speed, 3.0f);
    EXPECT_EQ(FProbeWorld::get().writes, 3);
}

TEST_F(ScriptBindingTest, EnumsTravelAsIntegersAndAcceptNames)
{
    const ScriptRef ref   = FProbeWorld::get().add(1);
    FProbe&         probe = FProbeWorld::get().probes.at(1);

    EXPECT_EQ(script::readField(ref, "mood"), ScriptValue{int64_t{0}});
    script::writeField(ref, "mood", std::string("Angry"));
    EXPECT_EQ(probe._mood, EProbeMood::Angry);
    script::writeField(ref, "mood", int64_t{0});
    EXPECT_EQ(probe._mood, EProbeMood::Calm);

    EXPECT_THROW(script::writeField(ref, "mood", int64_t{5}), ScriptError);
    EXPECT_THROW(script::writeField(ref, "mood", std::string("Sleepy")), ScriptError);
}

TEST_F(ScriptBindingTest, MethodsCallThePluginInvoker)
{
    const ScriptRef ref   = FProbeWorld::get().add(1);
    FProbe&         probe = FProbeWorld::get().probes.at(1);

    const ScriptValue two[] = {ScriptValue{int64_t{2}}};
    EXPECT_EQ(script::callMethod(ref, "scaled", two), ScriptValue{4.0});
    EXPECT_EQ(script::callMethod(ref, "getLabel", {}), ScriptValue{std::string("probe")});

    const ScriptValue angry[] = {ScriptValue{std::string("Angry")}};
    EXPECT_EQ(script::callMethod(ref, "setMood", angry), ScriptValue{});
    EXPECT_EQ(probe._mood, EProbeMood::Angry);

    EXPECT_THROW(script::callMethod(ref, "scaled", {}), ScriptError);
    EXPECT_THROW(script::callMethod(ref, "getMatrix", {}), ScriptError);
}

TEST_F(ScriptBindingTest, GoneObjectsRaiseInsteadOfDangling)
{
    const ScriptRef ref = FProbeWorld::get().add(1);
    FProbeWorld::get().probes.erase(1);

    EXPECT_EQ(script::tryResolve(ref), nullptr);
    EXPECT_THROW((void)script::readField(ref, "speed"), ScriptError);
    EXPECT_THROW(script::callMethod(ref, "getLabel", {}), ScriptError);
}

TEST_F(ScriptBindingTest, EntityRefsReachComponentsAndGoStale)
{
    script::ensureSceneScriptBindings();
    Scene     scene("ScriptBinding");
    Node3D*   node   = scene.createNode3D("Hero");
    Entity*   entity = node->getEntity();
    ScriptRef ref    = script::entityRef(entity);

    EXPECT_EQ(script::callMethod(ref, "getName", {}), ScriptValue{std::string("Hero")});

    const ScriptValue transformValue = script::callMethod(ref, "getTransform", {});
    ASSERT_TRUE(std::holds_alternative<ScriptRef>(transformValue));
    const ScriptRef transform = std::get<ScriptRef>(transformValue);
    script::writeField(transform, "position", glm::vec3(1.0f, 2.0f, 3.0f));
    auto* component = entity->getComponent<TransformComponent>();
    EXPECT_EQ(component->getPosition(), glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_TRUE(component->_localDirty);

    EXPECT_EQ(script::callMethod(ref, "hasCamera", {}), ScriptValue{false});
    EXPECT_EQ(script::callMethod(ref, "getCamera", {}), ScriptValue{});
    const ScriptValue cameraName[] = {ScriptValue{std::string("CameraComponent")}};
    const ScriptValue camera       = script::callMethod(ref, "add", cameraName);
    ASSERT_TRUE(std::holds_alternative<ScriptRef>(camera));
    script::writeField(std::get<ScriptRef>(camera), "bPrimary", true);
    EXPECT_TRUE(entity->getComponent<CameraComponent>()->bPrimary);
    EXPECT_EQ(script::callMethod(ref, "remove", cameraName), ScriptValue{true});
    EXPECT_EQ(script::tryResolve(std::get<ScriptRef>(camera)), nullptr);

    EXPECT_EQ(script::findMethod(ref.type, "getNoSuchThing"), nullptr);
    const ScriptValue unknown[] = {ScriptValue{std::string("NoSuchComponent")}};
    EXPECT_THROW(script::callMethod(ref, "get", unknown), ScriptError);

    scene.destroyEntity(entity);
    EXPECT_EQ(script::tryResolve(ref), nullptr);
    EXPECT_EQ(script::tryResolve(transform), nullptr);
    EXPECT_THROW(script::callMethod(ref, "getName", {}), ScriptError);
}

TEST_F(ScriptBindingTest, SceneRefsDieWithTheirScene)
{
    script::ensureSceneScriptBindings();
    ScriptRef sceneRef;
    ScriptRef entityRef;
    {
        Scene scene("Transient");
        sceneRef  = script::sceneRef(&scene);
        entityRef = script::entityRef(scene.createNode3D("A")->getEntity());
        EXPECT_EQ(script::callMethod(sceneRef, "getName", {}), ScriptValue{std::string("Transient")});
        EXPECT_EQ(script::sceneOf(sceneRef), &scene);
    }
    EXPECT_EQ(script::tryResolve(sceneRef), nullptr);
    EXPECT_EQ(script::tryResolve(entityRef), nullptr);
}

} // namespace
} // namespace ya::script_binding_test
