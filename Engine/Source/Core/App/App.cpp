#include "App.h"


// Core
#include "Core/App/App.h"
#include "Core/App/SDLMisc.h"
#include "Core/Debug/RenderDocCapture.h"
#include "Core/Event.h"
#include "Core/KeyCode.h"
#include "Core/MessageBus.h"
#include "Core/System/FileWatcher.h"
#include "Core/System/VirtualFileSystem.h"



// Managers/System
#include "Core/Manager/Facade.h"

#include "ECS/Component/PointLightComponent.h"
#include "ImGuiHelper.h"
#include "Render/Core/FrameBuffer.h"
#include "Scene/SceneManager.h"

#include "Resource/AssetManager.h"
#include "Resource/PrimitiveMeshCache.h"
// #include "Resource/RenderTargetPool.h"
#include "Resource/ResourceRegistry.h"
#include "Resource/TextureLibrary.h"


// Render Core
#include "Render/Core/Sampler.h"
#include "Render/Core/Texture.h"



// ECS

#include "ECS/Component/CameraComponent.h"
#include "ECS/Component/PlayerComponent.h"
#include "ECS/Entity.h"
#include "ECS/System/2D/UIComponentSytstem.h"
#include "ECS/System/ComponentLinkageSystem.h"
#include "ECS/System/LuaScriptingSystem.h"

#include "ECS/System/3D/SkyboxSystem.h"
#include "ECS/System/Render/DebugRenderSystem.h"
#include "ECS/System/Render/PhongMaterialSystem.h"
#include "ECS/System/Render/SimpleMaterialSystem.h"
#include "ECS/System/Render/UnlitMaterialSystem.h"

#include "ECS/System/ResourceResolveSystem.h"
#include "ECS/System/TransformSystem.h"



// Render
#include "Platform/Render/Vulkan//VulkanRender.h"
#include "Platform/Render/Vulkan/VulkanImage.h"
#include "Platform/Render/Vulkan/VulkanImageView.h"
#include "Render/2D/Render2D.h"
#include "Render/Core/Swapchain.h"
#include "Render/Material/MaterialFactory.h"
#include "Render/Mesh.h"
#include "Render/Pipelines/BasicPostprocessing.h"
#include "Render/Pipelines/ShadowMapping.h"
#include "Render/Render.h"



// UI
#include "Core/UI/UIButton.h"
#include "Core/UI/UIManager.h"
#include "Core/UI/UIPanel.h"
#include "Core/UI/UITextBlock.h"

// Utility
#include "utility.cc/ranges.h"

// GLM
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <filesystem>

// SDL
#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_misc.h>


// Scene
#include "Scene/Scene.h"


