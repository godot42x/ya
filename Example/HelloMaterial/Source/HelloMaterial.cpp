#include "HelloMaterial.h"
#include "Core/Math/Math.h"
#include "ECS/Component/2D/BillboardComponent.h"
#include "ECS/Systems/Components/CameraComponent.h"
#include "ECS/Systems/Components/MirrorComponent.h"
#include "Resource/AssetManager.h"
#include "GUI/Resources/FontManager.h"
#include "GUI/Widgets/Controls/Button.h"
#include "GUI/Widgets/Controls/Panel.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/UITypeRegistry.h"

#include "ECS/Component/3D/SkyboxComponent.h"
#include "ECS/Systems/Components/DirectionalLightComponent.h"
#include "ECS/Systems/Components/LuaScriptComponent.h"
#include "ECS/Component/Material/PBRMaterialComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "Render3D/Material/SimpleMaterial.h"
#include "Render3D/Material/UnlitMaterial.h"
#include "Render3D/Material/PhongMaterial.h"
#include "ECS/Component/Mesh/StaticMeshComponent.h"
#include "ECS/Component/ModelComponent.h"
#include "ECS/Systems/Components/PointLightComponent.h"
#include "Scene3D/TransformComponent.h"



#include "ECS/Entity.h"


#include "Core/Math/Geometry.h"

#include "Render3D/Material/MaterialFactory.h"
#include "Render3D/Material/PBRMaterial.h"
#include "Render3D/Material/PhongMaterial.h"
#include "RHI/Backend/TextureLibrary.h"
#include "Scene/Core/Scene.h"
#include <format>



#include "Scene/Runtime/SceneManager.h"

#include "Core/System/VirtualFileSystem.h"
#include "GameRuntime/App.h"


void HelloMaterialModule::onAttach(ya::App& app)
{
    render = app.getRenderServices().getRender();
    createCubeMesh();
    loadResources();
}

void HelloMaterialModule::onDetach(ya::App&)
{
    cubeMesh.reset();
    render = nullptr;
}

void HelloMaterialModule::onSceneActivated(ya::App& app, ya::Scene* scene)
{
    YA_INFO("HelloMaterial scene initialized.");
    createUIDemo(app, scene);
}

void HelloMaterialModule::createUIDemo(ya::App& app, ya::Scene* scene)
{
    // Game UI demo (WidgetTree): widgets join the presented world's Game UI
    // content layer via the host; the controller unmounts them when the scene
    // deactivates, so re-entering (PIE) never accumulates.
    auto* gameUIHost = app.getGameUIHost();
    if (!scene || !gameUIHost) {
        return;
    }

    auto& registry = ya::UITypeRegistry::instance();
    auto  panel    = registry.createInstance("engine.panel");
    panel->_position = {20.0f, 20.0f};
    panel->_size     = {300.0f, 120.0f};
    static_cast<ya::UIPanel*>(panel.get())->_color = {0.12f, 0.14f, 0.22f, 0.88f};
    gameUIHost->addToWorld(*scene, panel);

    auto title = registry.createInstance("engine.text");
    title->_position = {36.0f, 30.0f};
    title->_size     = {260.0f, 26.0f};
    static_cast<ya::UIText*>(title.get())->_text     = "Game UI (WidgetTree)";
    static_cast<ya::UIText*>(title.get())->_fontSize = 16;
    static_cast<ya::UIText*>(title.get())->_color    = {1.0f, 0.85f, 0.4f, 1.0f};
    gameUIHost->addToWorld(*scene, title);

    auto label = registry.createInstance("engine.text");
    label->_position = {36.0f, 66.0f};
    label->_size     = {260.0f, 20.0f};
    static_cast<ya::UIText*>(label.get())->_text     = "Click the button below";
    static_cast<ya::UIText*>(label.get())->_fontSize = 16;
    gameUIHost->addToWorld(*scene, label);

    auto button = registry.createInstance("engine.button");
    button->_position = {36.0f, 96.0f};
    button->_size     = {140.0f, 30.0f};
    auto* buttonWidget = static_cast<ya::UIButton*>(button.get());
    buttonWidget->_onClick = [label]() {
        auto* text = static_cast<ya::UIText*>(label.get());
        text->_text = (text->_text == "Button clicked!") ? "Click the button below" : "Button clicked!";
    };
    gameUIHost->addToWorld(*scene, button);
}

