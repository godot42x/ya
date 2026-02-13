#include "HelloMaterial.h"
#include "ECS/Component/MirrorComponent.h"
#include "Resource/AssetManager.h"
#include "Resource/FontManager.h"

#include "ECS/Component/3D/SkyboxComponent.h"
#include "ECS/Component/LuaScriptComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/ModelComponent.h"
#include "ECS/Component/PlayerComponent.h"
#include "ECS/Component/PointLightComponent.h"
#include "ECS/Component/TransformComponent.h"


#include "ECS/Entity.h"
#include "ECS/System/PhongMaterialSystem.h"


#include "Core/Math/Geometry.h"

#include "Render/Material/MaterialFactory.h"
#include "Render/Material/PhongMaterial.h"
#include "Resource/TextureLibrary.h"
#include "Scene/Scene.h"
#include <format>


#include "Scene/SceneManager.h"

#include "Core/System/VirtualFileSystem.h"


void HelloMaterial::createCubeMesh()
{
    // No longer needed - use PrimitiveMeshCache instead
    // cubeMesh is now managed by PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Cube)
}

void HelloMaterial::loadResources()
{

    ya::FontManager::get()->loadFont("Engine/Content/Fonts/JetBrainsMono-Medium.ttf", "JetBrainsMono-Medium", 18);
    auto tex = ya::AssetManager::get()->loadTexture("light", "Engine/Content/TestTextures/icons8-light-64.png");
}