namespace ya
{

static void openDirectoryInOS(const std::string& filePath)
{
    if (filePath.empty()) {
        YA_CORE_WARN("File path is empty, cannot open directory");
        return;
    }
    auto dir = std::filesystem::path(filePath).parent_path();
    if (dir.empty()) {
        YA_CORE_WARN("Directory path is empty for file: {}", filePath);
        return;
    }
    dir     = std::filesystem::absolute(dir);
    auto p  = std::format("file:///{}", dir.string());
    bool ok = SDL_OpenURL(p.c_str());
    if (!ok) {
        YA_CORE_ERROR("Failed to open directory {}: {}", dir.string(), SDL_GetError());
    }
}

// Define the static member variable
App*     App::_instance        = nullptr;
uint32_t App::App::_frameIndex = 0;



void App::renderScene(ICommandBuffer* cmdBuf, float dt, FrameContext& ctx)
{
    _simpleMaterialSystem->tick(cmdBuf, dt, &ctx);
    _unlitMaterialSystem->tick(cmdBuf, dt, &ctx);
    _phongMaterialSystem->tick(cmdBuf, dt, &ctx);
    _debugRenderSystem->tick(cmdBuf, dt, &ctx);
    // early-z  to render skybox last ?
    _skyboxSystem->tick(cmdBuf, dt, &ctx);
}

bool App::recreateViewPortRT(uint32_t width, uint32_t height)
{
    if (_render && _viewportRT) {
        _render->waitIdle();
    }
    _viewportTexture = nullptr;

    RenderTargetCreateInfo viewportRTci = RenderTargetCreateInfo{
        .label            = "Viewport RenderTarget",
        .renderingMode    = ERenderingMode::DynamicRendering,
        .bSwapChainTarget = false,
        // update when viewport resized
        .extent           = {.width = width, .height = height},
        .frameBufferCount = 1,
        .attachments      = {

            .colorAttach = {
                AttachmentDescription{
                    .index          = 0,
                    .format         = COLOR_FORMAT,
                    .samples        = ESampleCount::Sample_1,
                    .loadOp         = EAttachmentLoadOp::Clear,
                    .storeOp        = EAttachmentStoreOp::Store,
                    .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                    .stencilStoreOp = EAttachmentStoreOp::DontCare,
                    .initialLayout  = EImageLayout::ShaderReadOnlyOptimal,
                    .finalLayout    = EImageLayout::ShaderReadOnlyOptimal,
                    .usage          = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                },
            },
            .depthAttach = AttachmentDescription{
                .index          = 1,
                .format         = DEPTH_FORMAT,
                .samples        = ESampleCount::Sample_1,
                .loadOp         = EAttachmentLoadOp::Clear,
                .storeOp        = EAttachmentStoreOp::Store,
                .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                .stencilStoreOp = EAttachmentStoreOp::DontCare,
                .initialLayout  = EImageLayout::DepthStencilAttachmentOptimal,
                .finalLayout    = EImageLayout::DepthStencilAttachmentOptimal,
                .usage          = EImageUsage::DepthStencilAttachment,
            },
        } // namespace ya
    };

    if (bMSAA) {
        viewportRTci.attachments.colorAttach[0].samples = ESampleCount::Sample_4;
        viewportRTci.attachments.depthAttach->samples   = ESampleCount::Sample_4;

        viewportRTci.attachments.resolveAttach = AttachmentDescription{
            .index          = 2,
            .format         = COLOR_FORMAT,
            .samples        = ESampleCount::Sample_1,
            .loadOp         = EAttachmentLoadOp::DontCare,
            .storeOp        = EAttachmentStoreOp::Store,
            .stencilLoadOp  = EAttachmentLoadOp::DontCare,
            .stencilStoreOp = EAttachmentStoreOp::DontCare,
            .initialLayout  = EImageLayout::Undefined,
            .finalLayout    = EImageLayout::ShaderReadOnlyOptimal,
            .usage          = EImageUsage::ColorAttachment | EImageUsage::Sampled,
        };
    }

    _viewportRT = ya::createRenderTarget(viewportRTci);
    if (_viewportRT) {
        auto fb          = _viewportRT->getCurFrameBuffer();
        _viewportTexture = bMSAA ? fb->getResolveTexture() : fb->getColorTexture(0);
    }
    return _viewportRT != nullptr;
}


// ===== TODO: These global variables should be moved to appropriate managers =====



ClearValue colorClearValue = ClearValue(0.0f, 0.0f, 0.0f, 1.0f);
ClearValue depthClearValue = ClearValue(1.0f, 0);



void App::init(AppDesc ci)
{
    YA_PROFILE_FUNCTION_LOG();
    _ci = ci;
    YA_CORE_ASSERT(_instance == nullptr, "Only one instance of App is allowed");
    _instance = this;

    // register terminal  C-c signal
    handleSystemSignals();


    {
        YA_PROFILE_SCOPE_LOG("App Init Subsystems");
        {
            YA_PROFILE_SCOPE_LOG("Static Initializers");
            profiling::StaticInitProfiler::recordStart();
            ClassRegistry::instance().executeAllPostStaticInitializers();
            profiling::StaticInitProfiler::recordEnd();
        }
        Logger::init();
        VirtualFileSystem::init();
        FileWatcher::init();
        // 反射系统已通过静态初始化自动注册（YA_REFLECT 宏）
        MaterialFactory::init();
    }

    currentRenderAPI = ERenderAPI::Vulkan;

    // WHY: validate shaders before render initialized, avoid vk init(about > 3s) but shader error
    auto shaderProcessor = ShaderProcessorFactory()
                               .withProcessorType(ShaderProcessorFactory::EProcessorType::GLSL)
                               .withShaderStoragePath("Engine/Shader/GLSL")
                               .withCachedStoragePath("Engine/Intermediate/Shader/GLSL")
                               .FactoryNew<GLSLProcessor>();
    std::vector<std::string> commonDefines = {
        std::format("MAX_POINT_LIGHTS {}", MAX_POINT_LIGHTS),
    };
    _shaderStorage = std::make_shared<ShaderStorage>(shaderProcessor);

    // Register Slang processor for .slang shader files (hot-reload supported)
    auto slangProcessor = ShaderProcessorFactory()
                              .withProcessorType(ShaderProcessorFactory::EProcessorType::Slang)
                              .withShaderStoragePath("Engine/Shader/Slang")
                              .withCachedStoragePath("Engine/Intermediate/Shader/Slang")
                              .FactoryNew<SlangProcessor>();
    _shaderStorage->setSlangProcessor(slangProcessor);
    _shaderStorage->load(ShaderDesc{.shaderName = "Test/Unlit.glsl"});
    _shaderStorage->load(ShaderDesc{.shaderName = "Test/SimpleMaterial.glsl"});
    _shaderStorage->load(ShaderDesc{.shaderName = "Sprite2D.glsl"});
    // _shaderStorage->validate(ShaderDesc{.shaderName = "Test/PhongLit.glsl"}); // macro defines by various material system, so validate only, load when create pipeline
    _shaderStorage->load(ShaderDesc{.shaderName = "Test/DebugRender.glsl"});
    _shaderStorage->load(ShaderDesc{.shaderName = "PostProcessing/Basic.glsl"});
    _shaderStorage->load(ShaderDesc{.shaderName = "Skybox.glsl"});
    _shaderStorage->load(ShaderDesc{.shaderName = "Shadow/DirectionalLightDepthBuffer.glsl"});
    _shaderStorage->load(ShaderDesc{.shaderName = "Shadow/CombinedShadowDepthGenerate.glsl", .defines = commonDefines});
    _shaderStorage->validate(ShaderDesc{.shaderName = "PhongLit/PhongLit.glsl", .defines = commonDefines});
    _shaderStorage->validate(ShaderDesc{.shaderName = "PhongLit.slang", .defines = commonDefines});


    // MARK: Render/Hook

    if (_ci.bEnableRenderDoc) {
        // before render init to hook apis
        _renderDocCapture             = ya::makeShared<RenderDocCapture>();
        _renderDocConfiguredDllPath   = _ci.renderDocDllPath;
        _renderDocConfiguredOutputDir = _ci.renderDocCaptureOutputDir;
        _renderDocCapture->init(_renderDocConfiguredDllPath, _renderDocConfiguredOutputDir);
        _renderDocCapture->setCaptureFinishedCallback([this](const RenderDocCapture::CaptureResult& result) {
            if (!result.bSuccess) {
                return;
            }

            _renderDocLastCapturePath = result.capturePath;
            switch (_renderDocOnCaptureAction) {
            case 0:
            case 1:
                if (!_renderDocCapture->launchReplayUI(true, nullptr)) {
                    YA_CORE_WARN("RenderDoc: failed to launch replay UI");
                }
                break;
            case 2:
                openDirectoryInOS(result.capturePath);
                break;
            default:
                break;
            }
        });
    }


    RenderCreateInfo renderCI{
        .renderAPI   = currentRenderAPI,
        .swapchainCI = SwapchainCreateInfo{
            .imageFormat   = COLOR_FORMAT,
            .bVsync        = false,
            .minImageCount = 3,
            .width         = static_cast<uint32_t>(_ci.width),
            .height        = static_cast<uint32_t>(_ci.height),
        },
    };

    _render = IRender::create(renderCI);
    YA_CORE_ASSERT(_render, "Failed to create IRender instance");
    _render->init(renderCI);

    // Get window size
    int winW = 0, winH = 0;
    _render->getWindowSize(winW, winH);
    _windowSize.x = static_cast<float>(winW);
    _windowSize.y = static_cast<float>(winH);

    if (_ci.bEnableRenderDoc) {
        _renderDocCapture->setRenderContext({
            .device    = _render->as<VulkanRender>()->getDevice(),
            .swapchain = _render->as<VulkanRender>()->getSwapchain()->getHandle(),
        });
        _render->getSwapchain()->onRecreate.addLambda(
            this,
            [this](ISwapchain::DiffInfo old, ISwapchain::DiffInfo now, bool bImageRecreated) {
                _renderDocCapture->setRenderContext({
                    .device    = _render->as<VulkanRender>()->getDevice(),
                    .swapchain = _render->as<VulkanRender>()->getSwapchain()->getHandle(),
                });
            });
    }


    // MARK: Resources
    {
        TextureLibrary::get().init();

        // Register all resource caches with ResourceRegistry for unified cleanup
        // Priority order: higher = cleared first (GPU resources before CPU resources)
        ResourceRegistry::get().registerCache(&PrimitiveMeshCache::get(), 100); // GPU meshes first
        ResourceRegistry::get().registerCache(&TextureLibrary::get(), 90);      // GPU textures
        ResourceRegistry::get().registerCache(FontManager::get(), 80);          // Font textures
        ResourceRegistry::get().registerCache(AssetManager::get(), 70);         // General assets
    }


    _descriptorPool = IDescriptorPool::create(_render,
                                              DescriptorPoolCreateInfo{
                                                  .label     = "Global Descriptor Pool",
                                                  .maxSets   = 2, // skybox + depth shadow
                                                  .poolSizes = {
                                                      // max iamge set allowed < maxSets
                                                      DescriptorPoolSize{
                                                          .type            = EPipelineDescriptorType::CombinedImageSampler,
                                                          .descriptorCount = 1 + 2, // skybox + depth shadow(point and directional)
                                                      },
                                                  },
                                              });
    _deleter.push("DescriptorPool", [this](void*) { _descriptorPool.reset(); });


    // viewport
    _viewportRenderPass = nullptr;
    recreateViewPortRT(winW, winH);

    // shadow mapping
    _depthRT = ya::createRenderTarget(RenderTargetCreateInfo{
        .label            = "Shadow Map RenderTarget",
        .renderingMode    = ERenderingMode::DynamicRendering,
        .bSwapChainTarget = false,
        .extent           = {.width = 1024, .height = 1024},
        .frameBufferCount = 1,
        .layerCount       = 1 + MAX_POINT_LIGHTS * 6, // 1 directional light + 6 faces for each point light
        .attachments      = {

            .depthAttach = AttachmentDescription{
                .index          = 0,
                .format         = SHADOW_MAPPING_DEPTH_BUFFER_FORMAT,
                .samples        = ESampleCount::Sample_1,
                .loadOp         = EAttachmentLoadOp::Clear,
                .storeOp        = EAttachmentStoreOp::Store,
                .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                .stencilStoreOp = EAttachmentStoreOp::DontCare,
                .initialLayout  = EImageLayout::DepthStencilAttachmentOptimal,
                .finalLayout    = EImageLayout::ShaderReadOnlyOptimal,
                .usage          = EImageUsage::DepthStencilAttachment | EImageUsage::Sampled,
            },
        },
    });
    _deleter.push("DepthRT", [this](void*) { _depthRT.reset(); });

    _depthBufferDSL = IDescriptorSetLayout::create(
        _render,
        DescriptorSetLayoutDesc{
            .label    = "DepthBuffer_DSL",
            .bindings = {
                // directional light shadow map (2D)
                DescriptorSetLayoutBinding{
                    .binding         = 0,
                    .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                    .descriptorCount = 1,
                    .stageFlags      = EShaderStage::Fragment,
                },
                DescriptorSetLayoutBinding{
                    .binding         = 1,
                    .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                    .descriptorCount = 1,
                    .stageFlags      = EShaderStage::Fragment,
                },
            },
        });
    _depthBufferShadowDS = _descriptorPool->allocateDescriptorSets(_depthBufferDSL);
    _render->as<VulkanRender>()->setDebugObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET, _depthBufferShadowDS.ptr, "DepthBuffer_Shadow_DS");
    _deleter.push("DepthBufferDSL", [this](void*) { _depthBufferDSL.reset(); });