void HelloMaterialModule::createCubeMesh()
{
    // No longer needed - use PrimitiveMeshCache instead
    // cubeMesh is now managed by PrimitiveMeshCache::get().getMesh(EPrimitiveGeometry::Cube)
}

void HelloMaterialModule::loadResources()
{
    if (render) {
        ya::FontManager::get()->loadFont(*render, "Engine/Content/Fonts/JetBrainsMono-Medium.ttf", "JetBrainsMono-Medium", 18);
    }
    auto tex = ya::AssetManager::get()->loadTexture(ya::AssetManager::TextureLoadRequest{
        .filepath = "Engine/Content/TestTextures/icons8-light-64.png",
        .name     = "light",
    });
}

void HelloMaterialModule::createMaterials()
{
    // Create base materials
    auto* baseMaterial0      = ya::MaterialFactory::get()->createMaterial<ya::SimpleMaterial>("base0");
    auto* baseMaterial1      = ya::MaterialFactory::get()->createMaterial<ya::SimpleMaterial>("base1");
    baseMaterial0->colorType = ya::SimpleMaterial::EColor::Normal;
    baseMaterial1->colorType = ya::SimpleMaterial::EColor::Texcoord;

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

    ya::MaterialFactory::get()->createMaterial<ya::UnlitMaterial>("unlit_point-light_shared");
}

