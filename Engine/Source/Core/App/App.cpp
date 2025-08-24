#include "App.h"
#include "Core/EditorCamera.h"
#include "Core/FName.h"
#include "Core/FileSystem/FileSystem.h"

#include "Math/Geometry.h"
#include "Platform/Render/Vulkan/VulkanBuffer.h"
#include "Platform/Render/Vulkan/VulkanDescriptorSet.h"
#include "Platform/Render/Vulkan/VulkanFrameBuffer.h"
#include "Platform/Render/Vulkan/VulkanImage.h"
#include "Platform/Render/Vulkan/VulkanPipeline.h"
#include "Platform/Render/Vulkan/VulkanRender.h"

#include "Core/AssetManager.h"


#include "Render/Core/RenderTarget.h"
#include "Render/Core/Texture.h"
#include "Render/Render.h"
#include "render/Mesh.h"


#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>

#include "glm/glm.hpp"



namespace ya
{

// Define the static member variable
App *App::_instance = nullptr;


VulkanRenderPass *renderpass;
// std::vector<VulkanFrameBuffer> frameBuffers;
IRenderTarget *renderTarget = nullptr;

std::vector<VkCommandBuffer> commandBuffers;

VulkanPipelineLayout                      *defaultPipelineLayout = nullptr;
std::shared_ptr<VulkanDescriptorPool>      descriptorPool;
std::shared_ptr<VulkanDescriptorSetLayout> descriptorSetLayout0;
std::vector<VkDescriptorSet>               descriptorSets;

std::shared_ptr<VulkanBuffer> uGBuffer;
struct UboGBuffer
{
    // should be aligned by vec4
    glm::mat4 proj;
    glm::mat4 view;
    glm::vec4 dirLight;
} gBuffer;

std::shared_ptr<VulkanBuffer> uInstanceBuffer;
struct UboInstanceBuffer
{
    glm::mat4 model;
} instanceBuffer;

VulkanPipeline *pipeline = nullptr;


// 每帧需要独立的同步对象
std::vector<VkSemaphore> imageAvailableSemaphores;  // 每帧的图像可用信号量
std::vector<VkSemaphore> submittedSignalSemaphores; // 每张swapchain image渲染完成信号量
std::vector<VkFence>     frameFences;               // 每帧的CPU-GPU同步栅栏


// std::vector<std::shared_ptr<VulkanImage>> depthImages;
const auto DEPTH_FORMAT = EFormat::D32_SFLOAT_S8_UINT;

// 手动设置多层cpu缓冲，让 cpu 在多个帧之间轮转, cpu and gpu can work in parallel
// but frame count should be limited and considered  with performance
static uint32_t currentFrameIdx = 0;
int             fps             = 60;

uint32_t frameCount = 2; // CPU queue can submit 2 frames at most


std::shared_ptr<ya::Mesh> cubeMesh;
bool                      bRotating = true;

const char *faceTexturePath = "Engine/Content/TestTextures/face.png";
const char *uv1TexturePath  = "Engine/Content/TestTextures/uv1.png";


EditorCamera   camera;
vk::ImguiState imgui;

VkClearValue colorClearValue = {
    .color = {
        .float32 = {
            0,
            0,
            0,
            1,
        },
    },
};
VkClearValue depthClearValue = {
    .depthStencil = {
        .depth   = 1,
        .stencil = 0},
};

#pragma region misc

struct FPSControl
{
    float fps     = 0.0f;
    bool  bEnable = false;

    static constexpr float defaultFps = 60.f;

    float fpsLimit = defaultFps;
    float wantedDT = 1.f / defaultFps;

    float update(float &dt)
    {
        if (!bEnable) {
            return 0;
        }

        if (dt < wantedDT)
        {
            float delayTimeSec = wantedDT - dt;
            // YA_CORE_INFO("FPS limit exceeded. Delaying for {} ms", delayTime);
            SDL_Delay(static_cast<Uint32>(delayTimeSec * 1000));
            return delayTimeSec;
        }

        return 0;
    }