    auto tf        = _render->getTextureFactory();
    auto shadowImg = _depthRT->getCurFrameBuffer()->getDepthTexture()->getImageShared();
    // TODO: use 6 array first then 1 array last can optimize performance?
    _shadowDirectionalDepthIV = tf->createImageView(
        shadowImg,
        ImageViewCreateInfo{
            .label        = "Shadow Map Directional Depth ImageView",
            .viewType     = EImageViewType::View2D,
            .aspectFlags  = EImageAspect::Depth,
            .baseMipLevel = 0,
            .levelCount   = 1,
            // use 0 - 1 layers
            .baseArrayLayer = 0,
            .layerCount     = 1,
        });
    _shadowPointDepthIV = tf->createImageView(
        shadowImg,
        ImageViewCreateInfo{
            .label        = "Shadow Map Point Depth ImageView",
            .viewType     = EImageViewType::View2DArray,
            .aspectFlags  = EImageAspect::Depth,
            .baseMipLevel = 0,
            .levelCount   = 1,
            // use 1 - nx6 layers
            .baseArrayLayer = 1,
            .layerCount     = 6 * MAX_POINT_LIGHTS,
        });
    YA_CORE_ASSERT(_shadowDirectionalDepthIV && _shadowDirectionalDepthIV->getHandle(), "Failed to create shadow map directional depth image view");
    YA_CORE_ASSERT(_shadowPointDepthIV && _shadowPointDepthIV->getHandle(), "Failed to create shadow map point depth image view");
    auto shadowAddresMode = ESamplerAddressMode::ClampToBorder;
    _shadowSampler        = Sampler::create(
        SamplerDesc{
                   .label         = "shadow",
                   .minFilter     = EFilter::Linear,
                   .magFilter     = EFilter::Linear,
                   .mipmapMode    = ESamplerMipmapMode::Linear,
                   .addressModeU  = shadowAddresMode,
                   .addressModeV  = shadowAddresMode,
                   .addressModeW  = shadowAddresMode,
                   .mipLodBias    = 0.0f,
                   .maxAnisotropy = 1.0f,
                   .borderColor   = SamplerDesc::BorderColor{
                         .type  = SamplerDesc::EBorderColor::FloatOpaqueWhite,
                         .color = {1.0f, 1.0f, 1.0f, 1.0f},
            },
        });
    YA_CORE_ASSERT(_shadowSampler, "Failed to create shadow sampler");
    _deleter.push("ShadowSampler", [this](void*) { _shadowSampler.reset(); });

    _render
        ->getDescriptorHelper()
        ->updateDescriptorSets({
            IDescriptorSetHelper::writeOneImage(_depthBufferShadowDS, 0, _shadowDirectionalDepthIV.get(), _shadowSampler.get()),
            IDescriptorSetHelper::writeOneImage(_depthBufferShadowDS, 1, _shadowPointDepthIV.get(), _shadowSampler.get()),
        });
    _deleter.push("Shadow ImageViews", [this](void*) {
        _shadowDirectionalDepthIV.reset();
        _shadowPointDepthIV.reset();
    });


    {
        _postprocessTexture = Texture::createRenderTexture(RenderTextureCreateInfo{
            .label   = "PostprocessRenderTarget",
            .width   = static_cast<uint32_t>(winW),
            .height  = static_cast<uint32_t>(winH),
            .format  = EFormat::R8G8B8A8_UNORM,
            .usage   = EImageUsage::ColorAttachment | EImageUsage::Sampled,
            .samples = ESampleCount::Sample_1,
            .isDepth = false,
        });
        _deleter.push("PostprocessTexture", [this](void*) {
            _postprocessTexture.reset();
        });
    }