void HelloMaterialModule::createEntities(ya::Scene* scene)
{



    if (auto skyBox = scene->createNode3D("Skybox")) {
        ya::Entity* entity = skyBox->getEntity();

        // Mesh component
        auto mc = entity->addComponent<ya::StaticMeshComponent>();
        mc->setPrimitiveGeometry(ya::EPrimitiveGeometry::Cube);

        // Material component
        // auto lmc = entity->addComponent<ya::PhongMaterialComponent>();
        // lmc->createDefaultMaterial();
        // lmc->setTextureSlot(ya::EPhongMaterialTextureSlot::Diffuse, "Engine/Content/Textures/Skybox/skybox.png");
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
            sc->setCubemapSource(ci);
            sc->cylindricalSource.filepath     = "Engine/ThirdParty/LearnOpenGL/resources/textures/hdr/newport_loft.hdr";
            sc->cylindricalSource.flipVertical = true;
        }
    }


    auto simpleMaterials = ya::MaterialFactory::get()->getMaterials<ya::SimpleMaterial>();
    auto unlitMaterials  = ya::MaterialFactory::get()->getMaterials<ya::UnlitMaterial>();

    // Create ground plane - using new reflection-based approach
    if (auto plane = scene->createNode3D("Plane")) {
        ya::Entity* entity = plane->getEntity();
        auto        tc     = entity->getComponent<ya::TransformComponent>();
        tc->setScale(glm::vec3(100.f, 10.f, 100.f));
        tc->setPosition(glm::vec3(0.f, -10.f, 0.f));

        // Mesh and Material are now separate components
        auto mc = entity->addComponent<ya::StaticMeshComponent>();
        mc->setPrimitiveGeometry(ya::EPrimitiveGeometry::Cube);

        auto lmc                    = entity->addComponent<ya::PhongMaterialComponent>();
        lmc->getParamsMut().diffuse = glm::vec3(0.8f, 0.8f, 0.8f);
        if (auto diffuse = lmc->setTextureSlot(ya::EPhongMaterialTextureSlot::Diffuse,
                                               "Engine/ThirdParty/LearnOpenGL/resources/textures/wood.png")) {

            diffuse->bEnable = true;
            diffuse->uvScale = {tc->getScale().x, tc->getScale().z}; // Match the plane's XZ scale for proper tiling
        }
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
                    auto mc = entity->addComponent<ya::StaticMeshComponent>();
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
                        umc->setSharedMaterial(mat->as<ya::UnlitMaterial>());
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

        // Mesh component (separate from material)
        auto mc = entity->addComponent<ya::StaticMeshComponent>();
        mc->setPrimitiveGeometry(ya::EPrimitiveGeometry::Cube);

        // Material component with serializable texture slots
        auto lmc = entity->addComponent<ya::PhongMaterialComponent>();
        if (auto* diffuseSlot = lmc->setTextureSlot(ya::EPhongMaterialTextureSlot::Diffuse,
                                                    "Engine/Content/TestTextures/LearnOpenGL/container2.png")) {
            diffuseSlot->bEnable = true;
        }
        if (auto* specularSlot = lmc->setTextureSlot(ya::EPhongMaterialTextureSlot::Specular,
                                                     "Engine/Content/TestTextures/LearnOpenGL/container2_specular.png")) {
            specularSlot->bEnable = true;
        }
        lmc->getParamsMut() = ya::PhongMaterialComponent::AuthoringParams{
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

        // Mesh component (separate from material)
        auto mc = entity->addComponent<ya::StaticMeshComponent>();
        mc->setPrimitiveGeometry(ya::EPrimitiveGeometry::Quad);
        tc->setRotation(glm::vec3(-0.0f, 0.f, 0.0f));

        // Material component with serializable texture slots
        auto lmc            = entity->addComponent<ya::PhongMaterialComponent>();
        lmc->getParamsMut() = ya::PhongMaterialComponent::AuthoringParams{
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
        // auto mc = entity->addComponent<ya::StaticMeshComponent>();
        auto mc = entity->addComponent<ya::ModelComponent>();
        mc->setModelPath("Engine/Content/Misc/Monkey.obj");

        // Material component
        auto lmc            = entity->addComponent<ya::PhongMaterialComponent>();
        lmc->getParamsMut() = ya::PhongMaterialComponent::AuthoringParams{
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
        auto mc = entity->addComponent<ya::StaticMeshComponent>();
        mc->setPrimitiveGeometry(ya::EPrimitiveGeometry::Quad);

        // Material component
        auto lmc = entity->addComponent<ya::PhongMaterialComponent>();
        if (auto* diffuseSlot = lmc->setTextureSlot(ya::EPhongMaterialTextureSlot::Diffuse,
                                                    "Engine/ThirdParty/LearnOpenGL/resources/textures/window.png")) {
            diffuseSlot->bEnable = true;
        }
    }
    auto pointLightUnlitMat = ya::MaterialFactory::get()->getMaterialByName("unlit_point-light_shared")->as<ya::UnlitMaterial>();
    if (auto* pointLt = scene->createNode3D("Point Light")) {
        ya::Entity* entity = pointLt->getEntity();
        auto        tc     = entity->getComponent<ya::TransformComponent>();
        tc->setPosition(glm::vec3(0.0, 5.f, 0.f));

        // Mesh component
        auto mc = entity->addComponent<ya::StaticMeshComponent>();
        mc->setPrimitiveGeometry(ya::EPrimitiveGeometry::Cube);

        // Material component
        auto plc = entity->addComponent<ya::PointLightComponent>();
        auto umc = entity->addComponent<ya::UnlitMaterialComponent>();

        umc->setSharedMaterial(pointLightUnlitMat);
        umc->setTextureSlot(ya::EUnlitMaterialTextureSlot::BaseColor1, "Engine/Content/TestTextures/icons8-light-64.png");
        umc->getParamsMut().mixValue = 0.8f;
        if (auto* slot = umc->getTextureSlot(ya::EUnlitMaterialTextureSlot::BaseColor1)) {
            slot->bEnable    = true;
            slot->uvRotation = glm::degrees(glm::radians(90.f));
        }

        auto* bc = entity->addComponent<ya::BillboardComponent>();
        bc->image.fromPath("Engine/Content/TestTextures/icons8-light-64.png");

        // 添加 Lua 圆周运动脚本（新 API）
        auto lsc = entity->addComponent<ya::LuaScriptComponent>();
        lsc->addScript("Engine/Content/Lua/TestPointLight.lua");
    }
    if (auto* pointLt = scene->createNode3D("Point Light 2")) {
        ya::Entity* entity = pointLt->getEntity();
        auto        tc     = entity->getComponent<ya::TransformComponent>();
        tc->setPosition(glm::vec3(-10.0, 5.f, 0.f));

        // Mesh component
        auto mc = entity->addComponent<ya::StaticMeshComponent>();
        mc->setPrimitiveGeometry(ya::EPrimitiveGeometry::Cube);

        // Material component
        auto plc = entity->addComponent<ya::PointLightComponent>();
        auto umc = entity->addComponent<ya::UnlitMaterialComponent>();

        umc->setSharedMaterial(pointLightUnlitMat);
        umc->setTextureSlot(ya::EUnlitMaterialTextureSlot::BaseColor1, "Engine/Content/TestTextures/icons8-light-64.png");
        umc->getParamsMut().mixValue = 0.8f;
        if (auto* slot = umc->getTextureSlot(ya::EUnlitMaterialTextureSlot::BaseColor1)) {
            slot->bEnable    = true;
            slot->uvRotation = glm::degrees(glm::radians(90.f));
        }

        auto* bc = entity->addComponent<ya::BillboardComponent>();
        bc->image.fromPath("Engine/Content/TestTextures/icons8-light-64.png");

        // 添加 Lua 圆周运动脚本（新 API）
        // auto lsc = entity->addComponent<ya::LuaScriptComponent>();
        // lsc->addScript("Engine/Content/Lua/TestPointLight.lua");
    }
    if (auto* wall = scene->createNode3D("Wall")) {
        ya::Entity* entity = wall->getEntity();
        auto        tc     = entity->getComponent<ya::TransformComponent>();
        tc->setPosition(glm::vec3(10.0f, 0.0f, -10.0f));
        tc->setScale(glm::vec3(10.0f, 10.0f, 0.3f));
        tc->setRotation({0, -90, 0});

        // Mesh component
        auto mc = entity->addComponent<ya::StaticMeshComponent>();
        mc->setPrimitiveGeometry(ya::EPrimitiveGeometry::Cube);

        // Material component
        auto lmc = entity->addComponent<ya::PhongMaterialComponent>();
        if (auto* diffuseSlot = lmc->setTextureSlot(ya::EPhongMaterialTextureSlot::Diffuse,
                                                    "Engine/ThirdParty/LearnOpenGL/resources/textures/brickwall.jpg")) {
            diffuseSlot->bEnable = true;
        }
        if (auto* normalSlot = lmc->setTextureSlot(ya::EPhongMaterialTextureSlot::Normal,
                                                   "Engine/ThirdParty/LearnOpenGL/resources/textures/brickwall_normal.jpg")) {
            normalSlot->bEnable = true;
        }
    }

    if (auto* dirLt = scene->createNode3D("Directional Light")) {
        ya::Entity* entity = dirLt->getEntity();
        auto        tc     = entity->getComponent<ya::TransformComponent>();
        tc->setPosition(glm::vec3(0.0f, 10.0f, 0.0f));
        tc->setRotation(glm::vec3(-45.0f, 45.0f, 0.0f));

        auto dlc        = entity->addComponent<ya::DirectionalLightComponent>();
        dlc->_color     = glm::vec3(30.0f / 256.0f);
        dlc->intensity  = 1.0f;
        dlc->_direction = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f));

        auto mc = entity->addComponent<ya::StaticMeshComponent>();
        mc->setPrimitiveGeometry(ya::EPrimitiveGeometry::Sphere);

        auto pmc = entity->addComponent<ya::PhongMaterialComponent>();
    }

    // ========================================
    // PBR Sphere Grid — metallic (rows) x roughness (cols)
    // Classic LearnOpenGL-style PBR demo scene
    // ========================================
    {
        constexpr int   rows    = 7;
        constexpr int   cols    = 7;
        constexpr float spacing = 2.5f;
        glm::vec3       gridOrigin(20.0f, 0.0f, 0.0f); // offset from other objects

        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < cols; ++col) {
                auto* node   = scene->createNode3D(std::format("PBR_Sphere_{}_{}", row, col));
                auto* entity = node->getEntity();

                auto tc = entity->getComponent<ya::TransformComponent>();
                tc->setPosition(gridOrigin + glm::vec3(
                                                 col * spacing,
                                                 row * spacing,
                                                 0.0f));

                auto mc = entity->addComponent<ya::StaticMeshComponent>();
                mc->setPrimitiveGeometry(ya::EPrimitiveGeometry::Sphere);

                auto pbrMat            = entity->addComponent<ya::PBRMaterialComponent>();
                pbrMat->getParamsMut() = ya::PBRMaterialComponent::AuthoringParams{
                    .albedo    = glm::vec3(0.5f, 0.0f, 0.0f), // Red spheres
                    .metallic  = static_cast<float>(row) / static_cast<float>(rows - 1),
                    .roughness = glm::clamp(static_cast<float>(col) / static_cast<float>(cols - 1), 0.05f, 1.0f),
                    .ao        = 1.0f,
                };
            }
        }

        // 4 point lights around the PBR grid
        struct PBRPointLight
        {
            glm::vec3 pos;
            glm::vec3 color;
            float     intensity;
        };
        PBRPointLight pbrLights[] = {
            {.pos = gridOrigin + glm::vec3(-10.0f, 10.0f, 10.0f), .color = glm::vec3(300.0f), .intensity = 15.0f},
            {.pos = gridOrigin + glm::vec3(10.0f, 10.0f, 10.0f), .color = glm::vec3(300.0f), .intensity = 15.0f},
            {.pos = gridOrigin + glm::vec3(-10.0f, -10.0f, 10.0f), .color = glm::vec3(300.0f), .intensity = 15.0f},
            {.pos = gridOrigin + glm::vec3(10.0f, -10.0f, 10.0f), .color = glm::vec3(300.0f), .intensity = 15.0f},
        };

        for (int i = 0; i < 4; ++i) {
            auto* node   = scene->createNode3D(std::format("PBR_PointLight_{}", i));
            auto* entity = node->getEntity();

            auto tc = entity->getComponent<ya::TransformComponent>();
            tc->setPosition(pbrLights[i].pos);
            tc->setScale(glm::vec3(0.3f));

            auto mc = entity->addComponent<ya::StaticMeshComponent>();
            mc->setPrimitiveGeometry(ya::EPrimitiveGeometry::Sphere);

            auto plc       = entity->addComponent<ya::PointLightComponent>();
            plc->color     = pbrLights[i].color;
            plc->intensity = pbrLights[i].intensity;

            auto umc = entity->addComponent<ya::UnlitMaterialComponent>();
            umc->setSharedMaterial(pointLightUnlitMat);
        }
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
        auto mc = entity->addComponent<ya::StaticMeshComponent>();
        mc->setPrimitiveGeometry(ya::EPrimitiveGeometry::Cube);

        // Get pre-created material params from factory (loaded from JSON)
        auto existingMat = ya::MaterialFactory::get()->getMaterialByName(_pongMaterialNames[i])->as<ya::PhongMaterial>();

        // Material component
        auto lmc = entity->addComponent<ya::PhongMaterialComponent>();
        lmc->setSharedMaterial(existingMat);
        lmc->getParamsMut().ambient   = existingMat->getParams().ambient;
        lmc->getParamsMut().diffuse   = existingMat->getParams().diffuse;
        lmc->getParamsMut().specular  = existingMat->getParams().specular;
        lmc->getParamsMut().shininess = existingMat->getParams().shininess;

        // TODO: implement the 3D UI system to show material name
        // auto wc          = entity->addComponent<ya::WidgetComponent>();
        // auto textBlock   = ya::UIFactory::create<ya::UITextBlock>();
        // textBlock->_font = ya::FontManager::get()->getFont("JetBrainsMono-Medium", 18).get();
        // textBlock->setText(_pongMaterialNames[i]);
        // wc->widget = textBlock;
    }
}
