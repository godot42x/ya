#include "HelloMaterial.h"
#include "Core/AssetManager.h"

#include "ECS/Component/LuaScriptComponent.h"
#include "ECS/Component/Material/LitMaterialComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/PointLightComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/Entity.h"
#include "ECS/System/LitMaterialSystem.h"
#include "ECS/System/SimpleMaterialSystem.h"


#include "Math/Geometry.h"

#include "Render/Material/LitMaterial.h"
#include "Render/Material/MaterialFactory.h"
#include "Render/Mesh.h"
#include "Render/TextureLibrary.h"
#include "Scene/Scene.h"
#include <format>

#include "Scene/SceneManager.h"



void HelloMaterial::createCubeMesh()
{
    std::vector<ya::Vertex> vertices;
    std::vector<uint32_t>   indices;
    ya::PrimitiveGeometry::createCube(glm::vec3(1.0f), vertices, indices);
    cubeMesh = makeShared<ya::Mesh>(vertices, indices, "cube");
}

void HelloMaterial::loadTextures()
{
}

void HelloMaterial::createMaterials()
{
    // Create base materials
    auto *baseMaterial0      = ya::MaterialFactory::get()->createMaterial<ya::SimpleMaterial>("base0");
    auto *baseMaterial1      = ya::MaterialFactory::get()->createMaterial<ya::SimpleMaterial>("base1");
    baseMaterial0->colorType = ya::SimpleMaterial::EColor::Normal;
    baseMaterial1->colorType = ya::SimpleMaterial::EColor::Texcoord;

    // Create unlit materials
    auto *unlitMaterial0 = ya::MaterialFactory::get()->createMaterial<ya::UnlitMaterial>("unlit0");
    unlitMaterial0->setTextureView(ya::UnlitMaterial::BaseColor0, ya::TextureView{
                                                                      .texture = ya::TextureLibrary::getWhiteTexture(),
                                                                      .sampler = ya::TextureLibrary::getDefaultSampler(),
                                                                  });
    unlitMaterial0->setTextureView(ya::UnlitMaterial::BaseColor1, ya::TextureView{
                                                                      .texture = ya::TextureLibrary::getMultiPixelTexture(),
                                                                      .sampler = ya::TextureLibrary::getDefaultSampler(),
                                                                  });
    unlitMaterial0->setTextureViewEnable(ya::UnlitMaterial::BaseColor0, true);
    unlitMaterial0->setTextureViewEnable(ya::UnlitMaterial::BaseColor1, true);
    unlitMaterial0->setMixValue(0.5);

    auto *unlitMaterial1 = ya::MaterialFactory::get()->createMaterial<ya::UnlitMaterial>("unlit1");
    unlitMaterial1->setTextureView(ya::UnlitMaterial::BaseColor0,
                                   ya::TextureView{
                                       .texture = ya::TextureLibrary::getBlackTexture(),
                                       .sampler = ya::TextureLibrary::getDefaultSampler(),
                                   });
    unlitMaterial1->setTextureViewEnable(ya::UnlitMaterial::BaseColor0, true);
    unlitMaterial1->setTextureView(ya::UnlitMaterial::BaseColor1,
                                   ya::TextureView{
                                       .texture = ya::AssetManager::get()->getTextureByName("face"),
                                       .sampler = ya::TextureLibrary::getDefaultSampler(),
                                   });
    unlitMaterial1->setTextureViewEnable(ya::UnlitMaterial::BaseColor1, true);
    unlitMaterial1->setMixValue(0.5);

    auto *unlitMaterial2 = ya::MaterialFactory::get()->createMaterial<ya::UnlitMaterial>("unlit2");
    unlitMaterial2->setTextureView(ya::UnlitMaterial::BaseColor0,
                                   ya::TextureView{
                                       .texture = ya::AssetManager::get()->getTextureByName("uv1"),
                                       .sampler = ya::TextureLibrary::getDefaultSampler(),
                                   });
    unlitMaterial2->setTextureView(ya::UnlitMaterial::BaseColor1,
                                   ya::TextureView{
                                       .texture = ya::TextureLibrary::getWhiteTexture(),
                                       .sampler = ya::TextureLibrary::getDefaultSampler(),
                                   });
    unlitMaterial2->setTextureViewEnable(ya::UnlitMaterial::BaseColor0, true);
    unlitMaterial2->setTextureViewEnable(ya::UnlitMaterial::BaseColor1, true);
    unlitMaterial2->setMixValue(0.5);


    // Create ground plane material
    auto *unlitMaterial3 = ya::MaterialFactory::get()->createMaterial<ya::UnlitMaterial>("unlit3");
    unlitMaterial3->setTextureView(ya::UnlitMaterial::BaseColor0,
                                   ya::TextureView{
                                       .texture = ya::TextureLibrary::getWhiteTexture(),
                                       .sampler = ya::TextureLibrary::getDefaultSampler(),
                                   });
    unlitMaterial3->setTextureView(ya::UnlitMaterial::BaseColor1,
                                   ya::TextureView{
                                       .texture = ya::AssetManager::get()->getTextureByName("uv1"),
                                       .sampler = ya::TextureLibrary::getDefaultSampler(),
                                   });
    unlitMaterial3->setTextureViewEnable(ya::UnlitMaterial::BaseColor0, true);
    unlitMaterial3->setTextureViewEnable(ya::UnlitMaterial::BaseColor1, true);
    unlitMaterial3->setMixValue(0.5);
    unlitMaterial3->setTextureViewUVScale(ya::UnlitMaterial::BaseColor1, glm::vec2(100.f, 100.f));


    auto *litMaterial1 = ya::MaterialFactory::get()->createMaterial<ya::LitMaterial>("lit0");
    litMaterial1->setObjectColor(glm::vec3(1.0, 1.0, 1.0));
    auto *litMaterial2 = ya::MaterialFactory::get()->createMaterial<ya::LitMaterial>("lit1_WorldBasic");
    litMaterial2->setObjectColor(glm::vec3(1.0, 1.0, 1.0));
}

