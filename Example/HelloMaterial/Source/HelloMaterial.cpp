#include "HelloMaterial.h"
#include "Core/AssetManager.h"

#include "ECS/Component/Material/LitMaterialComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/Entity.h"
#include "ECS/System/SimpleMaterialSystem.h"


#include "Math/Geometry.h"

#include "Render/Material/LitMaterial.h"
#include "Render/Material/MaterialFactory.h"
#include "Render/Mesh.h"
#include "Render/TextureLibrary.h"
#include "Scene/Scene.h"
#include <format>

#include "glm/gtc/type_ptr.hpp"
#include "imgui.h"



void HelloMaterial::createCubeMesh()
{
    std::vector<ya::Vertex> vertices;
    std::vector<uint32_t>   indices;
    ya::GeometryUtils::makeCube(-0.5, 0.5, -0.5, 0.5, -0.5, 0.5, vertices, indices, true, true);
    cubeMesh = std::make_shared<ya::Mesh>(vertices, indices, "cube");
}

void HelloMaterial::loadTextures()
{

    const char *faceTexturePath = "Engine/Content/TestTextures/face.png";
    const char *uv1TexturePath  = "Engine/Content/TestTextures/uv1.png";

    ya::AssetManager::get()->loadTexture("face", faceTexturePath);
    ya::AssetManager::get()->loadTexture("uv1", uv1TexturePath);
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
    litMaterial1->setObjectColor(glm::vec3(1.0, 0.0, 0.0));
    auto *litMaterial2 = ya::MaterialFactory::get()->createMaterial<ya::LitMaterial>("lit1");
    litMaterial2->setObjectColor(glm::vec3(0.0, 1.0, 0.0));
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

        auto umc = plane->addComponent<ya::UnlitMaterialComponent>();
        umc->addMesh(cubeMesh.get(), unlitMaterials.back().get()->as<ya::UnlitMaterial>());
    }

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
            }
        }
    }

    if (auto *LitTest = scene->createEntity("Lit Test")) {
        auto tc = LitTest->addComponent<ya::TransformComponent>();
        tc->setPosition(glm::vec3(-10.f, 0.f, 0.f));
        tc->setScale(glm::vec3(3.0f));
        _litTestEntity = LitTest;

        auto lmc = LitTest->addComponent<ya::LitMaterialComponent>();
        // TODO: cast check
        auto litMat = ya::MaterialFactory::get()->getMaterialByName("lit0")->as<ya::LitMaterial>();
        lmc->addMesh(cubeMesh.get(), litMat);
    }
    if (auto *PointLight = scene->createEntity("Point Light")) {
        auto tc = PointLight->addComponent<ya::TransformComponent>();
        tc->setPosition(glm::vec3(-10.f, 4.f, 3.f));
        _lightEntity = PointLight;

        auto lmc = PointLight->addComponent<ya::LitMaterialComponent>();
        // TODO: cast check
        auto litMat = ya::MaterialFactory::get()->getMaterialByName("lit1")->as<ya::LitMaterial>();
        lmc->addMesh(cubeMesh.get(), litMat);
        litMat->setObjectColor(glm::vec3(1.0f));
    }
}

void HelloMaterial::onRenderGUI()
{
    Super::onRenderGUI();
    if (!ImGui::CollapsingHeader("HelloMaterial")) {
        return;
    }
    ImGui::Indent();
    // TODO: use my gui not imgui
    // imgui manipulate the lit material  "Lit Test" and "Point Light"
    if (_litTestEntity && _litTestEntity->hasComponent<ya::LitMaterialComponent>()) {
        auto lmc = _litTestEntity->getComponent<ya::LitMaterialComponent>();
        for (const auto &[material, meshIDs] : lmc->getMaterial2MeshIDs()) {
            auto litMat = material->as<ya::LitMaterial>();
            ImGui::ColorEdit3("Lit Test Object Color", glm::value_ptr(litMat->uParams.objectColor));
        }

        // transfrom manipulation
        auto      tc  = _litTestEntity->getComponent<ya::TransformComponent>();
        glm::vec3 pos = tc->getPosition();
        if (ImGui::DragFloat3("Lit Test Position", glm::value_ptr(pos), 0.1f)) {
            tc->setPosition(pos);
        }
    }
    ImGui::Spacing();
    if (_lightEntity && _lightEntity->hasComponent<ya::LitMaterialComponent>()) {
        auto lmc = _lightEntity->getComponent<ya::LitMaterialComponent>();
        for (const auto &[material, meshIDs] : lmc->getMaterial2MeshIDs()) {
            auto litMat = material->as<ya::LitMaterial>();
            ImGui::ColorEdit3("Point Light Object Color", glm::value_ptr(litMat->uParams.objectColor));
        }
        // transfrom manipulation
        auto      tc  = _lightEntity->getComponent<ya::TransformComponent>();
        glm::vec3 pos = tc->getPosition();
        if (ImGui::DragFloat3("Point Light Position", glm::value_ptr(pos), 0.1f)) {
            tc->setPosition(pos);
        }
    }
    ImGui::Unindent();
}
