#include "HelloMaterial.h"
#include "Core/AssetManager.h"

#include "Core/UI/UITextBlock.h"
#include "ECS/Component/LuaScriptComponent.h"
#include "ECS/Component/Material/LitMaterialComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/PointLightComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/Component/WidgetComponent.h"
#include "ECS/Entity.h"
#include "ECS/System/LitMaterialSystem.h"
#include "ECS/System/SimpleMaterialSystem.h"

#include "Core/UI/UIManager.h"

#include "Core/Math/Geometry.h"

#include "Render/Material/LitMaterial.h"
#include "Render/Material/MaterialFactory.h"
#include "Render/Mesh.h"
#include "Render/TextureLibrary.h"
#include "Scene/Scene.h"
#include <format>

#include "Scene/SceneManager.h"

#include "Core/System/FileSystem.h"


void HelloMaterial::createCubeMesh()
{
    std::vector<ya::Vertex> vertices;
    std::vector<uint32_t>   indices;
    ya::PrimitiveGeometry::createCube(glm::vec3(1.0f), vertices, indices);
    cubeMesh = makeShared<ya::Mesh>(vertices, indices, "cube");
}

void HelloMaterial::loadResources()
{

    ya::FontManager::get()->loadFont("Engine/Content/Fonts/JetBrainsMono-Medium.ttf", "JetBrainsMono-Medium", 18);
    auto tex = ya::AssetManager::get()->loadTexture("light", "Engine/Content/TestTextures/icons8-light-64.png");
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


    _pongMaterialNames.clear();
    std::string jsonContent;
    if (FileSystem::get()->readFileToString("Example/HelloMaterial/Content/PhongSamples.json", jsonContent)) {
        nlohmann::json j = nlohmann::json::parse(jsonContent);
        for (auto it : j["materials"]) {
            auto name = it["name"].get<std::string>();
            _pongMaterialNames.push_back(name);
            auto *mat       = ya::MaterialFactory::get()->createMaterial<ya::LitMaterial>(name);
            auto  ambient   = it["ambient"].get<std::vector<float>>();
            auto  diff      = it["diffuse"].get<std::vector<float>>();
            auto  specular  = it["specular"].get<std::vector<float>>();
            auto  shininess = it["shininess"].get<float>();
            mat->setPhongParam(
                glm::vec3(ambient[0], ambient[1], ambient[2]),
                glm::vec3(diff[0], diff[1], diff[2]),
                glm::vec3(specular[0], specular[1], specular[2]),
                shininess);
            YA_CORE_INFO("Created Phong material: {}", name);
        }
    }

    auto *litMaterial1 = ya::MaterialFactory::get()->createMaterial<ya::LitMaterial>("lit0");
    auto *litMaterial2 = ya::MaterialFactory::get()->createMaterial<ya::LitMaterial>("lit1_WorldBasic");

    auto pointLightMat = ya::MaterialFactory::get()->createMaterial<ya::UnlitMaterial>("unlit_point-light");
    pointLightMat->setTextureView(ya::UnlitMaterial::BaseColor0,
                                  ya::TextureView{
                                      .texture = ya::TextureLibrary::getWhiteTexture(),
                                      .sampler = ya::TextureLibrary::getDefaultSampler(),
                                  });
    pointLightMat->setTextureView(ya::UnlitMaterial::BaseColor1,
                                  ya::TextureView{
                                      .texture = ya::AssetManager::get()->getTextureByName("light"),
                                      .sampler = ya::TextureLibrary::getDefaultSampler(),
                                  });
    pointLightMat->setTextureViewEnable(ya::UnlitMaterial::BaseColor0, true);
    pointLightMat->setTextureViewUVRotation(ya::UnlitMaterial::BaseColor1, glm::radians(90.f));
    pointLightMat->setMixValue(0.8f);
}