    // mirror
    {
        _mirrorRT = ya::createRenderTarget(RenderTargetCreateInfo{
            .label            = "Mirror RenderTarget",
            .renderingMode    = ERenderingMode::DynamicRendering,
            .bSwapChainTarget = false,
            .extent           = {
                          .width  = static_cast<uint32_t>(winW),
                          .height = static_cast<uint32_t>(winH),
            },
            .frameBufferCount = 1,
            .attachments      = {

                .colorAttach = {
                    AttachmentDescription{
                        .index          = 0,
                        .format         = EFormat::R8G8B8A8_UNORM,
                        .samples        = ESampleCount::Sample_1,
                        .loadOp         = EAttachmentLoadOp::Clear,
                        .storeOp        = EAttachmentStoreOp::Store,
                        .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                        .stencilStoreOp = EAttachmentStoreOp::DontCare,
                        .initialLayout  = EImageLayout::Undefined,
                        .finalLayout    = EImageLayout::ShaderReadOnlyOptimal, // For sampling
                        .usage          = EImageUsage::ColorAttachment | EImageUsage::Sampled,
                    },
                },
                .depthAttach = AttachmentDescription{
                    .index          = 1,
                    .format         = DEPTH_FORMAT,
                    .samples        = ESampleCount::Sample_1,
                    .loadOp         = EAttachmentLoadOp::Clear,
                    .storeOp        = EAttachmentStoreOp::Store,
                    .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                    .stencilStoreOp = EAttachmentStoreOp::DontCare,
                    .initialLayout  = EImageLayout::Undefined,
                    .finalLayout    = EImageLayout::DepthStencilAttachmentOptimal,
                    .usage          = EImageUsage::DepthStencilAttachment,
                },
            },
        });
        _deleter.push("MirrorRT", [this](void*) {
            _mirrorRT.reset();
        });
    }

    // Screen/Editor
    {
        _screenRenderPass = nullptr;

        _screenRT = ya::createRenderTarget(RenderTargetCreateInfo{
            .label            = "Final RenderTarget",
            .renderingMode    = ERenderingMode::DynamicRendering,
            .bSwapChainTarget = true,
            .attachments      = {

                .colorAttach = {
                    AttachmentDescription{
                        .index          = 0,
                        .format         = _render->getSwapchain()->getFormat(),
                        .samples        = ESampleCount::Sample_1,
                        .loadOp         = EAttachmentLoadOp::Clear,
                        .storeOp        = EAttachmentStoreOp::Store,
                        .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                        .stencilStoreOp = EAttachmentStoreOp::DontCare,
                        .initialLayout  = EImageLayout::Undefined,
                        .finalLayout    = EImageLayout::PresentSrcKHR,
                        .usage          = EImageUsage::ColorAttachment,
                    },
                },
            },
        });

        _render->getSwapchain()->onRecreate.addLambda(
            this,
            [this](ISwapchain::DiffInfo old, ISwapchain::DiffInfo now, bool bImageRecreated) {
                Extent2D newExtent{
                    .width  = now.extent.width,
                    .height = now.extent.height,
                };

                if (bImageRecreated) {
                    // _viewportRT->setExtent(newExtent); // see App::onSceneViewportResized
                    _screenRT->setExtent(newExtent);
                }
                if ((now.extent.width != old.extent.width ||
                     now.extent.height != old.extent.height ||
                     old.presentMode != now.presentMode))
                {
                    // _viewportRT->setExtent(newExtent); // see App::onSceneViewportResized
                    _screenRT->setExtent(newExtent);
                }
            });
    }

    _render->allocateCommandBuffers(_render->getSwapchainImageCount(), _commandBuffers);


