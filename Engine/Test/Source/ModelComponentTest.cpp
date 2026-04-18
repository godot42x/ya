#include "Core/Reflection/DeferredInitializer.h"
#include "Core/Reflection/ReflectionSerializer.h"
#include "ECS/Component/ModelComponent.h"

#include <gtest/gtest.h>

namespace ya
{

namespace
{

void ensureReflectionReady()
{
    static bool bInitialized = false;
    if (!bInitialized) {
        reflection::DeferredInitializerQueue::instance().executeAll();
        bInitialized = true;
    }
}

} // namespace

TEST(ModelComponentTest, MaterialModeRoundtrip)
{
    ensureReflectionReady();

    ModelComponent modelComponent;
    modelComponent._modelRef.setPathWithoutNotify("Content/Models/Test.glb");
    modelComponent._materialType            = EModelMaterialType::PBR;
    modelComponent._customMaterialPath      = "Content/Materials/TestMaterial.yamat";
    modelComponent._useEmbeddedMaterials    = false;
    modelComponent._autoCreateChildEntities = false;

    const nlohmann::json json = ReflectionSerializer::serializeByRuntimeReflection(modelComponent);

    ModelComponent loadedModelComponent;
    ReflectionSerializer::deserializeByRuntimeReflection(loadedModelComponent, json, "ModelComponent");

    EXPECT_EQ(loadedModelComponent._modelRef.getPath(), "Content/Models/Test.glb");
    EXPECT_EQ(loadedModelComponent._materialType, EModelMaterialType::PBR);
    EXPECT_EQ(loadedModelComponent._customMaterialPath, "Content/Materials/TestMaterial.yamat");
    EXPECT_FALSE(loadedModelComponent._useEmbeddedMaterials);
    EXPECT_FALSE(loadedModelComponent._autoCreateChildEntities);
}

TEST(ModelComponentTest, SharedEmbeddedMaterialCachePolicyMatchesMaterialType)
{
    ModelComponent modelComponent;

    modelComponent._useEmbeddedMaterials = true;
    modelComponent._materialType         = EModelMaterialType::Phong;
    EXPECT_TRUE(modelComponent.canUseSharedEmbeddedMaterialCache());

    modelComponent._materialType = EModelMaterialType::PBR;
    EXPECT_TRUE(modelComponent.canUseSharedEmbeddedMaterialCache());

    modelComponent._materialType = EModelMaterialType::Unlit;
    EXPECT_FALSE(modelComponent.canUseSharedEmbeddedMaterialCache());

    modelComponent._materialType = EModelMaterialType::Custom;
    EXPECT_FALSE(modelComponent.canUseSharedEmbeddedMaterialCache());

    modelComponent._useEmbeddedMaterials = false;
    modelComponent._materialType         = EModelMaterialType::Phong;
    EXPECT_FALSE(modelComponent.canUseSharedEmbeddedMaterialCache());
}

} // namespace ya