void HelloMaterial::createEntities(ya::Scene *scene)
{

    auto simpleMaterials = ya::MaterialFactory::get()->getMaterials<ya::SimpleMaterial>();
    auto unlitMaterials  = ya::MaterialFactory::get()->getMaterials<ya::UnlitMaterial>();

    // Create ground plane
    if (auto plane = scene->createEntity("Plane")) {
        auto tc = plane->addComponent<ya::TransformComponent>();
        tc->setScale(glm::vec3(1000.f, 10.f, 1000.f));
        tc->setPosition(glm::vec3(0.f, -20.f, 0.f));

        auto lmc          = plane->addComponent<ya::LitMaterialComponent>();
        auto groundLitMat = ya::MaterialFactory::get()->getMaterialByName("lit1_WorldBasic")->as<ya::LitMaterial>();
        groundLitMat->setObjectColor(glm::vec3(0.8f, 0.8f, 0.8f));
        lmc->addMesh(cubeMesh.get(), groundLitMat);
    }



#if create_cube_matrix_for_unlit_material

    // Create cube grid
    float offset = 3.f;
    int   count  = 100;
    int   alpha  = (int)std::round(std::pow(count, 1.0 / 3.0));
    YA_CORE_DEBUG("Creating {} entities ({}x{}x{})", alpha * alpha * alpha, alpha, alpha, alpha);

    uint32_t index = 0;

    uint32_t maxMaterialIndex    = ya::MaterialFactory::get()->getMaterialCount() - 1;
    uint32_t simpleMaterialCount = simpleMaterials.size();
    for (int i = 0; i < alpha; ++i) {
        for (int j = 0; j < alpha; ++j) {
            for (int k = 0; k < alpha; ++k) {

                // YA_CORE_DEBUG("1.1 {} {} {}", i, j, k);
                auto cube = scene->createEntity(std::format("Cube_{}_{}_{}", i, j, k));
                {
                    auto v  = glm::vec3(i, j, k);
                    auto tc = cube->addComponent<ya::TransformComponent>();
                    tc->setPosition(offset * v);
                    float scale = std::sin(glm::radians(15.f * (float)(i + j + k)));
                    tc->setScale(glm::vec3(scale));

                    // random material
                    uint32_t materialIndex = index % maxMaterialIndex;
                    ++index;
                    if (materialIndex < simpleMaterialCount) {
                        // use base material
                        auto bmc = cube->addComponent<ya::SimpleMaterialComponent>();
                        auto mat = simpleMaterials[materialIndex];
                        YA_CORE_ASSERT(mat, "Material is null");
                        bmc->addMesh(cubeMesh.get(), mat->as<ya::SimpleMaterial>());
                    }
                    else {
                        // use unlit material
                        auto umc = cube->addComponent<ya::UnlitMaterialComponent>();
                        auto mat = unlitMaterials[materialIndex % unlitMaterials.size()];
                        YA_CORE_ASSERT(mat, "Material is null");
                        umc->addMesh(cubeMesh.get(), mat->as<ya::UnlitMaterial>());
                    }
                }
                // YA_CORE_DEBUG("1.2 {} {} {}", i, j, k);
            }
        }
    }
#endif

    if (auto *LitTestCube0 = scene->createEntity("Lit Test")) {
        auto tc = LitTestCube0->addComponent<ya::TransformComponent>();
        tc->setPosition(glm::vec3(0.0f, 0.f, 0.f));
        tc->setScale(glm::vec3(3.0f));
        _litTestEntity = LitTestCube0;

        auto lmc = LitTestCube0->addComponent<ya::LitMaterialComponent>();
        // TODO: cast check
        auto litMat = ya::MaterialFactory::get()->getMaterialByName("lit0")->as<ya::LitMaterial>();
        lmc->addMesh(cubeMesh.get(), litMat);

        ya::AssetManager::get()->loadTexture("container_diffuse", "Engine/Content/TestTextures/LearnOpenGL/container2.png");
        litMat->setTextureView(ya::LitMaterial::EResource::DiffuseTexture,
                               ya::TextureView{
                                   .texture = ya::AssetManager::get()->getTextureByName("container_diffuse"),
                                   .sampler = ya::TextureLibrary::getDefaultSampler(),
                               });

        // 添加 Lua 旋转脚本
        auto lsc        = LitTestCube0->addComponent<ya::LuaScriptComponent>();
        // lsc->scriptPath = "Engine/Content/Lua/RotateScript.lua";
    }

    if (auto *suzanne = scene->createEntity("Suzanne")) {
        auto tc = suzanne->addComponent<ya::TransformComponent>();
        tc->setPosition(glm::vec3(5.0f, 0.f, 0.f));
        tc->setScale(glm::vec3(2.0f));

        auto lmc   = suzanne->addComponent<ya::LitMaterialComponent>();
        auto model = ya::AssetManager::get()->loadModel("suzanne",
                                                        "Engine/Content/Misc/Monkey.obj");
        for (const auto &mesh : model->getMeshes()) {
            auto litMat = ya::MaterialFactory::get()->getMaterialByName("lit1_WorldBasic")->as<ya::LitMaterial>();
            lmc->addMesh(mesh.mesh.get(), litMat);
        }
    }

    if (auto *pointLt = scene->createEntity("Point Light")) {
        auto tc = pointLt->addComponent<ya::TransformComponent>();
        tc->setPosition(glm::vec3(0.0, 5.f, 0.f));
        _pointLightEntity = pointLt;



        auto plc = pointLt->addComponent<ya::PointLightComponent>();
        auto lmc = pointLt->addComponent<ya::UnlitMaterialComponent>();

        auto pointLightMat = ya::MaterialFactory::get()->createMaterial<ya::UnlitMaterial>("unlit_point-light");
        auto tex           = ya::AssetManager::get()->loadTexture("light", "Engine/Content/TestTextures/icons8-light-64.png");
        pointLightMat->setTextureView(ya::UnlitMaterial::BaseColor0,
                                      ya::TextureView{
                                          .texture = ya::TextureLibrary::getWhiteTexture(),
                                          .sampler = ya::TextureLibrary::getDefaultSampler(),
                                      });
        pointLightMat->setTextureView(ya::UnlitMaterial::BaseColor1,
                                      ya::TextureView{
                                          .texture = tex,
                                          .sampler = ya::TextureLibrary::getDefaultSampler(),
                                      });
        pointLightMat->setTextureViewEnable(ya::UnlitMaterial::BaseColor0, true);
        pointLightMat->setTextureViewUVRotation(ya::UnlitMaterial::BaseColor1, glm::radians(90.f));
        pointLightMat->setMixValue(0.8f);

        lmc->addMesh(cubeMesh.get(), pointLightMat);

        // 添加 Lua 圆周运动脚本
        auto lsc        = pointLt->addComponent<ya::LuaScriptComponent>();
        lsc->scriptPath = "Engine/Content/Lua/TestPointLight.lua";
    }
}
void HelloMaterial::onUpdate(float dt)
{
    Super::onUpdate(dt);

    // Lua 脚本已经处理旋转，不需要手动更新
}