    // TODO: common resource skybox and depth buffer can be in one set?
    _skyBoxCubeMapDSL = IDescriptorSetLayout::create(_render,
                                                     DescriptorSetLayoutDesc{
                                                         .label    = "Skybox_CubeMap_DSL",
                                                         .bindings = {
                                                             DescriptorSetLayoutBinding{
                                                                 .binding         = 0,
                                                                 .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                                                                 .descriptorCount = 1,
                                                                 .stageFlags      = EShaderStage::Fragment,
                                                             },
                                                         },
                                                     });
    _skyBoxCubeMapDS  = _descriptorPool->allocateDescriptorSets(_skyBoxCubeMapDSL);
    _render->as<VulkanRender>()->setDebugObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET, _skyBoxCubeMapDS.ptr, "Skybox_CubeMap_DS");
    _deleter.push("SkyboxCubeMapDSL", [this](void*) { _skyBoxCubeMapDSL.reset(); });



    // MARK: Render Systems
    {
        _simpleMaterialSystem = ya::makeShared<SimpleMaterialSystem>();
        _simpleMaterialSystem->init(IRenderSystem::InitParams{
            .renderPass            = nullptr,
            .pipelineRenderingInfo = PipelineRenderingInfo{
                .label                   = "SimpleMaterial Pipeline",
                .viewMask                = 0,
                .colorAttachmentFormats  = {EFormat::R8G8B8A8_UNORM},
                .depthAttachmentFormat   = DEPTH_FORMAT,
                .stencilAttachmentFormat = EFormat::Undefined,
            },
        });

        _unlitMaterialSystem = ya::makeShared<UnlitMaterialSystem>();
        _unlitMaterialSystem->init(IRenderSystem::InitParams{
            .renderPass            = nullptr,
            .pipelineRenderingInfo = PipelineRenderingInfo{
                .label                   = "UnlitMaterial Pipeline",
                .viewMask                = 0,
                .colorAttachmentFormats  = {EFormat::R8G8B8A8_UNORM},
                .depthAttachmentFormat   = DEPTH_FORMAT,
                .stencilAttachmentFormat = EFormat::Undefined,
            },
        });
        _phongMaterialSystem = ya::makeShared<PhongMaterialSystem>();
        _phongMaterialSystem->init(IRenderSystem::InitParams{
            .renderPass            = nullptr,
            .pipelineRenderingInfo = PipelineRenderingInfo{
                .label                   = "PhongMaterial Pipeline",
                .viewMask                = 0,
                .colorAttachmentFormats  = {EFormat::R8G8B8A8_UNORM},
                .depthAttachmentFormat   = DEPTH_FORMAT,
                .stencilAttachmentFormat = EFormat::Undefined,
            },
        });
        _debugRenderSystem = ya::makeShared<DebugRenderSystem>();
        _debugRenderSystem->init(IRenderSystem::InitParams{
            .renderPass            = nullptr,
            .pipelineRenderingInfo = PipelineRenderingInfo{
                .label                   = "DebugRender Pipeline",
                .viewMask                = 0,
                .colorAttachmentFormats  = {EFormat::R8G8B8A8_UNORM},
                .depthAttachmentFormat   = DEPTH_FORMAT,
                .stencilAttachmentFormat = EFormat::Undefined,
            },
        });


        _skyboxSystem = ya::makeShared<SkyBoxSystem>();
        _skyboxSystem->init(IRenderSystem::InitParams{
            .renderPass            = nullptr,
            .pipelineRenderingInfo = PipelineRenderingInfo{
                .label                   = "Skybox Pipeline",
                .viewMask                = 0,
                .colorAttachmentFormats  = {EFormat::R8G8B8A8_UNORM},
                .depthAttachmentFormat   = DEPTH_FORMAT,
                .stencilAttachmentFormat = EFormat::Undefined,
            },
        });

        _shadowMappingSystem = ya::makeShared<ShadowMapping>();
        _shadowMappingSystem->init(IRenderSystem::InitParams{
            .renderPass            = nullptr,
            .pipelineRenderingInfo = PipelineRenderingInfo{
                .label                   = "ShadowMapping Pipeline",
                .viewMask                = 0,
                .colorAttachmentFormats  = {},
                .depthAttachmentFormat   = SHADOW_MAPPING_DEPTH_BUFFER_FORMAT,
                .stencilAttachmentFormat = EFormat::Undefined,
            },
        });

        _basicPostprocessingSystem = ya::makeShared<BasicPostprocessing>();
        _basicPostprocessingSystem->init(IRenderSystem::InitParams{
            .renderPass            = nullptr,
            .pipelineRenderingInfo = PipelineRenderingInfo{
                .label                   = "BasicPostprocessing",
                .viewMask                = 0,
                .colorAttachmentFormats  = {EFormat::R8G8B8A8_UNORM},
                .depthAttachmentFormat   = EFormat::Undefined,
                .stencilAttachmentFormat = EFormat::Undefined,
            },
        });

        _onRenderRenderSystemsGUI.set([this]() {
            _simpleMaterialSystem->renderGUI();
            _unlitMaterialSystem->renderGUI();
            _phongMaterialSystem->renderGUI();
            _debugRenderSystem->renderGUI();
            _skyboxSystem->renderGUI();
            _shadowMappingSystem->renderGUI();
            _basicPostprocessingSystem->renderGUI();
        });
        _forEachSystem.set([this](Delegate<void(IRenderSystem*)> func) {
            func(_simpleMaterialSystem.get());
            func(_unlitMaterialSystem.get());
            func(_phongMaterialSystem.get());
            func(_debugRenderSystem.get());
            func(_skyboxSystem.get());
            func(_shadowMappingSystem.get());
            func(_basicPostprocessingSystem.get());
        });

        _deleter.push("RenderSystems", [this](void*) {
            _simpleMaterialSystem->onDestroy();
            _simpleMaterialSystem.reset();
            _unlitMaterialSystem->onDestroy();
            _unlitMaterialSystem.reset();
            _phongMaterialSystem->onDestroy();
            _phongMaterialSystem.reset();
            _debugRenderSystem->onDestroy();
            _debugRenderSystem.reset();
            _skyboxSystem->onDestroy();
            _skyboxSystem.reset();
            _shadowMappingSystem->onDestroy();
            _shadowMappingSystem.reset();
            _basicPostprocessingSystem->onDestroy();
            _basicPostprocessingSystem.reset();
        });

        // Initialize Render2D for dynamic rendering (depthTestEnable=false allows UI pass without depth)
        Render2D::init(_render);
    }

    // MARK: Resource-Inject
    // Inject shared resources into render systems
    _render->waitIdle(); // wait something done

    _skyboxSystem->as<SkyBoxSystem>()->_cubeMapDS                    = _skyBoxCubeMapDS;
    _phongMaterialSystem->as<PhongMaterialSystem>()->skyBoxCubeMapDS = _skyBoxCubeMapDS;

    _shadowMappingSystem->as<ShadowMapping>()->setRenderTarget(_depthRT);
    _phongMaterialSystem->as<PhongMaterialSystem>()->depthBufferDS = _depthBufferShadowDS;
    _phongMaterialSystem->as<PhongMaterialSystem>()->setDirectionalShadowMappingEnabled(bShadowMapping);

    _debugRenderSystem->bEnabled = false;


    // MARK: Render Init Done
    ImGuiManager::get().init(_render, nullptr);

    _render->waitIdle(); // wait something done


    _sceneManager = new SceneManager();
    _sceneManager->onSceneInit.addLambda(this, [this](Scene* scene) { this->onSceneInit(scene); });
    _sceneManager->onSceneActivated.addLambda(this, [this](Scene* scene) { this->onSceneActivated(scene); });
    _sceneManager->onSceneDestroy.addLambda(this, [this](Scene* scene) { this->onSceneDestroy(scene); });


    FPSControl::get()->bEnable = true;
    FPSControl::get()->setFPSLimit(120.f);

    auto sys = ya::makeShared<ResourceResolveSystem>();
    sys->init();
    _systems.push_back(sys);
    auto sys2 = ya::makeShared<TransformSystem>();
    sys2->init();
    _systems.push_back(sys2);
    auto sys3 = ya::makeShared<ComponentLinkageSystem>();
    sys3->init();
    _systems.push_back(sys3);
    _deleter.push("Systems", [this](void*) {
        for (auto& sys : _systems) {
            sys->shutdown();
        }
        _systems.clear();
    });


    _editorLayer = new EditorLayer(this);
    _editorLayer->onAttach();
    // todo: use ref
    // _editorLayer->onViewportResized.set([this](Rect2D rect) {
    //     onSceneViewportResized(rect);
    // });

    // see TypeRenderer.h
    ya::registerBuiltinTypeRenderers();

    // _viewportRT->onFrameBufferRecreated.addLambda(this, [this]() {
    //     YA_CORE_INFO("EditorLayer: viewport RT recreated, cleaning up ImGui texture cache");
    //     auto imageView = _viewportRT->getFrameBuffer()->getImageView(0);
    //     _editorLayer->setViewportImage(imageView);
    // });
    _luaScriptingSystem = new LuaScriptingSystem();
    _luaScriptingSystem->init();
    _deleter.push("LuaScriptingSystem", [this](void*) {
        _luaScriptingSystem->shutdown();
        delete _luaScriptingSystem;
        _luaScriptingSystem = nullptr;
    });


    {
        YA_PROFILE_SCOPE_LOG("Inheritance Init");
        onInit(ci);
    }

    {
        YA_PROFILE_SCOPE_LOG("Post Init");
        onPostInit();
    }


    loadScene(ci.defaultScenePath);

    camera.setPosition(glm::vec3(0.0f, 0.0f, 5.0f));
    camera.setRotation(glm::vec3(0.0f, 0.0f, 0.0f));
    camera.setPerspective(45.0f, 16.0f / 9.0f, 0.1f, 100.0f);
}