void HelloMaterial::createMaterials()
{
    // Create base materials
    auto* baseMaterial0      = ya::MaterialFactory::get()->createMaterial<ya::SimpleMaterial>("base0");
    auto* baseMaterial1      = ya::MaterialFactory::get()->createMaterial<ya::SimpleMaterial>("base1");
    baseMaterial0->colorType = ya::SimpleMaterial::EColor::Normal;
    baseMaterial1->colorType = ya::SimpleMaterial::EColor::Texcoord;

    // Create unlit materials
    auto* unlitMaterial0 = ya::MaterialFactory::get()->createMaterial<ya::UnlitMaterial>("unlit0");
    unlitMaterial0->setTextureView(ya::UnlitMaterial::BaseColor0,
                                   ya::TextureView{
                                       .texture = ya::TextureLibrary::get().getWhiteTexture(),
                                       .sampler = ya::TextureLibrary::get().getDefaultSampler(),
                                   });
    unlitMaterial0->setTextureView(ya::UnlitMaterial::BaseColor1,
                                   ya::TextureView{
                                       .texture = ya::TextureLibrary::get().getMultiPixelTexture(),
                                       .sampler = ya::TextureLibrary::get().getDefaultSampler(),
                                   });
    unlitMaterial0->setTextureViewEnable(ya::UnlitMaterial::BaseColor0, true);
    unlitMaterial0->setTextureViewEnable(ya::UnlitMaterial::BaseColor1, true);
    unlitMaterial0->setMixValue(0.5);

    auto* unlitMaterial1 = ya::MaterialFactory::get()->createMaterial<ya::UnlitMaterial>("unlit1");
    unlitMaterial1->setTextureView(ya::UnlitMaterial::BaseColor0,
                                   ya::TextureView{
                                       .texture = ya::TextureLibrary::get().getBlackTexture(),
                                       .sampler = ya::TextureLibrary::get().getDefaultSampler(),
                                   });
    unlitMaterial1->setTextureViewEnable(ya::UnlitMaterial::BaseColor0, true);
    unlitMaterial1->setTextureView(ya::UnlitMaterial::BaseColor1,
                                   ya::TextureView{
                                       .texture = ya::AssetManager::get()->getTextureByName("face"),
                                       .sampler = ya::TextureLibrary::get().getDefaultSampler(),
                                   });
    unlitMaterial1->setTextureViewEnable(ya::UnlitMaterial::BaseColor1, true);
    unlitMaterial1->setMixValue(0.5);

    auto* unlitMaterial2 = ya::MaterialFactory::get()->createMaterial<ya::UnlitMaterial>("unlit2");
    unlitMaterial2->setTextureView(ya::UnlitMaterial::BaseColor0,
                                   ya::TextureView{
                                       .texture = ya::AssetManager::get()->getTextureByName("uv1"),
                                       .sampler = ya::TextureLibrary::get().getDefaultSampler(),
                                   });
    unlitMaterial2->setTextureView(ya::UnlitMaterial::BaseColor1,
                                   ya::TextureView{
                                       .texture = ya::TextureLibrary::get().getWhiteTexture(),
                                       .sampler = ya::TextureLibrary::get().getDefaultSampler(),
                                   });
    unlitMaterial2->setTextureViewEnable(ya::UnlitMaterial::BaseColor0, true);
    unlitMaterial2->setTextureViewEnable(ya::UnlitMaterial::BaseColor1, true);
    unlitMaterial2->setMixValue(0.5);


    // Create ground plane material
    auto* unlitMaterial3 = ya::MaterialFactory::get()->createMaterial<ya::UnlitMaterial>("unlit3");
    unlitMaterial3->setTextureView(ya::UnlitMaterial::BaseColor0,
                                   ya::TextureView{
                                       .texture = ya::TextureLibrary::get().getWhiteTexture(),
                                       .sampler = ya::TextureLibrary::get().getDefaultSampler(),
                                   });
    unlitMaterial3->setTextureView(ya::UnlitMaterial::BaseColor1,
                                   ya::TextureView{
                                       .texture = ya::AssetManager::get()->getTextureByName("uv1"),
                                       .sampler = ya::TextureLibrary::get().getDefaultSampler(),
                                   });
    unlitMaterial3->setTextureViewEnable(ya::UnlitMaterial::BaseColor0, true);
    unlitMaterial3->setTextureViewEnable(ya::UnlitMaterial::BaseColor1, true);
    unlitMaterial3->setMixValue(0.5);
    unlitMaterial3->setTextureViewUVScale(ya::UnlitMaterial::BaseColor1, glm::vec2(100.f, 100.f));


    _pongMaterialNames.clear();
    std::string jsonContent;
    if (VirtualFileSystem::get()->readFileToString("Example/HelloMaterial/Content/PhongSamples.json", jsonContent)) {
        nlohmann::json j = nlohmann::json::parse(jsonContent);
        for (auto it : j["materials"]) {
            auto name = it["name"].get<std::string>();
            _pongMaterialNames.push_back(name);
            auto* mat       = ya::MaterialFactory::get()->createMaterial<ya::PhongMaterial>(name);
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

    auto* litMaterial1 = ya::MaterialFactory::get()->createMaterial<ya::PhongMaterial>("lit0");
    auto* litMaterial2 = ya::MaterialFactory::get()->createMaterial<ya::PhongMaterial>("lit1_WorldBasic");

    auto pointLightMat = ya::MaterialFactory::get()->createMaterial<ya::UnlitMaterial>("unlit_point-light");
    pointLightMat->setTextureView(ya::UnlitMaterial::BaseColor0,
                                  ya::TextureView{
                                      .texture = ya::TextureLibrary::get().getWhiteTexture(),
                                      .sampler = ya::TextureLibrary::get().getDefaultSampler(),
                                  });
    pointLightMat->setTextureView(ya::UnlitMaterial::BaseColor1,
                                  ya::TextureView{
                                      .texture = ya::AssetManager::get()->getTextureByName("light"),
                                      .sampler = ya::TextureLibrary::get().getDefaultSampler(),
                                  });
    pointLightMat->setTextureViewEnable(ya::UnlitMaterial::BaseColor0, true);
    pointLightMat->setTextureViewUVRotation(ya::UnlitMaterial::BaseColor1, glm::radians(90.f));
    pointLightMat->setMixValue(0.8f);
}

void HelloMaterial::createEntities(ya::Scene* scene)
{



    if (auto skyBox = scene->createNode3D("Skybox")) {
        ya::Entity* entity = skyBox->getEntity();

        // Mesh component
        auto mc = entity->addComponent<ya::MeshComponent>();
        mc->setPrimitiveGeometry(ya::EPrimitiveGeometry::Cube);

        // Material component
        // auto lmc = entity->addComponent<ya::PhongMaterialComponent>();
        // lmc->createDefaultMaterial();
        // lmc->setTextureSlot(ya::PhongMaterial::DiffuseTexture, "Engine/Content/Textures/Skybox/skybox.png");
        if (auto* sc = entity->addComponent<ya::SkyboxComponent>()) {
            ya::CubeMapCreateInfo ci{
                .label = "SkyboxCubemap",
                .files = {},
            };
            ci.files[ya::CubeFace_PosX] = "Engine/ThirdParty/LearnOpenGL/resources/textures/skybox/right.jpg";
            ci.files[ya::CubeFace_NegX] = "Engine/ThirdParty/LearnOpenGL/resources/textures/skybox/left.jpg";
            ci.files[ya::CubeFace_PosY] = "Engine/ThirdParty/LearnOpenGL/resources/textures/skybox/top.jpg";
            ci.files[ya::CubeFace_NegY] = "Engine/ThirdParty/LearnOpenGL/resources/textures/skybox/bottom.jpg";
            ci.files[ya::CubeFace_PosZ] = "Engine/ThirdParty/LearnOpenGL/resources/textures/skybox/front.jpg";
            ci.files[ya::CubeFace_NegZ] = "Engine/ThirdParty/LearnOpenGL/resources/textures/skybox/back.jpg";

            auto cubeMap = ya::Texture::createCubeMap(ci);

            sc->cubemapTexture = cubeMap;
        }
    }


    auto simpleMaterials = ya::MaterialFactory::get()->getMaterials<ya::SimpleMaterial>();
    auto unlitMaterials  = ya::MaterialFactory::get()->getMaterials<ya::UnlitMaterial>();

    // Create ground plane - using new reflection-based approach
    if (auto plane = scene->createNode3D("Plane")) {
        ya::Entity* entity = plane->getEntity();
        auto        tc     = entity->getComponent<ya::TransformComponent>();
        tc->setScale(glm::vec3(1000.f, 10.f, 1000.f));
        tc->setPosition(glm::vec3(0.f, -30.f, 0.f));

        // Mesh and Material are now separate components
        auto mc = entity->addComponent<ya::MeshComponent>();
        mc->setPrimitiveGeometry(ya::EPrimitiveGeometry::Cube);

        auto lmc = entity->addComponent<ya::PhongMaterialComponent>();
        lmc->createDefaultMaterial();
        lmc->getMaterial()->getParamsMut().diffuse = glm::vec3(0.8f, 0.8f, 0.8f);
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
                auto        cube   = scene->createNode(std::format("Cube_{}_{}_{}", i, j, k));
                ya::Entity* entity = cube->getEntity();
                {
                    auto v  = glm::vec3(i, j, k);
                    auto tc = entity->getComponent<ya::TransformComponent>();
                    tc->setPosition(offset * v);
                    float scale = std::sin(glm::radians(15.f * (float)(i + j + k)));
                    tc->setScale(glm::vec3(scale));

                    // Add mesh component (shared primitive)
                    auto mc = entity->addComponent<ya::MeshComponent>();
                    mc->setPrimitiveGeometry(ya::EPrimitiveGeometry::Cube);

                    // random material
                    uint32_t materialIndex = index % maxMaterialIndex;
                    ++index;
                    if (materialIndex < simpleMaterialCount) {
                        // use base material
                        auto bmc = entity->addComponent<ya::SimpleMaterialComponent>();
                        auto mat = simpleMaterials[materialIndex];
                        YA_CORE_ASSERT(mat, "Material is null");
                        bmc->setMaterial(mat->as<ya::SimpleMaterial>());
                    }
                    else {
                        // use unlit material
                        auto umc = entity->addComponent<ya::UnlitMaterialComponent>();
                        auto mat = unlitMaterials[materialIndex % unlitMaterials.size()];
                        YA_CORE_ASSERT(mat, "Material is null");
                        umc->setMaterial(mat->as<ya::UnlitMaterial>());
                    }
                }
                // YA_CORE_DEBUG("1.2 {} {} {}", i, j, k);
            }
        }
    }
#endif

    if (auto* LitTestCube0 = scene->createNode3D("Lit Test")) {
        ya::Entity* entity = LitTestCube0->getEntity();
        auto        tc     = entity->getComponent<ya::TransformComponent>();
        tc->setPosition(glm::vec3(0.0f, 0.f, -5.0f));
        tc->setScale(glm::vec3(3.0f));
        _litTestEntity = entity;

        // Mesh component (separate from material)
        auto mc = entity->addComponent<ya::MeshComponent>();
        mc->setPrimitiveGeometry(ya::EPrimitiveGeometry::Cube);

        // Material component with serializable texture slots
        auto lmc = entity->addComponent<ya::PhongMaterialComponent>();
        lmc->createDefaultMaterial();
        lmc->setTextureSlot(ya::PhongMaterial::DiffuseTexture, "Engine/Content/TestTextures/LearnOpenGL/container2.png");
        lmc->setTextureSlot(ya::PhongMaterial::SpecularTexture, "Engine/Content/TestTextures/LearnOpenGL/container2_specular.png");
        lmc->getMaterial()->getParamsMut() = ya::PhongMaterial::ParamUBO{
            .ambient   = glm::vec3(0.1f),
            .diffuse   = glm::vec3(1.0f),
            .specular  = glm::vec3(1.0f),
            .shininess = 32.0f,
        };

        // 添加 Lua 旋转脚本（新 API）
        auto lsc = entity->addComponent<ya::LuaScriptComponent>();
        // 可以添加多个脚本，类似Unity
        // lsc->addScript("Content/Scripts/Health.lua");
        // lsc->addScript("Content/Scripts/Inventory.lua");
    }
    if (auto* LitTestCube1 = scene->createNode3D("Lit Test 1")) {
        ya::Entity* entity = LitTestCube1->getEntity();
        auto        tc     = entity->getComponent<ya::TransformComponent>();
        tc->setPosition(glm::vec3(-5.0f, 0.f, -5.0f));
        tc->setScale(glm::vec3(3.0f));
        _litTestEntity = entity;

        // Mesh component (separate from material)
        auto mc = entity->addComponent<ya::MeshComponent>();
        mc->setPrimitiveGeometry(ya::EPrimitiveGeometry::Quad);
        tc->setRotation(glm::vec3(-0.0f, 0.f, 0.0f));

        // Material component with serializable texture slots
        auto lmc = entity->addComponent<ya::PhongMaterialComponent>();
        lmc->createDefaultMaterial();
        lmc->getMaterial()->getParamsMut() = ya::PhongMaterial::ParamUBO{
            .ambient   = glm::vec3(1.0f),
            .diffuse   = glm::vec3(1.0f),
            .specular  = glm::vec3(1.0f),
            .shininess = 32.0f,
        };

        entity->addComponent<ya::MirrorComponent>();
    }
    if (auto* suzanne = scene->createNode3D("Suzanne")) {
        ya::Entity* entity = suzanne->getEntity();
        auto        tc     = entity->getComponent<ya::TransformComponent>();
        tc->setPosition(glm::vec3(5.0f, 0.f, 0.f));
        tc->setScale(glm::vec3(2.0f));

        // Mesh component with external model
        // auto mc = entity->addComponent<ya::MeshComponent>();
        auto mc = entity->addComponent<ya::ModelComponent>();
        mc->setModelPath("Engine/Content/Misc/Monkey.obj");

        // Material component
        auto lmc = entity->addComponent<ya::PhongMaterialComponent>();
        lmc->createDefaultMaterial();
        lmc->getMaterial()->getParamsMut() = ya::PhongMaterial::ParamUBO{
            .ambient   = glm::vec3(0.1f),
            .diffuse   = glm::vec3(0.6f, 0.4f, 0.2f), // Brownish color
            .specular  = glm::vec3(0.5f),
            .shininess = 16.0f,
        };
    }

    if (auto* backpack = scene->createNode3D("Backpack")) {
        ya::Entity* entity = backpack->getEntity();
        auto        tc     = entity->getComponent<ya::TransformComponent>();
        tc->setPosition(glm::vec3(-5.0f, 0.f, 0.f));

        // Mesh component with external model
        auto mc = entity->addComponent<ya::ModelComponent>();
        mc->setModelPath("Engine/Content/Assets/backpack/backpack.obj");

        // Material component
        // auto lmc     = entity->addComponent<ya::PhongMaterialComponent>();
        // lmc->_params = ya::PhongMaterial::ParamUBO{
        //     .ambient   = glm::vec3(0.1f),
        //     .diffuse   = glm::vec3(0.6f, 0.4f, 0.2f), // Brownish color
        //     .specular  = glm::vec3(0.5f),
        //     .shininess = 16.0f,
        // };
    }
    if (auto* nanoSuit = scene->createNode3D("NanoSuit"))
    {
        ya::Entity* entity = nanoSuit->getEntity();
        auto        tc     = entity->getComponent<ya::TransformComponent>();
        tc->setPosition(glm::vec3(-10.0f, 0.f, 5.f));

        // Mesh component with external model
        auto mc = entity->addComponent<ya::ModelComponent>();
        mc->setModelPath("Engine/ThirdParty/LearnOpenGL/resources/objects/nanosuit/nanosuit.obj");

        // Material component
        // auto lmc = entity->addComponent<ya::PhongMaterialComponent>();
        // lmc->createDefaultMaterial();
        // lmc->getMaterial()->getParamsMut() = ya::PhongMaterial::ParamUBO{
        //     .ambient   = glm::vec3(0.1f),
        //     .diffuse   = glm::vec3(0.6f, 0.4f, 0.2f), // Brownish color
        //     .specular  = glm::vec3(0.5f),
        //     .shininess = 16.0f,
        // };
    }

    if (auto* window = scene->createNode3D("Window"))
    {
        ya::Entity* entity = window->getEntity();
        auto        tc     = entity->getComponent<ya::TransformComponent>();
        tc->setPosition(glm::vec3(5.f, 0.f, 3.f));
        tc->setScale(glm::vec3(1.0f, 1.0f, 0.1f));
        tc->setRotation(glm::vec3(180.f, 0, 0));

        // Mesh component
        auto mc = entity->addComponent<ya::MeshComponent>();
        mc->setPrimitiveGeometry(ya::EPrimitiveGeometry::Quad);

        // Material component
        auto lmc = entity->addComponent<ya::PhongMaterialComponent>();
        lmc->createDefaultMaterial();
        lmc->setTextureSlot(ya::PhongMaterial::DiffuseTexture, "Engine/ThirdParty/LearnOpenGL/resources/textures/window.png");
    }

    if (auto* pointLt = scene->createNode3D("Point Light")) {
        ya::Entity* entity = pointLt->getEntity();
        auto        tc     = entity->getComponent<ya::TransformComponent>();
        tc->setPosition(glm::vec3(0.0, 5.f, 0.f));
        _pointLightEntity = entity;

        // Mesh component
        auto mc = entity->addComponent<ya::MeshComponent>();
        mc->setPrimitiveGeometry(ya::EPrimitiveGeometry::Cube);

        // Material component
        auto plc = entity->addComponent<ya::PointLightComponent>();
        auto umc = entity->addComponent<ya::UnlitMaterialComponent>();

        auto pointLightMat = ya::MaterialFactory::get()->getMaterialByName("unlit_point-light")->as<ya::UnlitMaterial>();
        umc->setMaterial(pointLightMat);
        // umc.add

        // 添加 Lua 圆周运动脚本（新 API）
        auto lsc = entity->addComponent<ya::LuaScriptComponent>();
        lsc->addScript("Engine/Content/Lua/TestPointLight.lua");
    }

    // Create Phong sample cubes using new reflection-based approach
    glm::vec3 startPos(-10.0f, -20.0f, -20.0f);
    float     spacing = 3.0f;
    for (size_t i = 0; i < _pongMaterialNames.size(); ++i) {
        auto*       node   = scene->createNode3D(std::format("PhongSample_{}_{}", i, _pongMaterialNames[i]));
        ya::Entity* entity = node->getEntity();
        auto        tc     = entity->getComponent<ya::TransformComponent>();
        float       x      = startPos.x + (i % 5) * spacing;
        float       z      = startPos.z + (i / 5) * spacing;
        tc->setPosition(glm::vec3(x, 0.0f, z));

        // Mesh component
        auto mc = entity->addComponent<ya::MeshComponent>();
        mc->setPrimitiveGeometry(ya::EPrimitiveGeometry::Cube);

        // Get pre-created material params from factory (loaded from JSON)
        auto existingMat = ya::MaterialFactory::get()->getMaterialByName(_pongMaterialNames[i])->as<ya::PhongMaterial>();

        // Material component
        auto lmc = entity->addComponent<ya::PhongMaterialComponent>();
        lmc->setMaterial(existingMat);

        // TODO: implement the 3D UI system to show material name
        // auto wc          = entity->addComponent<ya::WidgetComponent>();
        // auto textBlock   = ya::UIFactory::create<ya::UITextBlock>();
        // textBlock->_font = ya::FontManager::get()->getFont("JetBrainsMono-Medium", 18).get();
        // textBlock->setText(_pongMaterialNames[i]);
        // wc->widget = textBlock;
    }
}