    void setFPSLimit(float limit)
    {
        fpsLimit = limit;
        wantedDT = 1.f / fpsLimit;
    }
};

static FPSControl fpsCtrl;

void imcFpsControl(FPSControl &fpsCtrl)
{
    if (ImGui::CollapsingHeader("FPS Control", ImGuiTreeNodeFlags_DefaultOpen)) {

        ImGui::Text("FPS Limit: %.1f", fpsCtrl.fpsLimit);

        static float newFpsLimit = fpsCtrl.fpsLimit;
        ImGui::PushItemWidth(100.0f);
        ImGui::InputFloat("New: ", &newFpsLimit, 10.0f, 10.0f, "%.1f");
        ImGui::PopItemWidth();

        ImGui::SameLine();
        if (ImGui::Button("Confirm")) {
            fpsCtrl.setFPSLimit(newFpsLimit);
            fps = (int)newFpsLimit;
        }

        ImGui::Checkbox("Enable FPS Control", &fpsCtrl.bEnable);
    }
}

bool imcEditorCamera(EditorCamera &camera)
{
    auto position = camera.position;
    auto rotation = camera.rotation;
    bool bChanged = false;

    // Add camera control settings to UI
    if (ImGui::CollapsingHeader("Camera Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::DragFloat3("Camera Position", glm::value_ptr(position), 0.01f, -100.0f, 100.0f)) {
            bChanged = true;
        }
        if (ImGui::DragFloat3("Camera Rotation", glm::value_ptr(rotation), 1.f, -180.0f, 180.0f)) {
            bChanged = true;
        }
        ImGui::DragFloat("Move Speed", &camera.moveSpeed, 0.1f, 0.1f, 20.0f);
        ImGui::DragFloat("Rotation Speed", &camera.rotationSpeed, 0.01f, 0.01f, 1.0f);
        ImGui::Text("Hold right mouse button to rotate camera");
        ImGui::Text("WASD: Move horizontally, QE: Move vertically");
    }

    if (bChanged) {
        camera.setPositionAndRotation(position, rotation);
    }
    return bChanged;
}

void imcClearValues()
{
    if (ImGui::CollapsingHeader("Clear Values", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit4("Color Clear Value", colorClearValue.color.float32);
        ImGui::DragFloat("Depth Clear Value", &depthClearValue.depthStencil.depth, 0.01f, 0.0f, 1.0f);
    }
}

#pragma endregion

void App::init(AppCreateInfo ci)
{
    _ci = ci;
    YA_CORE_ASSERT(_instance == nullptr, "Only one instance of App is allowed");
    _instance = this;

    YA_PROFILE_FUNCTION();
    Logger::init();
    FileSystem::init();
    NameRegistry::init(); // Initialize FName registry
    AssetManager::init();

    onInit(ci);

    currentRenderAPI          = ERenderAPI::Vulkan;
    std::string currentShader = "Test/HelloTexture.glsl";

    auto shaderProcessor = ShaderProcessorFactory()
                               .withProcessorType(ShaderProcessorFactory::EProcessorType::GLSL)
                               .withShaderStoragePath("Engine/Shader/GLSL")
                               .withCachedStoragePath("Engine/Intermediate/Shader/GLSL")
                               .FactoryNew<GLSLProcessor>();

    _shaderStorage = std::make_shared<ShaderStorage>(shaderProcessor);
    _shaderStorage->load(currentShader);


    RenderCreateInfo renderCI{
        .renderAPI   = currentRenderAPI,
        .swapchainCI = SwapchainCreateInfo{
            .imageFormat   = EFormat::R8G8B8A8_UNORM,
            .presentMode   = EPresentMode::FIFO, // vsync
            .bVsync        = true,
            .minImageCount = 3,
            .width         = static_cast<uint32_t>(_ci.width),
            .height        = static_cast<uint32_t>(_ci.height),
        },
    };
    _render        = IRender::create(renderCI);
    auto *vkRender = dynamic_cast<VulkanRender *>(_render);


    /**
      In vulkan:
      1. Create instance
      2. Create surface
      3. Find physical device
      4. Create logical device
      5. Create swap chain
      6. Other device resources: command pool{command buffers}, fences, semaphores, etc.
     */
    vkRender->init(renderCI);



    // WHY: my ARC 730M must need to recreate with FIFO again to enable vsync
    vkRender->getSwapChain()->setVsync(renderCI.swapchainCI.bVsync);

    constexpr auto _sampleCount = ESampleCount::Sample_1; // TODO: support MSAA
    // MARK: RenderPass

    /**
      In Vulkan:
        Create render pass and subpass
        define all attachments,
        input/color/depth/resolved attachment ref from all attachments
        and each subpasses dependencies (source -> next)
     */
    renderpass = new VulkanRenderPass(vkRender);
    renderpass->recreate(RenderPassCreateInfo{
        .attachments = {
            // color to present
            AttachmentDescription{
                .index          = 0,
                .format         = EFormat::R8G8B8A8_UNORM, // TODO: detect by device
                .samples        = ESampleCount::Sample_1,  // first present attachment cannot be multi-sampled
                .loadOp         = EAttachmentLoadOp::Clear,
                .storeOp        = EAttachmentStoreOp::Store,
                .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                .stencilStoreOp = EAttachmentStoreOp::DontCare,
                .initialLayout  = EImageLayout::Undefined,
                .finalLayout    = EImageLayout::PresentSrcKHR,
                .usage          = EImageUsage::ColorAttachment,
            },
            // depth attachment
            AttachmentDescription{
                .index          = 1,
                .format         = DEPTH_FORMAT,
                .samples        = _sampleCount,
                .loadOp         = EAttachmentLoadOp::Clear,
                .storeOp        = EAttachmentStoreOp::Store,
                .stencilLoadOp  = EAttachmentLoadOp::DontCare,
                .stencilStoreOp = EAttachmentStoreOp::DontCare,
                .initialLayout  = EImageLayout::Undefined,
                .finalLayout    = EImageLayout::DepthStencilAttachmentOptimal,
                .usage          = EImageUsage::DepthStencilAttachment,
            },
        },
        .subpasses = {
            RenderPassCreateInfo::SubpassInfo{
                .subpassIndex     = 0,
                .inputAttachments = {},
                .colorAttachments = {
                    RenderPassCreateInfo::AttachmentRef{
                        .ref    = 0, // color attachment
                        .layout = EImageLayout::ColorAttachmentOptimal,
                    },
                },
                .depthAttachment = RenderPassCreateInfo::AttachmentRef{
                    .ref    = 1, // depth attachment
                    .layout = EImageLayout::DepthStencilAttachmentOptimal,
                },
                .resolveAttachment = {},
            },
        },
        .dependencies = {
            RenderPassCreateInfo::SubpassDependency{
                .bSrcExternal = true,
                .srcSubpass   = 0,
                .dstSubpass   = 0,
            },
        },
    });

    VulkanSwapChain *vkSwapChain = vkRender->getSwapChain();

    const std::vector<VkImage> &images = vkSwapChain->getImages();
    swapchainImageSize                 = static_cast<int>(images.size());

    vkRender->allocateCommandBuffers(swapchainImageSize, commandBuffers);

    // use the RT instead of framebuffers directly
    renderTarget = new IRenderTarget(renderpass);

#pragma region Layout

    /**
     In Vulkan:
        define pipeline layout
        the layout(set=?, binding=?) for each descriptor set:
        - uniform buffer(camera, light)
        - push constants?
        - texture samplers?
    */
    PipelineLayout pipelineLayout{
        .pushConstants = {
            PushConstant{
                .offset     = 0,
                .size       = sizeof(char) * 256, // dynamical allocated buffer
                .stageFlags = EShaderStage::Fragment,
            },
        },
        .descriptorSetLayouts = {
            DescriptorSetLayout{
                .set      = 0,
                .bindings = {
                    // uGBuffer
                    DescriptorSetLayoutBinding{
                        .binding         = 0,
                        .descriptorType  = EPipelineDescriptorType::UniformBuffer,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Vertex,
                    },
                    // uInstanceBuffer
                    DescriptorSetLayoutBinding{
                        .binding         = 1,
                        .descriptorType  = EPipelineDescriptorType::UniformBuffer,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Vertex,
                    },
                    // uTexture0
                    DescriptorSetLayoutBinding{
                        .binding         = 2,
                        .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Fragment,
                    },
                    // uTexture1
                    DescriptorSetLayoutBinding{
                        .binding         = 3,
                        .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                        .descriptorCount = 1,
                        .stageFlags      = EShaderStage::Fragment,
                    },
                    // uTextures
                    DescriptorSetLayoutBinding{
                        .binding         = 4,
                        .descriptorType  = EPipelineDescriptorType::CombinedImageSampler,
                        .descriptorCount = 16,
                        .stageFlags      = EShaderStage::Fragment,
                    },
                },
            },
        },
    };

    std::vector<ya::DescriptorPoolSize> poolSizes = {
        ya::DescriptorPoolSize{
            .type            = EPipelineDescriptorType::UniformBuffer,
            .descriptorCount = 2 + 6, // uGBuffer + uInstanceBuffer
        },
        ya::DescriptorPoolSize{
            .type            = EPipelineDescriptorType::CombinedImageSampler,
            .descriptorCount = 1 + 1 + 16, // uTexture0 + uTexture1 + uTextures
        },
    };

    std::vector<VkDescriptorPoolSize> vkPoolSizes;
    for (const auto &poolSize : poolSizes) {
        vkPoolSizes.push_back(VkDescriptorPoolSize{
            .type            = toVk(poolSize.type),
            .descriptorCount = poolSize.descriptorCount,
        });
    }

    descriptorPool       = std::make_shared<VulkanDescriptorPool>(vkRender, 1, vkPoolSizes);
    descriptorSetLayout0 = std::make_shared<VulkanDescriptorSetLayout>(vkRender, pipelineLayout.descriptorSetLayouts[0]);
    descriptorPool->allocateDescriptorSets({descriptorSetLayout0}, descriptorSets);

    defaultPipelineLayout = new VulkanPipelineLayout(vkRender);
    defaultPipelineLayout->create(pipelineLayout.pushConstants,
                                  {
                                      descriptorSetLayout0->getHandle(),
                                  }); // we create the descriptor set layout outside
    vkRender->setDebugObjectName(VK_OBJECT_TYPE_PIPELINE_LAYOUT, defaultPipelineLayout->getHandle(), "DefaultPipelineLayout");

#pragma endregion


    // MARK: Pipeline

    /**
      In Vulkan:
        define the pipelines
        1. Shader Part
            -  shader programs from vert/frag/geom/comp shaders source codes
            -  the vertex layout define/from reflection, like glVertexAttribPointer()
        2. reference to the pipeline layout(seems one layout can be compatible with different pipelines)
        3. reference to the subpass
        4. other states initial settings (unlike OpenGL, all state are immutable in Vulkan by default):
            -  rasterization state
            -  multisample state
            -  depth/stencil state
            -  color blend state
            -  viewport state
    */
    pipeline = new VulkanPipeline(vkRender, renderpass, defaultPipelineLayout);

    pipeline->recreate(GraphicsPipelineCreateInfo{
        // .pipelineLayout   = pipelineLayout,
        .shaderCreateInfo = ShaderCreateInfo{
            .shaderName        = currentShader,
            .bDeriveFromShader = false,
            .vertexBufferDescs = {
                VertexBufferDescription{
                    .slot  = 0,
                    .pitch = sizeof(ya::Vertex),
                },
            },
            .vertexAttributes = {
                // (location=0) in vec3 aPos,
                VertexAttribute{
                    .bufferSlot = 0,
                    .location   = 0,
                    .format     = EVertexAttributeFormat::Float3,
                    .offset     = offsetof(ya::Vertex, position),
                },
                //  texcoord
                VertexAttribute{
                    .bufferSlot = 0, // same buffer slot
                    .location   = 1,
                    .format     = EVertexAttributeFormat::Float2,
                    .offset     = offsetof(ya::Vertex, texCoord0),
                },
                // normal
                VertexAttribute{
                    .bufferSlot = 0, // same buffer slot
                    .location   = 2,
                    .format     = EVertexAttributeFormat::Float4,
                    .offset     = offsetof(ya::Vertex, normal),
                },
            },
        },
        .subPassRef = 0,

        // define what state need to dynamically modified in render pass execution
        .dynamicFeatures = EPipelineDynamicFeature::Scissor | // the imgui required this feature as I did not set the dynamical render feature
                           EPipelineDynamicFeature::Viewport,
        .primitiveType      = EPrimitiveType::TriangleList,
        .rasterizationState = RasterizationState{
            .polygonMode = EPolygonMode::Fill,
            .frontFace   = EFrontFaceType::CounterClockWise, // GL
        },
        .multisampleState = MultisampleState{
            .sampleCount          = _sampleCount,
            .bSampleShadingEnable = false,
        },
        .depthStencilState = DepthStencilState{
            .bDepthTestEnable       = true,
            .bDepthWriteEnable      = true,
            .depthCompareOp         = ECompareOp::Less,
            .bDepthBoundsTestEnable = false,
            .bStencilTestEnable     = false,
            .minDepthBounds         = 0.0f,
            .maxDepthBounds         = 1.0f,
        },
        .colorBlendState = ColorBlendState{
            .attachments = {
                ColorBlendAttachmentState{
                    .index               = 0,
                    .bBlendEnable        = false,
                    .srcColorBlendFactor = EBlendFactor::SrcAlpha,
                    .dstColorBlendFactor = EBlendFactor::OneMinusSrcAlpha,
                    .colorBlendOp        = EBlendOp::Add,
                    .srcAlphaBlendFactor = EBlendFactor::One,
                    .dstAlphaBlendFactor = EBlendFactor::Zero,
                    .alphaBlendOp        = EBlendOp::Add,
                    .colorWriteMask      = EColorComponent::R | EColorComponent::G | EColorComponent::B | EColorComponent::A,
                },
            },
        },
        .viewportState = ViewportState{
            .viewports = {
                {
                    .x        = 0,
                    .y        = 0,
                    .width    = static_cast<float>(vkRender->getSwapChain()->getWidth()),
                    .height   = static_cast<float>(vkRender->getSwapChain()->getHeight()),
                    .minDepth = 0.0f,
                    .maxDepth = 1.0f,
                },
            },
            .scissors = {Scissor{
                .offsetX = 0,
                .offsetY = 0,
                .width   = static_cast<uint32_t>(vkRender->getSwapChain()->getWidth()),
                .height  = static_cast<uint32_t>(vkRender->getSwapChain()->getHeight()),
            }},
        },
    });



    _firstGraphicsQueue = &vkRender->getGraphicsQueues()[0];
    _firstPresentQueue  = &vkRender->getPresentQueues()[0];


    // MARK: Resources
    initSemaphoreAndFence();

    std::vector<ya::Vertex> vertices;
    std::vector<uint32_t>   indices;
    ya::GeometryUtils::makeCube(
        -0.5,
        0.5,
        -0.5,
        0.5,
        -0.5,
        0.5,
        vertices,
        indices,
        true,
        true);

    cubeMesh = std::make_shared<ya::Mesh>(vertices, indices, "cube");

    uGBuffer = VulkanBuffer::create(
        vkRender,
        BufferCreateInfo{
            .usage         = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            .size          = sizeof(UboGBuffer),
            .memProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            .debugName     = "uGBuffer",
        });
    uInstanceBuffer = VulkanBuffer::create(
        vkRender,
        BufferCreateInfo{
            .usage         = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            .data          = std::nullopt,
            .size          = sizeof(UboGBuffer),
            .memProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            .debugName     = "uGBufferStaging",
        });
    AssetManager::get()->loadTexture(faceTexturePath);
    AssetManager::get()->loadTexture(uv1TexturePath);

    VkSampler dummySampler = VK_NULL_HANDLE;
    vkRender->createSampler("DefaultSampler",
                            SamplerCreateInfo{
                                .minFilter     = EFilter::Linear,
                                .magFilter     = EFilter::Linear,
                                .mipmapMode    = ESamplerMipmapMode::Linear,
                                .addressModeU  = ESamplerAddressMode::Repeat,
                                .addressModeV  = ESamplerAddressMode::Repeat,
                                .addressModeW  = ESamplerAddressMode::Repeat,
                                .mipLodBias    = 0.0f,
                                .maxAnisotropy = 1.0f,
                            },
                            dummySampler);

    // we span at z = 3 and look at the origin ( right hand? and the cubes all place on xy plane )
    camera.setPosition(glm::vec3(0.0f, 0.0f, 3.0f));
    camera.setRotation(glm::vec3(0.0f, 0.0f, 0.0f));
    camera.setPerspective(45.0f, 16.0f / 9.0f, 0.1f, 100.0f);



#pragma region ImGui Init
    // imgui init
    auto                      queue = _firstGraphicsQueue;
    ImGui_ImplVulkan_InitInfo initInfo{
        .ApiVersion     = vkRender->getApiVersion(),
        .Instance       = vkRender->getInstance(),
        .PhysicalDevice = vkRender->getPhysicalDevice(),
        .Device         = vkRender->getLogicalDevice(),
        .QueueFamily    = queue->getFamilyIndex(),
        .Queue          = queue->getHandle(),
        .DescriptorPool = nullptr,
        .RenderPass     = renderpass->getHandle(),
        .MinImageCount  = 2,
        .ImageCount     = static_cast<uint32_t>(vkRender->getSwapChain()->getImages().size()),
        .MSAASamples    = VK_SAMPLE_COUNT_1_BIT,

        // (Optional)
        .PipelineCache = vkRender->getPipelineCache(),
        .Subpass       = 0,

        // (Optional) Set to create internal descriptor pool instead of using DescriptorPool
        .DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE, // let imgui create it by itself internal

        // (Optional) Dynamic Rendering
        // Need to explicitly enable VK_KHR_dynamic_rendering extension to use this, even for Vulkan 1.3.
        .UseDynamicRendering = false,
#ifdef IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING
        .PipelineRenderingCreateInfo = VkPipelineRenderingCreateInfoKHR{},
#endif

        // (Optional) Allocation, Debugging
        .Allocator       = VK_NULL_HANDLE,
        .CheckVkResultFn = [](VkResult err) {
            if (err != VK_SUCCESS) {
                YA_CORE_ERROR("Vulkan error: {}", err);
            }
        },
        // Minimum allocation size. Set to 1024*1024 to satisfy zealous best practices validation layer and waste a little memory.
        .MinAllocationSize = static_cast<VkDeviceSize>(1024 * 1024),
    };
    imgui.init(vkRender->getNativeWindow<SDL_Window>(), initInfo);
#pragma endregion


    // wait something done
    vkDeviceWaitIdle(vkRender->getLogicalDevice());
}

// MARK: QUIT
void ya::App::quit()
{
    auto *vkRender = static_cast<VulkanRender *>(_render);
    vkDeviceWaitIdle(vkRender->getLogicalDevice());


    cubeMesh.reset();
    uGBuffer.reset();
    uInstanceBuffer.reset();

    imgui.shutdown();

    releaseSemaphoreAndFence();

    pipeline->cleanup();
    delete pipeline;

    defaultPipelineLayout->cleanup();
    delete defaultPipelineLayout;
    descriptorSetLayout0.reset();
    descriptorPool.reset();


    delete renderTarget;
    delete renderpass;

    AssetManager::get()->cleanup();
    _render->destroy();

    delete _render;
}



int ya::App::run()
{

    _startTime = std::chrono::steady_clock::now();
    _lastTime  = _startTime;

    while (bRunning) {

        time_point_t now        = clock_t::now();
        auto         dtMicroSec = std::chrono::duration_cast<std::chrono::microseconds>(now - _lastTime).count();
        float        dtSec      = (double)dtMicroSec / 1000000.0;
        dtSec                   = std::max(dtSec, 0.0001f);
        _lastTime               = now;

        if (auto result = iterate(dtSec); result != 0) {
            break;
        }
    }

    return 0;
}


int ya::App::onEvent(SDL_Event &event)
{
    EventProcessState ret = imgui.processEvents(event);
    if (ret != EventProcessState::Continue) {
        return 0;
    }
    inputManager.processEvent(event);


#pragma region Sdl Event

    switch (SDL_EventType(event.type))
    {
    case SDL_EVENT_FIRST:
        break;
    case SDL_EVENT_QUIT:
    {
        bRunning = false;
        return 1;
    } break;
    case SDL_EVENT_TERMINATING:
    case SDL_EVENT_LOW_MEMORY:
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
    case SDL_EVENT_DID_ENTER_BACKGROUND:
    case SDL_EVENT_WILL_ENTER_FOREGROUND:
    case SDL_EVENT_DID_ENTER_FOREGROUND:
    case SDL_EVENT_LOCALE_CHANGED:
    case SDL_EVENT_SYSTEM_THEME_CHANGED:
    case SDL_EVENT_DISPLAY_ORIENTATION:
    case SDL_EVENT_DISPLAY_ADDED:
    case SDL_EVENT_DISPLAY_REMOVED:
    case SDL_EVENT_DISPLAY_MOVED:
    case SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED:
    case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:
    case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED:
    case SDL_EVENT_WINDOW_SHOWN:
    case SDL_EVENT_WINDOW_HIDDEN:
    case SDL_EVENT_WINDOW_EXPOSED:
    case SDL_EVENT_WINDOW_MOVED:
        break;
    case SDL_EVENT_WINDOW_RESIZED:
    {
        // YA_CORE_INFO("window resized {}x{}",
        //              event.window.data1,
        //              event.window.data2);
        // auto vkRender = static_cast<VulkanRender *>(_render);
        // vkRender->recreateSwapChain();
        float aspectRatio = event.window.data2 > 0 ? static_cast<float>(event.window.data1) / static_cast<float>(event.window.data2) : 1.f;
        YA_CORE_DEBUG("Window resized to {}x{}, aspectRatio: {} ", event.window.data1, event.window.data2, aspectRatio);
        camera.setAspectRatio(aspectRatio);

    } break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_METAL_VIEW_RESIZED:
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_MAXIMIZED:
    case SDL_EVENT_WINDOW_RESTORED:
    case SDL_EVENT_WINDOW_MOUSE_ENTER:
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    {
        bRunning = false;
        return 1;
    } break;
    case SDL_EVENT_WINDOW_HIT_TEST:
    case SDL_EVENT_WINDOW_ICCPROF_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
    case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
    case SDL_EVENT_WINDOW_OCCLUDED:
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
    case SDL_EVENT_WINDOW_DESTROYED:
    case SDL_EVENT_WINDOW_HDR_STATE_CHANGED:
    case SDL_EVENT_KEY_DOWN:
        break;
    case SDL_EVENT_KEY_UP:
    {
        if (event.key.key == SDLK_ESCAPE) {
            bRunning = false;
            return 1;
        }
    } break;
    case SDL_EVENT_TEXT_EDITING:
    case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_KEYMAP_CHANGED:
    case SDL_EVENT_KEYBOARD_ADDED:
    case SDL_EVENT_KEYBOARD_REMOVED:
    case SDL_EVENT_TEXT_EDITING_CANDIDATES:
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_MOUSE_ADDED:
    case SDL_EVENT_MOUSE_REMOVED:
    case SDL_EVENT_JOYSTICK_AXIS_MOTION:
    case SDL_EVENT_JOYSTICK_BALL_MOTION:
    case SDL_EVENT_JOYSTICK_HAT_MOTION:
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
    case SDL_EVENT_JOYSTICK_BUTTON_UP:
    case SDL_EVENT_JOYSTICK_ADDED:
    case SDL_EVENT_JOYSTICK_REMOVED:
    case SDL_EVENT_JOYSTICK_BATTERY_UPDATED:
    case SDL_EVENT_JOYSTICK_UPDATE_COMPLETE:
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
    case SDL_EVENT_GAMEPAD_ADDED:
    case SDL_EVENT_GAMEPAD_REMOVED:
    case SDL_EVENT_GAMEPAD_REMAPPED:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_UP:
    case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
    case SDL_EVENT_GAMEPAD_UPDATE_COMPLETE:
    case SDL_EVENT_GAMEPAD_STEAM_HANDLE_UPDATED:
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_CANCELED:
    case SDL_EVENT_CLIPBOARD_UPDATE:
    case SDL_EVENT_DROP_FILE:
    case SDL_EVENT_DROP_TEXT:
    case SDL_EVENT_DROP_BEGIN:
    case SDL_EVENT_DROP_COMPLETE:
    case SDL_EVENT_DROP_POSITION:
    case SDL_EVENT_AUDIO_DEVICE_ADDED:
    case SDL_EVENT_AUDIO_DEVICE_REMOVED:
    case SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED:
    case SDL_EVENT_SENSOR_UPDATE:
    case SDL_EVENT_PEN_PROXIMITY_IN:
    case SDL_EVENT_PEN_PROXIMITY_OUT:
    case SDL_EVENT_PEN_DOWN:
    case SDL_EVENT_PEN_UP:
    case SDL_EVENT_PEN_BUTTON_DOWN:
    case SDL_EVENT_PEN_BUTTON_UP:
    case SDL_EVENT_PEN_MOTION:
    case SDL_EVENT_PEN_AXIS:
    case SDL_EVENT_CAMERA_DEVICE_ADDED:
    case SDL_EVENT_CAMERA_DEVICE_REMOVED:
    case SDL_EVENT_CAMERA_DEVICE_APPROVED:
    case SDL_EVENT_CAMERA_DEVICE_DENIED:
    case SDL_EVENT_RENDER_TARGETS_RESET:
    case SDL_EVENT_RENDER_DEVICE_RESET:
    case SDL_EVENT_RENDER_DEVICE_LOST:
    case SDL_EVENT_PRIVATE0:
    case SDL_EVENT_PRIVATE1:
    case SDL_EVENT_PRIVATE2:
    case SDL_EVENT_PRIVATE3:
    case SDL_EVENT_POLL_SENTINEL:
    case SDL_EVENT_USER:
    case SDL_EVENT_LAST:
    case SDL_EVENT_ENUM_PADDING:
        break;
    }
#pragma endregion


    return 0;
};


int ya::App::iterate(float dt)
{
    SDL_Event evt;
    SDL_PollEvent(&evt);

    if (auto result = onEvent(evt); result != 0) {
        return 1;
    }

    dt += fpsCtrl.update(dt);

    if (!_bPause) {
        onUpdate(dt);
    }
    onRender(dt);
    taskManager.update();
    ++_frameIndex;
    return 0;
}


void App::onUpdate(float dt)
{
    inputManager.update();

    static auto time = 0.f;
    time += dt;                       // dt is in milliseconds
    float speed = glm::radians(45.f); // 45 degrees per second
    float alpha = speed * time;       // Convert time from ms to seconds

    glm::quat rotX = glm::angleAxis(alpha, glm::vec3(1, 0, 0));
    glm::quat rotY = glm::angleAxis(alpha, glm::vec3(0, 1, 0));
    glm::quat rotZ = glm::angleAxis(alpha, glm::vec3(0, 0, 1));

    // xyz rotation
    glm::quat combinedRot = rotZ * rotY * rotX;

    // auto rotation by up-down and left-right
    glm::mat4 rot = glm::mat4_cast(combinedRot);


    camera.update(inputManager, dt); // Camera expects dt in seconds

    gBuffer.view     = camera.getViewMatrix();
    gBuffer.proj     = camera.getProjectionMatrix();
    gBuffer.dirLight = glm::normalize(glm::vec4(-1.0f, -3.0f, -1.0f, 1.f));
    if (bRotating) {
        instanceBuffer.model = rot;
    }
}

void App::onRender(float dt)
{
    onDraw(dt);
}

void App::onDraw(float dt)
{
    auto vkRender = static_cast<VulkanRender *>(_render);
    // vkDeviceWaitIdle(vkRender->getLogicalDevice());
    // SDL_Delay(1000 / 30); // Simulate frame time, remove in production


    // ✅ Flight Frames关键步骤1: CPU等待当前帧对应的fence
    // 这确保CPU不会在GPU还在使用资源时(present)就开始修改它们
    // 例如：如果MAX_FRAMES_IN_FLIGHT=2，当渲染第3帧时，等待第1帧完成
    VK_CALL(vkWaitForFences(vkRender->getLogicalDevice(),
                            1,
                            &frameFences[currentFrameIdx],
                            VK_TRUE,
                            UINT64_MAX));

    // 重置fence为未信号状态，准备给GPU在本帧结束时发送信号
    VK_CALL(vkResetFences(vkRender->getLogicalDevice(), 1, &frameFences[currentFrameIdx]));

    auto swapchain = vkRender->getSwapChain();

    // get imageIndex
    uint32_t imageIndex = -1;
    {
        VkResult ret = swapchain->acquireNextImage(
            imageAvailableSemaphores[currentFrameIdx], // 当前帧的图像可用信号量
            frameFences[currentFrameIdx],              // 等待上一present完成
            imageIndex);

        // Do a sync recreation here, Can it be async?(just return and register a frame task)
        if (ret == VK_ERROR_OUT_OF_DATE_KHR) {
            vkDeviceWaitIdle(vkRender->getLogicalDevice());

            // current ignore the size in ci
            VkExtent2D originalExtent = swapchain->getExtent();
            bool       ok             = swapchain->recreate(vkRender->getSwapChain()->getCreateInfo());

            if (ok) {
                if (swapchain->getWidth() != originalExtent.width || swapchain->getHeight() != originalExtent.height) {
                    // recreate renderpass and pipeline
                    // rebind frame buffer
                    renderTarget->setExtent(swapchain->getExtent());
                }
                if (imageIndex == -1) {
                    ret = vkRender->getSwapChain()->acquireNextImage(
                        imageAvailableSemaphores[currentFrameIdx],
                        frameFences[currentFrameIdx],
                        imageIndex);
                    if (ret != VK_SUCCESS && ret != VK_SUBOPTIMAL_KHR) {
                        YA_CORE_ERROR("Failed to acquire next image: {}", ret);
                    }
                }
            }
            else {
                YA_CORE_ERROR("Failed to recreate swapchain, exiting application.");
                bRunning = false; // Exit the application if swapchain recreation fails
                return;
            }
        }
        YA_CORE_ASSERT(imageIndex >= 0 && imageIndex < swapchainImageSize,
                       "Invalid image index: {}. Swapchain image size: {}",
                       imageIndex,
                       swapchainImageSize);
    }


    // 2. begin command buffer
    VkCommandBuffer curCmdBuf = commandBuffers[imageIndex];
    vkResetCommandBuffer(curCmdBuf, 0); // Reset command buffer before recording
    begin(curCmdBuf);

#pragma region Update uniform
#if 1

    uGBuffer->writeData(&gBuffer, sizeof(UboGBuffer), 0);
    uInstanceBuffer->writeData(&instanceBuffer, sizeof(UboInstanceBuffer), 0);


    // Add memory barrier to ensure copy completes before shader reads
    // std::vector<VkBufferMemoryBarrier> bmBarriers{
    //     VkBufferMemoryBarrier{
    //         .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
    //         .pNext               = nullptr,
    //         .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
    //         .dstAccessMask       = VK_ACCESS_UNIFORM_READ_BIT,
    //         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    //         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
    //         .buffer              = uGBuffer->getHandle(),
    //         .offset              = 0,
    //         .size                = sizeof(UboGBuffer),
    //     },
    // };


    auto set = descriptorSets[0];

    VkDescriptorBufferInfo gBuffer{
        .buffer = uGBuffer->getHandle(), // 🤣 this is the buffer contains the data
        .offset = 0,
        .range  = sizeof(UboGBuffer), // size of the uniform buffer
    };
    VkDescriptorBufferInfo instanceBuffer{
        .buffer = uInstanceBuffer->getHandle(), // 🤣 this is the buffer contains the data
        .offset = 0,
        .range  = sizeof(UboInstanceBuffer), // size of the uniform buffer
    };

    VkDescriptorImageInfo texture0{
        .sampler     = vkRender->getSampler("DefaultSampler"),
        .imageView   = AssetManager::get()->getTexture(faceTexturePath)->getVkImageView(),
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkDescriptorImageInfo texture1{
        .sampler     = vkRender->getSampler("DefaultSampler"),
        .imageView   = AssetManager::get()->getTexture(uv1TexturePath)->getVkImageView(),
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };


    std::vector<VkWriteDescriptorSet> writeDSs{
        VkWriteDescriptorSet{
            .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext            = nullptr,
            .dstSet           = set, // the first descriptor set
            .dstBinding       = 0,   // see gplCI[0].bindings[0].binding
            .dstArrayElement  = 0,   // the index of array
            .descriptorCount  = 1,   // see gplCI[0].bindings[0].descriptorCount
            .descriptorType   = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pImageInfo       = nullptr,
            .pBufferInfo      = &gBuffer,
            .pTexelBufferView = nullptr,
        },

        VkWriteDescriptorSet{
            .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext            = nullptr,
            .dstSet           = set, // the first descriptor set
            .dstBinding       = 1,   // see gplCI[0].bindings[0].binding
            .dstArrayElement  = 0,   // the index of array
            .descriptorCount  = 1,   // see gplCI[0].bindings[0].descriptorCount
            .descriptorType   = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pImageInfo       = nullptr,
            .pBufferInfo      = &instanceBuffer,
            .pTexelBufferView = nullptr,
        },
        VkWriteDescriptorSet{
            .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext            = nullptr,
            .dstSet           = set, // the first descriptor set
            .dstBinding       = 2,   // see gplCI[0].bindings[0].binding
            .dstArrayElement  = 0,   // the index of array
            .descriptorCount  = 1,   // see gplCI[0].bindings[0].descriptorCount
            .descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo       = &texture0,
            .pBufferInfo      = nullptr,
            .pTexelBufferView = nullptr,
        },
        VkWriteDescriptorSet{
            .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext            = nullptr,
            .dstSet           = set, // the first descriptor set
            .dstBinding       = 3,   // see gplCI[0].bindings[0].binding
            .dstArrayElement  = 0,   // the index of array
            .descriptorCount  = 1,   // see gplCI[0].bindings[0].descriptorCount
            .descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo       = &texture1,
            .pBufferInfo      = nullptr,
            .pTexelBufferView = nullptr,
        },
    };

    // 4. bind descriptor sets, as global concepts
    vkCmdBindDescriptorSets(curCmdBuf,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            defaultPipelineLayout->getHandle(),
                            0, // first set
                            descriptorSets.size(),
                            descriptorSets.data(),
                            // 😁 We can use {2,4,6} at the firstOffset{3} to make a new layoutSet  from old layoutSet{1,2,3...}
                            // This can avoid of creating a new descriptor set?
                            0,        // dynamic offset count
                            nullptr); // dynamic offsets



    // vkCmdPushDescriptorSet(curCmdBuf,
    //                        VK_PIPELINE_BIND_POINT_GRAPHICS,
    //                        defaultPipelineLayout->getHandle(),
    //                        0,
    //                        writeDSs.size(),
    //                        writeDSs.data());

    // TODO: make a copy pass like SDL
    vkUpdateDescriptorSets(vkRender->getLogicalDevice(),
                           writeDSs.size(),
                           writeDSs.data(),
                           0,
                           nullptr);

    // vkCmdPipelineBarrier(curCmdBuf,
    //                      VK_PIPELINE_STAGE_TRANSFER_BIT,
    //                      VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
    //                      0,
    //                      0,
    //                      nullptr,
    //                      bmBarriers.size(),
    //                      bmBarriers.data(),
    //                      0,
    //                      nullptr);

    // MARK: Push Constant
    //  push constants: dynamical stack push, high performance, reduce descriptor usage (to ubo)
    static struct PushConstantData
    {
        float textureMixAlpha = 0.5f;
    } pushData;

    vkCmdPushConstants(curCmdBuf,
                       defaultPipelineLayout->getHandle(),
                       VK_SHADER_STAGE_FRAGMENT_BIT,
                       0,
                       sizeof(PushConstantData),
                       &pushData);



#endif
#pragma endregion


#pragma region RenderPass

    // 3 begin render pass && bind frame buffer
    // TODO: subpasses?
    renderTarget->setColorClearValue(colorClearValue);
    renderTarget->setDepthStencilClearValue(depthClearValue);
    renderTarget->begin(curCmdBuf);


    VulkanFrameBuffer *curFrameBuffer = renderTarget->getFrameBuffer();



    // 5. bind pipeline
    pipeline->bind(curCmdBuf);



    // 6. setup vertex buffer, index buffer (2d payloads? upload?)
    // 7. other resources, uniform buffers, etc.

#pragma region Dynamic State
    // need set VkPipelineDynamicStateCreateInfo
    // or those properties should be modified in the pipeline recreation if needed.
    // but sometimes that we want to enable depth or color-blend state dynamically



    VkViewport viewport{
        .x        = 0,
        .y        = 0,
        .width    = static_cast<float>(curFrameBuffer->getWidth()),
        .height   = static_cast<float>(curFrameBuffer->getHeight()),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(curCmdBuf, 0, 1, &viewport);


    // Set scissor (required by imgui , and cause I must call this here)
    VkRect2D scissor{
        .offset = {0, 0},
        .extent = vkRender->getSwapChain()->getExtent(),
    };
    vkCmdSetScissor(curCmdBuf, 0, 1, &scissor);
#pragma endregion



    // Bind vertex buffer
    VkBuffer vertexBuffers[] = {cubeMesh->getVertexBuffer()->getHandle()};
    // current no need to support subbuffer
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(curCmdBuf, 0, 1, vertexBuffers, offsets);

    vkCmdBindIndexBuffer(curCmdBuf,
                         cubeMesh->getIndexBuffer()->getHandle(),
                         0,
                         VK_INDEX_TYPE_UINT32);

    // 8. draw triangle

    // vkCmdDraw(curCmdBuf, 3, 1, 0, 0);
    vkCmdDrawIndexed(curCmdBuf,
                     cubeMesh->getIndexCount(), // index count
                     9,                         // instance count: 9 cubes
                     0,                         // first index
                     0,                         // vertex offset, this for merge vertex buffer?
                     0                          // first instance
    );

#pragma region UI

    imgui.beginFrame();
    if (ImGui::Begin("Test")) {
        float fps = 1.0f / dt;
        ImGui::Text("DeltaTime: %.1f ms,\t FPS: %.1f", dt * 1000.0f, fps);
        ImGui::Text("Frame: %d, SwapchainImage: %d, FameIndex: %d", _frameIndex, currentFrameIdx, currentFrameIdx);
        static int count = 0;
        if (ImGui::Button(std::format("Click Me ({})", count).c_str())) {
            count++;
            YA_CORE_INFO("=====================================");
        }

        ImGui::DragFloat("Texture Mix Alpha", &pushData.textureMixAlpha, 0.01, 0, 1);
        ImGui::Checkbox("Is Rotating", &bRotating);

        bool bVsync = vkRender->getSwapChain()->bVsync;
        if (ImGui::Checkbox("VSync", &bVsync)) {
            // vkRender->getSwapChain()->recreate();
            // recreate renderpass and pipeline
            // renderpass->recreate();
            // pipeline->recreate();
            // rebind frame buffer
            // renderTarget->recreate();
            taskManager.registerFrameTask([vkRender, bVsync]() {
                // TODO :bind dirty link
                vkRender->getSwapChain()->setVsync(bVsync);
            });
        }

        imcEditorCamera(camera);
        imcClearValues();
        // imcFpsControl(fpsCtrl);
        ImGui::End();
    }
    imgui.render();
    // imgui.submit(curCmdBuf, pipeline->getHandle());
    imgui.submit(curCmdBuf); // leave nullptr to let imgui use its pipeline
    imgui.endFrame();

#pragma endregion

    // 9. end render pass
    // TODO: subpasses?
    // #if 1 // multiple renderpass
    //     // vkCmdNextSubpass2()
    //     // vkCmdNextSubpass()
    // #endif
    // renderpass->end(curCmdBuf);
    renderTarget->end(curCmdBuf);

#pragma endregion



    // 10. end command buffer
    end(curCmdBuf);

    // 11. submit command buffer
    // _firstGraphicsQueue.waitIdle();
    // auto curQue = currentFrame == 0 ? _firstGraphicsQueue : _firstPresentQueue;
    _firstGraphicsQueue->submit(
        {
            curCmdBuf,
        },
        //  wait image available to submit, after acquire image done
        {
            imageAvailableSemaphores[currentFrameIdx], // 当前帧的图像可用信号量
        },
        // send submitted signal after command buffer submitted completed
        {
            submittedSignalSemaphores[imageIndex], // ⚠️ the signal sema of the submit operation should be the IMAGE_INDEX!!!
        },
        frameFences[currentFrameIdx] // GPU完成所有(submit)工作后会发送此fence信号;
    );


    // submit and present are asynchronous operations,
    VkResult result = vkRender->getSwapChain()->presentImage(
        imageIndex,
        {
            submittedSignalSemaphores[imageIndex], // ⚠️ the wait sema of the Present operation  should be the IMAGE_INDEX!!!
        });

    // if not use gl, this should be eq?
    // YA_CORE_INFO("Presenting image index: {}, current frame index: {}", curFrameIdx, curFrameIdx);
    // NE_ASSERT(curFrameIdx == curImageIdx, "Image index mismatch! Expected {}, got {}", curFrameIdx, curImageIdx);

    // SUB: submit
    if (result == VK_SUBOPTIMAL_KHR) {
        // recreate swapchain
        VK_CALL(vkDeviceWaitIdle(vkRender->getLogicalDevice()));
        VkExtent2D originalExtent = swapchain->getExtent();

        bool ok = swapchain->recreate(vkRender->getSwapChain()->getCreateInfo());
        if (ok) {
            if (swapchain->getWidth() != originalExtent.width || swapchain->getHeight() != originalExtent.height) {
                // recreate renderpass and pipeline
                // rebind frame buffer
                renderTarget->setExtent(swapchain->getExtent());
            }
        }
        else {
            YA_CORE_ERROR("Failed to recreate swapchain after suboptimal!");
        }
    }

    // ✅ Flight Frames关键步骤5: 切换到下一个飞行帧
    // 使用模运算实现环形缓冲区，在多个帧之间循环
    // 例如：0 -> 1 -> 0 -> 1 ... (当submissionResourceSize=2时)
    // ⚠️ frameCount can not equal to imageSize of swapchain!!
    currentFrameIdx = (currentFrameIdx + 1) % frameCount;
} // namespace ya

#pragma region Sync Resources

void App::initSemaphoreAndFence()
{
    /* ✅ Flight Frames机制的同步对象初始化

       SDL与Vulkan没有像OpenGL那样的"swap buffers"概念（如SDL_GL_SwapWindow()）。
       在Vulkan中，呈现机制通过Vulkan的swapchain扩展来管理，
       因此我们使用semaphore和fence来同步图像获取和渲染完成。

       Flight Frames需要为每个并行帧创建独立的同步对象：
       - imageAvailableSemaphores: GPU-GPU同步，swapchain图像可用信号
       - submittedSignalSemaphores: GPU-GPU同步，渲染完成信号
       - frameFences: CPU-GPU同步，CPU等待GPU完成帧渲染
    */

    auto vkRender = static_cast<VulkanRender *>(_render);

    VkSemaphoreCreateInfo semaphoreInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0, // No special flags
    };

    VkResult ret = {};
    // NOTICE : 应该创建与swapchain图像数量相同的信号量数量
    // 以避免不同图像之间的资源竞争（如信号量) VUID-vkQueueSubmit-pSignalSemaphores-00067

    // ✅ 为每个飞行帧分配独立的同步对象数组
    // 这是Flight Frames的核心：避免不同帧之间的资源竞争
    imageAvailableSemaphores.resize(swapchainImageSize);
    submittedSignalSemaphores.resize(swapchainImageSize);
    frameFences.resize(swapchainImageSize);

    // ✅ 循环创建每个飞行帧的同步对象
    for (uint32_t i = 0; i < (uint32_t)swapchainImageSize; i++) {
        // 创建图像可用信号量：当swapchain图像准备好被渲染时发出信号
        ret = vkCreateSemaphore(vkRender->getLogicalDevice(), &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]);
        YA_CORE_ASSERT(ret == VK_SUCCESS, "Failed to create image available semaphore! Result: {}", ret);

        // 创建渲染完成信号量：当GPU完成渲染命令时发出信号
        ret = vkCreateSemaphore(vkRender->getLogicalDevice(), &semaphoreInfo, nullptr, &submittedSignalSemaphores[i]);
        YA_CORE_ASSERT(ret == VK_SUCCESS, "Failed to create render finished semaphore! Result: {}", ret);

        // 创建帧fence：用于CPU等待GPU完成整个帧的处理
        // ⚠️ 重要：初始状态设为已信号(SIGNALED)，这样第一帧不会被阻塞
        VkFenceCreateInfo fenceInfo{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT // 开始时就处于已信号状态
        };

        ret = vkCreateFence(vkRender->getLogicalDevice(), &fenceInfo, nullptr, &frameFences[i]);
        YA_CORE_ASSERT(ret == VK_SUCCESS, "failed to create fence!");

        vkRender->setDebugObjectName(VK_OBJECT_TYPE_SEMAPHORE,
                                     imageAvailableSemaphores[i],
                                     std::format("ImageAvailableSemaphore_{}", i).c_str());
        vkRender->setDebugObjectName(VK_OBJECT_TYPE_SEMAPHORE,
                                     submittedSignalSemaphores[i],
                                     std::format("SubmittedSignalSemaphore_{}", i).c_str());
        vkRender->setDebugObjectName(VK_OBJECT_TYPE_FENCE,
                                     frameFences[i],
                                     std::format("FrameFence_{}", i).c_str());
    }
}

void App::releaseSemaphoreAndFence()
{
    auto vkRender = static_cast<VulkanRender *>(_render);
    vkDeviceWaitIdle(vkRender->getLogicalDevice());
    for (uint32_t i = 0; i < imageAvailableSemaphores.size(); i++) {
        vkDestroySemaphore(vkRender->getLogicalDevice(), imageAvailableSemaphores[i], nullptr);
        vkDestroySemaphore(vkRender->getLogicalDevice(), submittedSignalSemaphores[i], nullptr);
        vkDestroyFence(vkRender->getLogicalDevice(), frameFences[i], nullptr);
    }
}



#pragma endregion


} // namespace ya