template <typename T>
int App::dispatchEvent(const T& event)
{
    if (0 == onEvent(event)) {
        MessageBus::get()->publish(event);
    }
    return 0;
}

void App::renderGUI(float dt)
{
    _editorLayer->onImGuiRender([this, dt]() {
        this->onRenderGUI(dt);
    });
}

// MARK: on init
void App::onInit(AppDesc ci)
{
    // auto &bus = *MessageBus::get();
    FontManager::get()->loadFont("Engine/Content/Fonts/JetBrainsMono-Medium.ttf", "JetBrainsMono-Medium", 48);

    // auto panel     = UIFactory::create<UIPanel>();
    // panel->_color  = FUIColor(0.2f, 0.2f, 0.2f, 0.8f);
    // auto btn       = UIFactory::create<UIButton>();
    // btn->_position = {50.0f, 50.0f};
    // btn->_size     = {200.0f, 100.0f};
    // panel->addChild(btn);

    // auto textBlock   = UIFactory::create<UITextBlock>();
    // textBlock->_font = FontManager::get()->getFont("JetBrainsMono-Medium", 48).get();
    // btn->addChild(textBlock);


    auto mgr = UIManager::get();
    // mgr->_rootCanvas->addChild(panel);
}

void App::onPostInit()
{
    // those resources are depends on the render context
    // LOAD demo testing textures
    const char* faceTexturePath = "Engine/Content/TestTextures/face.png";
    const char* uv1TexturePath  = "Engine/Content/TestTextures/uv1.png";

    ya::AssetManager::get()->loadTexture("face", faceTexturePath);
    ya::AssetManager::get()->loadTexture("uv1", uv1TexturePath);

    onScenePostInit.broadcast();
}


int App::onEvent(const Event& event)
{
    // YA_CORE_TRACE("Event processed: {}", event.type);
    EventProcessState ret = ImGuiManager::get().processEvent(event);
    if (ret != EventProcessState::Continue) {
        return 0; // ImGui captured the event
    }


    bool      bHandled = false;
    EEvent::T ty       = event.getEventType();
    switch (ty) {
    case EEvent::MouseMoved:
    {
        bHandled |= onMouseMoved(static_cast<const MouseMoveEvent&>(event));

    } break;
    case EEvent::MouseButtonReleased:
    {
        bHandled |= onMouseButtonReleased(static_cast<const MouseButtonReleasedEvent&>(event));
    } break;
    case EEvent::WindowResize:
    {
        bHandled |= onWindowResized(static_cast<const WindowResizeEvent&>(event));
    } break;
    case EEvent::KeyReleased:
    {
        bHandled |= onKeyReleased(static_cast<const KeyReleasedEvent&>(event));
    } break;
    case EEvent::MouseScrolled:
    {
        bHandled |= onMouseScrolled(static_cast<const MouseScrolledEvent&>(event));
    } break;
    case EEvent::None:
        break;
    case EEvent::WindowClose:
        requestQuit();
        break;
    case EEvent::WindowRestore:
        _bMinimized = false;
        break;
    case EEvent::WindowMinimize:
        _bMinimized = true;
        break;
    case EEvent::WindowFocus:
    case EEvent::WindowFocusLost:
    case EEvent::WindowMoved:
    case EEvent::AppTick:
    case EEvent::AppUpdate:
    case EEvent::AppRender:
        break;
    case EEvent::AppQuit:
        requestQuit();
        break;
    case EEvent::KeyPressed:
    case EEvent::KeyTyped:
    case EEvent::MouseButtonPressed:
    case EEvent::EventTypeCount:
    case EEvent::ENUM_MAX:
        break;
    }

    if (bHandled) {
        return 0;
    }

    inputManager.processEvent(event);


    if (bHandled) {
        return 0;
    }

    bool bInViewport = FUIHelper::isPointInRect(_lastMousePos, _viewportRect.pos, _viewportRect.extent);
    // currently ui only rendering in viewport
    if (bInViewport)
    {
        UIAppCtx ctx{
            .lastMousePos = _lastMousePos,
            .bInViewport  = bInViewport,
            .viewportRect = _viewportRect,
        };
        _editorLayer->screenToViewport(_lastMousePos, ctx.lastMousePos);
        UIManager::get()->onEvent(event, ctx);
    }

    _editorLayer->onEvent(event);

    // if()
    return 0;
}


// MARK: QUIT
void ya::App::quit()
{
    if (_render) {
        _render->waitIdle();
    }
    {
        YA_PROFILE_SCOPE_LOG("Inheritance Quit");
        onQuit();
    }

    unloadScene();
    _editorLayer->onDetach();
    delete _editorLayer;

    // CRITICAL: Destroy SceneManager BEFORE LuaScriptingSystem
    // LuaScriptComponent holds sol::object references to lua state
    // If lua state is destroyed first, component destruction will crash
    delete _sceneManager;

    MaterialFactory::get()->destroy();

    Render2D::destroy();

    // Cleanup managers
    // if (ImGuiManager::get()) {
    ImGuiManager::get().shutdown();
    // }

    // Cleanup material systems (managed externally now)

    // Cleanup render targets before render passes (dependency order)
    if (_viewportRT) {
        _viewportRT->destroy();
        _viewportRT.reset();
    }

    if (_screenRT) {
        _screenRT->destroy();
        _screenRT.reset();
    }

    _screenRenderPass.reset();
    _viewportRenderPass.reset();

    _deleter.clear();

    if (_renderDocCapture) {
        _renderDocCapture->shutdown();
        _renderDocCapture.reset();
    }

    // Unified cleanup of all resource caches in priority order
    ResourceRegistry::get().clearAll();

    if (_render) {
        _render->waitIdle();
        _commandBuffers.clear();

        _render->destroy();
        delete _render;
        _render = nullptr;
    }
}



int ya::App::run()
{

    _startTime = std::chrono::steady_clock::now();
    _lastTime  = _startTime;

    while (bRunning) {

        time_point_t now        = clock_t::now();
        auto         dtMicroSec = std::chrono::duration_cast<std::chrono::microseconds>(now - _lastTime).count();
        float        dtSec      = (float)((double)dtMicroSec / 1000000.0);
        dtSec                   = std::max(dtSec, 0.0001f);
        _lastTime               = now;


        if (auto result = iterate(dtSec); result != 0) {
            break;
        }
    }

    return 0;
}