void HelloMaterial::createEntities(ya::Scene *scene)
{

    // Set up default camera

    auto cam = scene->createEntity("Camera");
    cam->addComponent<ya::TransformComponent>();
    cam->addComponent<ya::CameraComponent>();
    cam->addComponent<ya::SimpleMaterialComponent>();
    _viewportRT->setCamera(cam);

    YA_CORE_ASSERT(scene->getRegistry().all_of<ya::CameraComponent>(cam->getHandle()), "Camera component not found");
    YA_CORE_ASSERT(cam->hasComponent<ya::CameraComponent>(), "Camera component not attached");

    auto cc    = cam->getComponent<ya::CameraComponent>();
    auto owner = cc->getOwner();
    YA_CORE_ASSERT(owner == cam, "Camera component owner mismatch");

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
        ya::AssetManager::get()->loadTexture("container_specular", "Engine/Content/TestTextures/LearnOpenGL/container2_specular.png");
        litMat->setTextureView(ya::LitMaterial::EResource::DiffuseTexture,
                               ya::TextureView{
                                   .texture = ya::AssetManager::get()->getTextureByName("container_diffuse"),
                                   .sampler = ya::TextureLibrary::getDefaultSampler(),
                               });
        litMat->setTextureView(ya::LitMaterial::EResource::SpecularTexture,
                               ya::TextureView{
                                   .texture = ya::AssetManager::get()->getTextureByName("container_specular"),
                                   .sampler = ya::TextureLibrary::getDefaultSampler(),
                               });

        // 添加 Lua 旋转脚本（新 API）
        auto lsc = LitTestCube0->addComponent<ya::LuaScriptComponent>();
        // 可以添加多个脚本，类似 Unity
        // lsc->addScript("Content/Scripts/Health.lua");
        // lsc->addScript("Content/Scripts/Inventory.lua");
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
            lmc->addMesh(mesh.get(), litMat); // mesh is now stdptr<Mesh>, not MeshData
        }
    }

    if (auto *pointLt = scene->createEntity("Point Light")) {
        auto tc = pointLt->addComponent<ya::TransformComponent>();
        tc->setPosition(glm::vec3(0.0, 5.f, 0.f));
        _pointLightEntity = pointLt;



        auto plc = pointLt->addComponent<ya::PointLightComponent>();
        auto lmc = pointLt->addComponent<ya::UnlitMaterialComponent>();

        auto pointLightMat = ya::MaterialFactory::get()->getMaterialByName("unlit_point-light")->as<ya::UnlitMaterial>();

        lmc->addMesh(cubeMesh.get(), pointLightMat);

        // 添加 Lua 圆周运动脚本（新 API）
        auto lsc = pointLt->addComponent<ya::LuaScriptComponent>();
        lsc->addScript("Engine/Content/Lua/TestPointLight.lua");
    }


    // create
    glm::vec3 startPos(-10.0f, -10.0f, -10.0f);
    float     xDir    = 1.0f;
    float     zDir    = 1.0f;
    float     spacing = 3.0f;
    for (size_t i = 0; i < _pongMaterialNames.size(); ++i) {
        auto *entity = scene->createEntity(std::format("PhongSample_{}_{}", i, _pongMaterialNames[i]));
        auto  tc     = entity->addComponent<ya::TransformComponent>();
        float x      = startPos.x + (i % 5) * spacing;
        float z      = startPos.z + (i / 5) * spacing;
        tc->setPosition(glm::vec3(x, 0.0f, z));

        auto lmc = entity->addComponent<ya::LitMaterialComponent>();
        auto mat = ya::MaterialFactory::get()->getMaterialByName(_pongMaterialNames[i])->as<ya::LitMaterial>();
        lmc->addMesh(cubeMesh.get(), mat);

        // TODO: implement the 3D UI system to show material name
        // auto wc          = entity->addComponent<ya::WidgetComponent>();
        // auto textBlock   = ya::UIFactory::create<ya::UITextBlock>();
        // textBlock->_font = ya::FontManager::get()->getFont("JetBrainsMono-Medium", 18).get();
        // textBlock->setText(_pongMaterialNames[i]);
        // wc->widget = textBlock;
    }
}
void HelloMaterial::onUpdate(float dt)
{
    Super::onUpdate(dt);

    // Lua 脚本已经处理旋转，不需要手动更新
}

void HelloMaterial::onRenderGUI(float dt)
{
    Super::onRenderGUI(dt);
}