void HelloMaterial::onRenderGUI(float dt)
{
    Super::onRenderGUI(dt);
}

void HelloMaterial::onEnterRuntime()
{
    Super::onEnterRuntime();
    // Set up default camera
    auto scene = ya::App::get()->getSceneManager()->getActiveScene();
    YA_CORE_ASSERT(scene, "Scene is null");

    if (auto player = scene->createNode3D("Player"))
    {
        ya::Entity* entity = player->getEntity();
        entity->addComponent<ya::PlayerComponent>();
        entity->addComponent<ya::CameraComponent>();
        entity->addComponent<ya::SimpleMaterialComponent>();
        entity->addComponent<ya::LuaScriptComponent>();
        if (auto spot = entity->addComponent<ya::PointLightComponent>()) {
            spot->_type           = ya::PointLightComponent::Spot;
            spot->_innerConeAngle = 10.0f;
            spot->_outerConeAngle = 30.0f;
        }

        YA_CORE_ASSERT(scene->getRegistry().all_of<ya::CameraComponent>(entity->getHandle()), "Camera component not found");
        YA_CORE_ASSERT(entity->hasComponent<ya::CameraComponent>(), "Camera component not attached");

        auto cc    = entity->getComponent<ya::CameraComponent>();
        auto owner = cc->getOwner();
        YA_CORE_ASSERT(owner == entity, "Camera component owner mismatch");
    }
}