int ya::App::processEvent(SDL_Event& event)
{
    processSDLEvent(
        event,
        [this](const auto& e) { this->dispatchEvent(e); });
    return 0;
};


int ya::App::iterate(float dt)
{
    YA_PROFILE_FUNCTION()
    SDL_Event evt;
    SDL_PollEvent(&evt);
    processEvent(evt);

    dt += FPSControl::get()->update(dt);

    // Skip rendering when minimized to avoid swapchain recreation with invalid extent
    // TODO: only skip render, but still update logic
    if (_bMinimized) {
        SDL_Delay(100); // Small delay to reduce CPU usage when minimized
        // YA_CORE_INFO("Application minimized, skipping frame");
        return 0;
    }
    if (!_bPause) {
        tickLogic(dt);
    }
    tickRender(dt);
    ++_frameIndex;
    return 0;
}


// MARK: update
void App::tickLogic(float dt)
{
    YA_PROFILE_FUNCTION()
    taskManager.update();
    Facade.timerManager.onUpdate(dt);

    // Store real-time delta for editor
    // Note: ClearValue is now set in onRender() via beginRenderPass() or beginRendering()

    for (auto& sys : _systems) {
        sys->onUpdate(dt);
    }


    Render2D::onUpdate(dt);

    switch (_appState) {

    case AppState::Editor:
        break;
    case AppState::Simulation:
    case AppState::Runtime:
    {
        _luaScriptingSystem->onUpdate(dt);
    } break;
    }

    // 文件监视器轮询（检测文件变化）
    if (auto* watcher = FileWatcher::get()) {
        watcher->poll();
    }

    _editorLayer->onUpdate(dt);
    inputManager.postUpdate();



    inputManager.preUpdate();
    // Update Editor camera (FreeCamera)
    cameraController.update(camera, inputManager, dt);
}

// MARK: Render
void App::tickRender(float dt)
{
    tickRenderPipeline(dt);
}

// MARK: Render GUI
void App::onRenderGUI(float dt)
{
    YA_PROFILE_FUNCTION()
    auto& io = ImGui::GetIO();
    if (!ImGui::Begin("App Info"))
    {
        ImGui::End();
        return;
    }

    if (ImGui::CollapsingHeader("Render 2D", 0)) {
        // temp code here to adopt to new Render2D
        Render2D::onImGui();
    }

    if (ImGui::CollapsingHeader("Render Target Pool", 0)) {
        // RenderTargetPool::get().onRenderGUI();
    }

    if (ImGui::CollapsingHeader("Render Systems", ImGuiTreeNodeFlags_DefaultOpen)) {
        _onRenderRenderSystemsGUI.executeIfBound();
    }

    _viewportRT->onRenderGUI();
    _screenRT->onRenderGUI();

    if (ImGui::CollapsingHeader("Context", ImGuiTreeNodeFlags_DefaultOpen)) {

        {
            static constexpr int ringBufSize = 120;                          // 环形缓冲区大小，保持原有定义
            static float         fpsRingBuf[ringBufSize]{};                  // 存储每帧的FPS值
            static int           fpsRingHead = 0;                            // 缓冲区头指针（下一个要写入的位置）
            static int           fpsRingFill = 0;                            // 缓冲区已填充的元素数量
            static float         fpsSum      = 0.0f;                         // 新增：维护缓冲区所有元素的累加和，避免每次遍历求和
            float                currentFps  = dt > 0.0f ? 1.0f / dt : 0.0f; // 计算当前帧的FPS（dt为帧间隔时间，单位：秒）

            // 1. 处理旧值：缓冲区满时，先减去即将被覆盖的旧值
            if (fpsRingFill >= ringBufSize) {
                fpsSum -= fpsRingBuf[fpsRingHead]; // head指向的就是即将被覆盖的最旧元素
            }
            // 2. 写入新值并更新累加和
            fpsRingBuf[fpsRingHead] = currentFps;
            fpsSum += currentFps;

            // 3. 更新环形缓冲区指针和填充数
            fpsRingHead = (fpsRingHead + 1) % ringBufSize;
            fpsRingFill = std::min(fpsRingFill + 1, ringBufSize);

            // 4. 计算平均FPS（O(1)时间复杂度）
            float avgFps = fpsRingFill > 0 ? (fpsSum / static_cast<float>(fpsRingFill)) : 0.0f;

            // 输出FPS信息（简化当前帧FPS的获取，避免重复计算索引）
            ImGui::Text("%s",
                        std::format("Frame: {}, DeltaTime: {:.2f} ms,\t FPS: {:.1f} (avg {}f: {:.1f})",
                                    _frameIndex,
                                    dt * 1000.0f,
                                    currentFps,
                                    fpsRingFill,
                                    avgFps)
                            .data());
        }


        static int count = 0;
        if (ImGui::Button(std::format("Click Me ({})", count).c_str())) {
            count++;
            YA_CORE_INFO("=====================================");
        }

        if (ImGui::TreeNode("ImGUI")) {
            ImGuiManager::get().onRenderGUI();
            ImGui::TreePop();
        }

        ImGui::DragFloat("Viewport Scale", &_viewportFrameBufferScale, 0.1f, 1.0f, 10.0f);
        if (ImGui::Checkbox("MSAA", &bMSAA)) {
            taskManager.registerFrameTask([this]() {
                auto sampleCount = bMSAA ? ESampleCount::Sample_4 : ESampleCount::Sample_1;
                _simpleMaterialSystem->getPipeline()->setSampleCount(sampleCount);
                _unlitMaterialSystem->getPipeline()->setSampleCount(sampleCount);
                _phongMaterialSystem->getPipeline()->setSampleCount(sampleCount);
                _debugRenderSystem->getPipeline()->setSampleCount(sampleCount);
                _skyboxSystem->getPipeline()->setSampleCount(sampleCount);

                uint32_t width  = _viewportRect.extent2D().width;
                uint32_t height = _viewportRect.extent2D().height;
                recreateViewPortRT(width, height);
            });
        }
        if (ImGui::Checkbox("Shadow Mapping", &bShadowMapping))
        {
            auto* phongSys = _phongMaterialSystem->as<PhongMaterialSystem>();
            phongSys->setDirectionalShadowMappingEnabled(bShadowMapping);
        }

        auto* swapchain = _render->getSwapchain();
        bool  bVsync    = swapchain->getVsync();
        if (ImGui::Checkbox("VSync", &bVsync)) {
            taskManager.registerFrameTask([swapchain, bVsync]() {
                swapchain->setVsync(bVsync);
            });
        }

        EPresentMode::T presentMode  = swapchain->getPresentMode();
        const char*     presentModes = "Immediate\0Mailbox\0FIFO\0FIFO Relaxed\0";
        if (ImGui::Combo("Present Mode", reinterpret_cast<int*>(&presentMode), presentModes)) {
            taskManager.registerFrameTask([swapchain, presentMode]() {
                swapchain->setPresentMode(presentMode);
            });
        }

        AppMode mode = _appMode;
        if (ImGui::Combo("App Mode", reinterpret_cast<int*>(&mode), "Control\0Drawing\0")) {
            _appMode = mode;
        }

        if (ImGui::TreeNode("RenderDoc")) {
            bool bAvailable = _renderDocCapture && _renderDocCapture->isAvailable();
            ImGui::Text("Available: %s", bAvailable ? "Yes" : "No");
            ImGui::TextWrapped("DLL Path: %s", _renderDocConfiguredDllPath.empty() ? "<default>" : _renderDocConfiguredDllPath.c_str());
            ImGui::TextWrapped("Output Dir: %s", _renderDocConfiguredOutputDir.empty() ? "<default>" : _renderDocConfiguredOutputDir.c_str());
            if (bAvailable) {
                bool bCaptureEnabled = _renderDocCapture->isCaptureEnabled();
                if (ImGui::Checkbox("Capture Enabled", &bCaptureEnabled)) {
                    _renderDocCapture->setCaptureEnabled(bCaptureEnabled);
                }

                bool bHudVisible = _renderDocCapture->isHUDVisible();
                if (ImGui::Checkbox("Show RenderDoc HUD", &bHudVisible)) {
                    _renderDocCapture->setHUDVisible(bHudVisible);
                }

                ImGui::Text("Capturing: %s", _renderDocCapture->isCapturing() ? "Yes" : "No");
                ImGui::Text("Delay Frames: %u", _renderDocCapture->getDelayFrames());
                ImGui::Combo("On Capture", &_renderDocOnCaptureAction, "None\0Open Replay UI\0Open Capture Folder\0");
                ImGui::TextWrapped("Last Capture: %s", _renderDocLastCapturePath.empty() ? "<none>" : _renderDocLastCapturePath.c_str());

                bool bCanCapture = _renderDocCapture->isCaptureEnabled();
                ImGui::BeginDisabled(!bCanCapture);
                if (ImGui::Button("Capture Next Frame (F9)")) {
                    _renderDocCapture->requestNextFrame();
                }
                if (ImGui::Button("Capture After 120 Frames (Ctrl+F9)")) {
                    _renderDocCapture->requestAfterFrames(120);
                }
                ImGui::EndDisabled();

                if (ImGui::Button("Open Last Capture Folder") && !_renderDocLastCapturePath.empty()) {
                    openDirectoryInOS(_renderDocLastCapturePath);
                }
            }
            ImGui::TreePop();
        }

        std::string clickedPoints;
        for (const auto& p : clicked) {
            clickedPoints += std::format("({}, {}) ", (int)p.x, (int)p.y);
        }
        ImGui::Text("Clicked Points: %s", clickedPoints.c_str());

        if (ImGui::Button("Deserialize Scene"))
        {
            auto sceneManager = ya::App::get()->getSceneManager();
            YA_CORE_ASSERT(sceneManager, "SceneManager is null");
            sceneManager->serializeToFile("Example/HelloMaterial/Content/Scenes/HelloMaterial.scene.json",
                                          getSceneManager()->getActiveScene());
        }
    }

    imcEditorCamera(camera);
    imcClearValues();
    imcFpsControl(*FPSControl::get());

    static bool bDarkMode = true;
    if (ImGui::Checkbox("Dark Mode", &bDarkMode)) {
        if (bDarkMode) {
            ImGui::StyleColorsDark();
        }
        else {
            ImGui::StyleColorsLight();
        }
    }

    ImGui::End();
}


// MARK: Scene
bool App::loadScene(const std::string& path)
{
    switch (_appState) {
    case AppState::Runtime:
    case AppState::Simulation:
        stopRuntime();
        break;
    case AppState::Editor:
        break;
    }

    if (_sceneManager) {
        return _sceneManager->loadScene(path);
    }
    return false;
}

bool App::unloadScene()
{
    if (_sceneManager) {
        return _sceneManager->unloadScene();
    }
    return false;
}

void App::onSceneInit(Scene* scene)
{

    // Create camera entity
}

void App::onSceneDestroy(Scene* scene)
{
    // No longer need to clear runtime camera reference
    // as we query it from ECS each frame
}

void App::onSceneActivated(Scene* scene)
{
    _editorLayer->setSceneContext(scene);

    // Engine core initialization - basic scene setup
    // Application-specific logic should be in derived classes (e.g., HelloMaterial)
}

// State transition hooks
void App::onEnterRuntime()
{
}

void App::startRuntime()
{
    if (_appState != AppState::Editor) {
        YA_CORE_WARN("Cannot start runtime: not in editor mode");
        return;
    }

    YA_CORE_INFO("Starting runtime...");
    _sceneManager->onStartRuntime();
    _appState = AppState::Runtime;


    onEnterRuntime();
}

void App::startSimulation()
{
    if (_appState != AppState::Editor) {
        YA_CORE_WARN("Cannot start simulation: not in editor mode");
        return;
    }

    YA_CORE_INFO("Starting simulation...");
    _appState = AppState::Simulation;

    onEnterSimulation();
}

void App::stopRuntime()
{
    if (_appState != AppState::Runtime) {
        YA_CORE_WARN("Cannot stop: not in runtime mode");
        return;
    }

    YA_CORE_INFO("Stopping runtime");
    _appState = AppState::Editor;
    _sceneManager->onStopRuntime();
    _luaScriptingSystem->onStop();
}

void App::stopSimulation()
{
    if (_appState != AppState::Simulation) {
        YA_CORE_WARN("Cannot stop: not in simulation mode");
        return;
    }

    YA_CORE_INFO("Stopping simulation");
    _appState = AppState::Editor;

    onExitSimulation();
}

Entity* App::getPrimaryCamera() const
{
    if (!_sceneManager) {
        return nullptr;
    }

    Scene* scene = _sceneManager->getActiveScene();
    if (!scene || !scene->isValid()) {
        return nullptr;
    }

    auto& registry = scene->getRegistry();

    // 1: Find first camera with PlayerComponent (fallback)
    for (const auto& [entity, cameraComp, playerComp] :
         registry.view<CameraComponent, PlayerComponent>().each()) {
        return scene->getEntityByEnttID(entity);
    }

    // 2: Find camera with _primary == true
    for (const auto& [entity, cameraComp] :
         registry.view<CameraComponent>().each()) {
        if (cameraComp.bPrimary) {
            return scene->getEntityByEnttID(entity);
        }
    }

    // No primary camera found
    return nullptr;
}



} // namespace ya
