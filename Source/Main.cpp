#define VOLK_IMPLEMENTATION
#include <volk.h>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <spdlog/spdlog.h>
#include <GLFW/glfw3.h>

#include <fstream>
#include <intrin.h>

// GLM Includes
// ---------------------------------------------------------

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/gtc/matrix_transform.hpp>

// OpenUSD Includes
// ---------------------------------------------------------

#include <pxr/pxr.h>
#include <pxr/base/tf/errorMark.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/tf/staticTokens.h>

// Hydra Core
#include <pxr/imaging/hd/engine.h>
#include <pxr/imaging/hd/rendererPlugin.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/resourceRegistry.h>
#include <pxr/imaging/hd/renderPass.h>
#include <pxr/imaging/hd/renderPassState.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hd/task.h>
#include <pxr/imaging/hd/mesh.h>

// HDX (Hydra Utilities)
#include <pxr/imaging/hdx/renderTask.h>
#include <pxr/imaging/hdx/taskController.h>

// USD Hydra Scene Delegate Implementation.
#include <pxr/usdImaging/usdImaging/delegate.h>

PXR_NAMESPACE_USING_DIRECTIVE

// Constants
// ---------------------------------------------------------

const uint32_t kWindowWidth       = 1920u;
const uint32_t kWindowHeight      = 1080u;
const uint32_t kMaxFramesInFlight = 3u;

// Logging + crash utility when an assertion fails.
// ---------------------------------------------------------

#ifdef _DEBUG
    void Check(VkResult a, const char* b) { if (a != VK_SUCCESS) { spdlog::critical(b); __debugbreak(); exit(a); } }
    void Check(bool     a, const char* b) { if (a != true)       { spdlog::critical(b); __debugbreak(); exit(1); } }
#else
    void Check(VkResult a, const char* b) { if (a != VK_SUCCESS) { spdlog::critical(b); exit(a); } }
    void Check(bool     a, const char* b) { if (a != true)       { spdlog::critical(b); exit(1); } }
#endif

// Index of shaders used by this application.
// ---------------------------------------------------------

enum ShaderID
{
    FullscreenTriangleVert,
    LitFrag,
    MeshVert
};

// Common parameters pushed to all shaders.
// ---------------------------------------------------------

struct PushConstants
{
    glm::mat4 _MatrixVP;
    glm::mat4 _MatrixM;
    float     _Time;
};

// Layout of the standard Vertex for this application.
// ---------------------------------------------------------

struct Vertex
{
    glm::vec3 positionOS;
    glm::vec3 normalOS;
    glm::vec2 texCoord0;
};

// Collection of vulkan primitives to hold the current frame state. 
// ---------------------------------------------------------

struct FrameParams
{
    VkCommandBuffer cmd;
    VkImage         backBuffer;
    VkImageView     backBufferView;
    double          deltaTime;
};

// Collection of vulkan primitives to hold an image.
// ---------------------------------------------------------

struct Image
{
    VkImage       image;
    VkImageView   imageView;
    VmaAllocation imageAllocation;  
};

// Context for OS-Window, Vulkan Initialization, Swapchain Management.
// ---------------------------------------------------------

class RenderContext
{
public:
    RenderContext(uint32_t windowWidth, uint32_t windowHeight);
    ~RenderContext();

    // Dispatch a render loop into the OS window, invoking a provided command recording callback each frame.
    void Dispatch(std::function<void(FrameParams)> commandsFunc);

    inline VkDevice&      GetDevice()            { return m_VKDeviceLogical;     }
    inline VmaAllocator&  GetAllocator()         { return m_VKMemoryAllocator;   }
    inline VkQueue&       GetCommandQueue()      { return m_VKCommandQueue;      }
    inline uint32_t&      GetCommandQueueIndex() { return m_VKCommandQueueIndex; }
    inline VkCommandPool& GetCommandPool()       { return m_VKCommandPool;       }
    inline GLFWwindow*    GetWindow()            { return m_Window;              }

private:
    VkInstance       m_VKInstance;
    VkPhysicalDevice m_VKDevicePhysical;
    VkDevice         m_VKDeviceLogical;
    VkDescriptorPool m_VKDescriptorPool;
    VmaAllocator     m_VKMemoryAllocator;
    GLFWwindow*      m_Window;

    // Command Primitives
    VkCommandPool    m_VKCommandPool;
    VkQueue          m_VKCommandQueue;
    uint32_t         m_VKCommandQueueIndex;

    // Swapchain Primitives
    VkSwapchainKHR           m_VKSwapchain;
    VkSurfaceKHR             m_VKSurface;
	std::vector<VkImage>     m_VKSwapchainImages;
    std::vector<VkImageView> m_VKSwapchainImageViews;

    // Frame Primitives    
    VkCommandBuffer m_VKCommandBuffers           [kMaxFramesInFlight];
    VkSemaphore     m_VKImageAvailableSemaphores [kMaxFramesInFlight];
    VkSemaphore     m_VKRenderCompleteSemaphores [kMaxFramesInFlight];
    VkFence         m_VKInFlightFences           [kMaxFramesInFlight];
};

// USD Hydra Render Delegate
// ---------------------------------------------------------

const TfTokenVector kSupportedRPrimTypes =
{
    HdPrimTypeTokens->mesh,
};

const TfTokenVector kSupportedSPrimTypes =
{
    HdPrimTypeTokens->camera,
};

const TfTokenVector kSupportedBPrimTypes =
{
};

// This token is used for identifying our custom Hydra driver.
const TfToken kTokenRenderContextDriver = TfToken("RenderContextDriver");
const TfToken kTokenCurrenFrameParams   = TfToken("CurrentFrameParams");

class RenderDelegate : public HdRenderDelegate
{
public:
    RenderDelegate() {};
    RenderDelegate(HdRenderSettingsMap const& settingsMap) {};
    virtual ~RenderDelegate() {};

    void SetDrivers(HdDriverVector const& drivers) override;

    const TfTokenVector& GetSupportedRprimTypes() const override { return kSupportedRPrimTypes; };
    const TfTokenVector& GetSupportedSprimTypes() const override { return kSupportedSPrimTypes; };
    const TfTokenVector& GetSupportedBprimTypes() const override { return kSupportedBPrimTypes; };

    HdResourceRegistrySharedPtr GetResourceRegistry() const override { return nullptr; };

    HdRenderPassSharedPtr CreateRenderPass(HdRenderIndex* index, HdRprimCollection const& collection) override;

    HdInstancer* CreateInstancer(HdSceneDelegate *delegate, SdfPath const& id) override { return nullptr; };
    void DestroyInstancer(HdInstancer *instancer) override {};

    HdRprim* CreateRprim(TfToken const& typeId, SdfPath const& rprimId) override { return nullptr; };
    HdSprim* CreateSprim(TfToken const& typeId, SdfPath const& sprimId) override { return nullptr; };
    HdBprim* CreateBprim(TfToken const& typeId, SdfPath const& bprimId) override { return nullptr; };
    
    HdSprim* CreateFallbackSprim(TfToken const& typeId) override { return nullptr; };
    HdBprim* CreateFallbackBprim(TfToken const& typeId) override { return nullptr; };

    void DestroyRprim(HdRprim* rPrim) override { };
    void DestroySprim(HdSprim* sprim) override { };
    void DestroyBprim(HdBprim* bprim) override { };

    void CommitResources(HdChangeTracker *tracker) override { };

    HdRenderParam* GetRenderParam() const override { return nullptr; };

    inline RenderContext* GetRenderContext() { return m_RenderContext; };

private:

    // Reference to the custom Vulkan driver implementation.
    RenderContext* m_RenderContext;

    HdResourceRegistrySharedPtr m_ResourceRegistry;
};

// USD Hydra Render Pass
// ---------------------------------------------------------

class RenderPass final : public HdRenderPass
{
public:
    RenderPass(HdRenderIndex* pRenderIndex, HdRprimCollection const &collection, RenderDelegate* pRenderDelegate);
    virtual ~RenderPass();

protected:

    void _Execute(HdRenderPassStateSharedPtr const& pRenderPassState, TfTokenVector const &renderTags) override;

private:
    RenderDelegate* m_Owner;

    Image m_ColorAttachment;
    Image m_DepthAttachment;
};

struct SceneParseContext
{
    RenderContext* pRenderContext;

    // Device-uploaded resources.
    std::vector<std::pair<VkBuffer, VmaAllocation>>& dedicatedMemoryIndices;
    std::vector<std::pair<VkBuffer, VmaAllocation>>& dedicatedMemoryVertices;
};

// Forward-declared utilities. 
// ---------------------------------------------------------

bool CreatePhysicallyBasedMaterialDescriptorLayout  (const VkDevice& vkLogicalDevice, VkDescriptorSetLayout& vkDescriptorSetLayout);
bool SelectVulkanPhysicalDevice                     (const VkInstance& vkInstance, const std::vector<const char*> requiredExtensions, VkPhysicalDevice& vkPhysicalDevice);
bool CreateVulkanLogicalDevice                      (const VkPhysicalDevice& vkPhysicalDevice, const std::vector<const char*>& requiredExtensions, uint32_t vkGraphicsQueueIndex, VkDevice& vkLogicalDevice);
bool LoadByteCode                                   (const char* filePath, std::vector<char>& byteCode);
void SetDefaultRenderState                          (VkCommandBuffer commandBuffer);
bool GetVulkanQueueIndices                          (const VkInstance& vkInstance, const VkPhysicalDevice& vkPhysicalDevice, uint32_t& vkQueueIndexGraphics);
bool ParseScene                                     (const char* pFilePath, SceneParseContext& context);
void GetVertexInputLayout                           (std::vector<VkVertexInputBindingDescription2EXT>& bindings, std::vector<VkVertexInputAttributeDescription2EXT>& attributes);
bool CreateRenderingAttachments                     (RenderContext* pRenderContext, Image& colorAttachment, Image& depthAttachment);
void NameVulkanObject                               (VkDevice vkLogicalDevice, VkObjectType vkObjectType, uint64_t vkObject, std::string vkObjectName);

// Executable implementation.
// ---------------------------------------------------------

int main()
{
    // Launch Vulkan + OS Window
    // --------------------------------------

    std::unique_ptr<RenderContext> pRenderContext = std::make_unique<RenderContext>(kWindowWidth, kWindowHeight);

    // Create render delegate.
    // ---------------------

    auto pRenderDelegate = std::make_unique<RenderDelegate>();
    TF_VERIFY(pRenderDelegate != nullptr);

    // Wrap the RenderContext into a USD Hydra Driver. 
    // -------------------------------------- 

    HdDriver renderContextHydraDriver(kTokenRenderContextDriver, VtValue(pRenderContext.get()));
    
    // Create render index from the delegate. 
    // ---------------------

    auto pRenderIndex = HdRenderIndex::New(pRenderDelegate.get(), { &renderContextHydraDriver });
    TF_VERIFY(pRenderIndex != nullptr);

    // Construct a scene delegate from the stock OpenUSD scene delegate implementation.
    // ---------------------

    auto pSceneDelegate = new UsdImagingDelegate(pRenderIndex, SdfPath::AbsoluteRootPath());
    TF_VERIFY(pSceneDelegate != nullptr);

    // Load a USD Stage.
    // ---------------------

    auto pUsdStage = pxr::UsdStage::Open("C:\\Development\\MarketScene\\MarketScene.usd");
    TF_VERIFY(pUsdStage != nullptr);

    // Pipe the USD stage into the scene delegate (will create render primitives in the render delegate).
    // ---------------------

    pSceneDelegate->Populate(pUsdStage->GetPseudoRoot());

    // Create the render tasks.
    // ---------------------

    HdxTaskController taskController(pRenderIndex, SdfPath("/taskController"));
    {
        auto params = HdxRenderTaskParams();
        {
            params.viewport = GfVec4i(0, 0, kWindowWidth, kWindowHeight);
        }

        // The "Task Controller" will automatically configure an HdxRenderTask
        // (which will create and invoke our delegate's renderpass).
        taskController.SetRenderParams(params);
        taskController.SetRenderViewport({ 0, 0, kWindowWidth, kWindowHeight });
    }

    // Initialize the Hydra engine. 
    // ---------------------

    HdEngine engine;

    // Command Recording
    // ------------------------------------------------

    auto RecordCommands = [&](FrameParams frameParams)
    {
        // Forward the current backbuffer and commandbuffer to the delegate. 
        // There might be a simpler way to manage this by writing my own HdTask, but
        // it would require sacrificing the simplicity that HdxTaskController offers.
        pRenderDelegate->SetRenderSetting(kTokenCurrenFrameParams, VtValue(&frameParams));

        // Invoke Hydra!
        auto renderTasks = taskController.GetRenderingTasks();
        engine.Execute(pRenderIndex, &renderTasks);
    };
    
    // Kick off render-loop.
    // ------------------------------------------------

    pRenderContext->Dispatch(RecordCommands);
}

// Render Context Implementation
// ------------------------------------------------------------

RenderContext::RenderContext(uint32_t width, uint32_t height)
{
    Check(glfwInit(), "Failed to initialize GLFW.");

    // Initialize Vulkan
    // ------------------------------------------------

    Check(volkInitialize(), "Failed to initialize volk.");

    // Pass the dynamically loaded function pointer from volk. 
    glfwInitVulkanLoader(vkGetInstanceProcAddr);

    Check(glfwVulkanSupported(), "Failed to locate a Vulkan Loader for GLFW.");

    VkApplicationInfo vkApplicationInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    vkApplicationInfo.pApplicationName   = "Vulkan-Raytraced-Indirect";
    vkApplicationInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    vkApplicationInfo.pEngineName        = "No Engine";
    vkApplicationInfo.engineVersion      = VK_MAKE_VERSION(0, 0, 0);
    vkApplicationInfo.apiVersion         = VK_API_VERSION_1_3;

    std::vector<const char*> requiredInstanceLayers;
#ifdef _DEBUG
    requiredInstanceLayers.push_back("VK_LAYER_KHRONOS_validation");
#endif
    
    uint32_t windowExtensionCount;
    auto windowExtensions = glfwGetRequiredInstanceExtensions(&windowExtensionCount);

    std::vector<const char*> requiredInstanceExtensions;

    for (uint32_t windowExtensionIndex = 0u; windowExtensionIndex < windowExtensionCount; windowExtensionIndex++)
        requiredInstanceExtensions.push_back(windowExtensions[windowExtensionIndex]);

#ifdef _DEBUG
    requiredInstanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    VkInstanceCreateInfo vkInstanceCreateInfo    = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    vkInstanceCreateInfo.pApplicationInfo        = &vkApplicationInfo;
    vkInstanceCreateInfo.enabledLayerCount       = (uint32_t)requiredInstanceLayers.size();
    vkInstanceCreateInfo.ppEnabledLayerNames     = requiredInstanceLayers.data();
    vkInstanceCreateInfo.enabledExtensionCount   = (uint32_t)requiredInstanceExtensions.size();
    vkInstanceCreateInfo.ppEnabledExtensionNames = requiredInstanceExtensions.data();
    Check(vkCreateInstance(&vkInstanceCreateInfo, nullptr, &m_VKInstance), "Failed to create the Vulkan Instance.");

    volkLoadInstanceOnly(m_VKInstance);

    std::vector<const char*> requiredDeviceExtensions;
    {
        requiredDeviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        requiredDeviceExtensions.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
        requiredDeviceExtensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
        requiredDeviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        requiredDeviceExtensions.push_back(VK_EXT_SHADER_OBJECT_EXTENSION_NAME);
    }

    Check(SelectVulkanPhysicalDevice(m_VKInstance, requiredDeviceExtensions, m_VKDevicePhysical), "Failed to select a Vulkan Physical Device.");
    Check(GetVulkanQueueIndices(m_VKInstance, m_VKDevicePhysical, m_VKCommandQueueIndex), "Failed to obtain the required Vulkan Queue Indices from the physical device.");
    Check(CreateVulkanLogicalDevice(m_VKDevicePhysical, requiredDeviceExtensions, m_VKCommandQueueIndex, m_VKDeviceLogical), "Failed to create a Vulkan Logical Device");
 
    volkLoadDevice(m_VKDeviceLogical);

    // Create OS Window + Vulkan Swapchain
    // ------------------------------------------------

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    m_Window = glfwCreateWindow(kWindowWidth, kWindowHeight, "Vulkan Raytraced Indirect", NULL, NULL);
    Check(m_Window, "Failed to create the OS Window.");

    Check(glfwCreateWindowSurface(m_VKInstance, m_Window, NULL, &m_VKSurface), "Failed to create the Vulkan Surface.");

	VkSurfaceCapabilitiesKHR vkSurfaceProperties;
	Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_VKDevicePhysical, m_VKSurface, &vkSurfaceProperties), "Failed to obect the Vulkan Surface Properties");

    VkSwapchainCreateInfoKHR vkSwapchainCreateInfo = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    vkSwapchainCreateInfo.surface          = m_VKSurface;
    vkSwapchainCreateInfo.minImageCount    = vkSurfaceProperties.minImageCount + 1;
    vkSwapchainCreateInfo.imageExtent      = vkSurfaceProperties.currentExtent;
    vkSwapchainCreateInfo.imageArrayLayers = vkSurfaceProperties.maxImageArrayLayers;
    vkSwapchainCreateInfo.imageUsage       = vkSurfaceProperties.supportedUsageFlags;
    vkSwapchainCreateInfo.preTransform     = vkSurfaceProperties.currentTransform;
    vkSwapchainCreateInfo.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    vkSwapchainCreateInfo.imageFormat      = VK_FORMAT_R8G8B8A8_UNORM;
    vkSwapchainCreateInfo.imageColorSpace  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    vkSwapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkSwapchainCreateInfo.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
    vkSwapchainCreateInfo.oldSwapchain     = nullptr;
    vkSwapchainCreateInfo.clipped          = true;
    Check(vkCreateSwapchainKHR(m_VKDeviceLogical, &vkSwapchainCreateInfo, nullptr, &m_VKSwapchain), "Failed to create the Vulkan Swapchain");

	uint32_t vkSwapchainImageCount;
	Check(vkGetSwapchainImagesKHR(m_VKDeviceLogical, m_VKSwapchain, &vkSwapchainImageCount, nullptr), "Failed to obtain Vulkan Swapchain image count.");

    m_VKSwapchainImages    .resize(vkSwapchainImageCount);
    m_VKSwapchainImageViews.resize(vkSwapchainImageCount);

	Check(vkGetSwapchainImagesKHR(m_VKDeviceLogical, m_VKSwapchain, &vkSwapchainImageCount, m_VKSwapchainImages.data()), "Failed to obtain the Vulkan Swapchain images.");

#ifdef _DEBUG
    for (uint32_t swapChainIndex = 0u; swapChainIndex < vkSwapchainImageCount; swapChainIndex++)
    {
        auto swapChainName = std::format("Swapchain Image {}", swapChainIndex);
        NameVulkanObject(m_VKDeviceLogical, VK_OBJECT_TYPE_IMAGE, (uint64_t)m_VKSwapchainImages[swapChainIndex], swapChainName);
    }
#endif

    VkImageSubresourceRange vkSwapchainImageSubresourceRange;
    {
		vkSwapchainImageSubresourceRange.levelCount     = 1u;
		vkSwapchainImageSubresourceRange.layerCount     = 1u;
        vkSwapchainImageSubresourceRange.baseMipLevel   = 0u;
        vkSwapchainImageSubresourceRange.baseArrayLayer = 0u;
		vkSwapchainImageSubresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    for (uint32_t imageIndex = 0; imageIndex < vkSwapchainImageCount; imageIndex++)
	{
		// Create an image view which we can render into.
		VkImageViewCreateInfo vkImageViewInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };

		vkImageViewInfo.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
		vkImageViewInfo.format                      = VK_FORMAT_R8G8B8A8_UNORM;
		vkImageViewInfo.image                       = m_VKSwapchainImages[imageIndex];
        vkImageViewInfo.subresourceRange            = vkSwapchainImageSubresourceRange;
		vkImageViewInfo.components.r                = VK_COMPONENT_SWIZZLE_R;
		vkImageViewInfo.components.g                = VK_COMPONENT_SWIZZLE_G;
		vkImageViewInfo.components.b                = VK_COMPONENT_SWIZZLE_B;
		vkImageViewInfo.components.a                = VK_COMPONENT_SWIZZLE_A;

		VkImageView vkImageView;
		Check(vkCreateImageView(m_VKDeviceLogical, &vkImageViewInfo, nullptr, &vkImageView), "Failed to create a Swapchain Image View.");

        m_VKSwapchainImageViews[imageIndex] = vkImageView;
	}

    VkCommandPoolCreateInfo vkCommandPoolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    {
        vkCommandPoolInfo.queueFamilyIndex = m_VKCommandQueueIndex;
        vkCommandPoolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    }
    Check(vkCreateCommandPool(m_VKDeviceLogical, &vkCommandPoolInfo, nullptr, &m_VKCommandPool), "Failed to create a Vulkan Command Pool");

    // Per-frame resources. 
    for (uint32_t frameIndex = 0u; frameIndex < kMaxFramesInFlight; frameIndex++)
    {
        VkCommandBufferAllocateInfo vkCommandBufferInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        {
            vkCommandBufferInfo.commandPool        = m_VKCommandPool;
            vkCommandBufferInfo.commandBufferCount = 1u;
            vkCommandBufferInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        }

        Check(vkAllocateCommandBuffers(m_VKDeviceLogical, &vkCommandBufferInfo, &m_VKCommandBuffers[frameIndex]), "Failed to allocate Vulkan Command Buffers.");
        
        VkSemaphoreCreateInfo vkSemaphoreInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0x0                          };
        VkFenceCreateInfo     vkFenceInfo     = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,     nullptr, VK_FENCE_CREATE_SIGNALED_BIT };

        // Synchronization Primitives
        Check(vkCreateSemaphore (m_VKDeviceLogical, &vkSemaphoreInfo, NULL, &m_VKImageAvailableSemaphores[frameIndex]), "Failed to create Vulkan Semaphore.");
        Check(vkCreateSemaphore (m_VKDeviceLogical, &vkSemaphoreInfo, NULL, &m_VKRenderCompleteSemaphores[frameIndex]), "Failed to create Vulkan Semaphore.");
        Check(vkCreateFence     (m_VKDeviceLogical, &vkFenceInfo,     NULL, &m_VKInFlightFences[frameIndex]),           "Failed to create Vulkan Fence.");
    }

    // Obtain Queues (just Graphics for now).
    // ------------------------------------------------

    vkGetDeviceQueue(m_VKDeviceLogical, m_VKCommandQueueIndex, 0u, &m_VKCommandQueue);

    // Create Memory Allocator
    // ------------------------------------------------

    VmaVulkanFunctions vmaVulkanFunctions = {};
    vmaVulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vmaVulkanFunctions.vkGetDeviceProcAddr   = vkGetDeviceProcAddr;
    
    VmaAllocatorCreateInfo vmaAllocatorInfo = {};
    vmaAllocatorInfo.flags            = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
    vmaAllocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    vmaAllocatorInfo.physicalDevice   = m_VKDevicePhysical;
    vmaAllocatorInfo.device           = m_VKDeviceLogical;
    vmaAllocatorInfo.instance         = m_VKInstance;
    vmaAllocatorInfo.pVulkanFunctions = &vmaVulkanFunctions;
    Check(vmaCreateAllocator(&vmaAllocatorInfo, &m_VKMemoryAllocator), "Failed to create Vulkan Memory Allocator.");

    // Create Descriptor Pool
    // ------------------------------------------------

    VkDescriptorPoolSize vkDescriptorPoolSizes[2] = 
    {
        { VK_DESCRIPTOR_TYPE_SAMPLER,         1u },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 128u }
    };

    VkDescriptorPoolCreateInfo vkDescriptorPoolInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    {
        vkDescriptorPoolInfo.poolSizeCount = ARRAYSIZE(vkDescriptorPoolSizes);
        vkDescriptorPoolInfo.pPoolSizes    = vkDescriptorPoolSizes;
        vkDescriptorPoolInfo.maxSets       = 1u;
        vkDescriptorPoolInfo.flags         = 0x0;
    }
    Check(vkCreateDescriptorPool(m_VKDeviceLogical, &vkDescriptorPoolInfo, VK_NULL_HANDLE, &m_VKDescriptorPool), "Failed to create Vulkan Descriptor Pool.");

    // Done.
    // ------------------------------------------------

    spdlog::info("Initialized Render Context.");
}

RenderContext::~RenderContext()
{
    glfwDestroyWindow(m_Window);
    glfwTerminate();

    vmaDestroyAllocator(m_VKMemoryAllocator);

    for (uint32_t frameIndex = 0u; frameIndex < kMaxFramesInFlight; frameIndex++)
    {
        vkDestroySemaphore (m_VKDeviceLogical, m_VKImageAvailableSemaphores[frameIndex], nullptr);
        vkDestroySemaphore (m_VKDeviceLogical, m_VKRenderCompleteSemaphores[frameIndex], nullptr);
        vkDestroyFence     (m_VKDeviceLogical, m_VKInFlightFences[frameIndex],           nullptr);
    }

    for (auto& vkImageView : m_VKSwapchainImageViews)
        vkDestroyImageView(m_VKDeviceLogical, vkImageView, nullptr);

    vkDestroyDescriptorPool (m_VKDeviceLogical, m_VKDescriptorPool, nullptr);
    vkDestroyCommandPool    (m_VKDeviceLogical, m_VKCommandPool,    nullptr);
    vkDestroySwapchainKHR   (m_VKDeviceLogical, m_VKSwapchain,      nullptr);
    vkDestroyDevice         (m_VKDeviceLogical,                     nullptr);
    vkDestroySurfaceKHR     (m_VKInstance,      m_VKSurface,        nullptr);
    vkDestroyInstance       (m_VKInstance,                          nullptr);
}  

void RenderContext::Dispatch(std::function<void(FrameParams)> commandsCallback)
{
    uint64_t frameIndex = 0u;

    // Initialize delta time.
    auto deltaTime = std::chrono::duration<double>(0.0);

    // Render-loop
    // ------------------------------------------------

    while (!glfwWindowShouldClose(m_Window))
    {
        // Sample the time at the beginning of the frame.
        auto frameTimeBegin = std::chrono::high_resolution_clock::now();

        // Determine frame-in-flight index.
        uint32_t frameInFlightIndex = frameIndex % kMaxFramesInFlight;

        // Wait for the current frame fence to be signaled.
        Check(vkWaitForFences(m_VKDeviceLogical, 1u, &m_VKInFlightFences[frameInFlightIndex], VK_TRUE, UINT64_MAX), "Failed to wait for frame fence");

        // Acquire the next swap chain image available.
        uint32_t vkCurrentSwapchainImageIndex;
        Check(vkAcquireNextImageKHR(m_VKDeviceLogical, m_VKSwapchain, UINT64_MAX, m_VKImageAvailableSemaphores[frameInFlightIndex], VK_NULL_HANDLE, &vkCurrentSwapchainImageIndex), "Failed to acquire swapchain image.");

        // Get the current frame's command buffer.
        auto& vkCurrentCommandBuffer = m_VKCommandBuffers[frameInFlightIndex];

        // Clear previous work. 
        Check(vkResetCommandBuffer(vkCurrentCommandBuffer, 0x0), "Failed to reset frame command buffer");

        // Open command recording.
        VkCommandBufferBeginInfo vkCommandBufferBeginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        {
            vkCommandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        }
        Check(vkBeginCommandBuffer(vkCurrentCommandBuffer, &vkCommandBufferBeginInfo), "Failed to open frame command buffer for recording");

        // Dispatch command recording.
        FrameParams frameParams = 
        {
            vkCurrentCommandBuffer,
            m_VKSwapchainImages    [vkCurrentSwapchainImageIndex],
            m_VKSwapchainImageViews[vkCurrentSwapchainImageIndex],
            deltaTime.count()
        };
        commandsCallback(frameParams);

        // Close command recording.
        Check(vkEndCommandBuffer(vkCurrentCommandBuffer), "Failed to close frame command buffer for recording");

        // Reset the frame fence to re-signal. 
        Check(vkResetFences(m_VKDeviceLogical, 1u, &m_VKInFlightFences[frameInFlightIndex]), "Failed to reset the frame fence.");

        VkSubmitInfo vkQueueSubmitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        {
            VkPipelineStageFlags vkWaitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

            vkQueueSubmitInfo.commandBufferCount   = 1u;
            vkQueueSubmitInfo.pCommandBuffers      = &vkCurrentCommandBuffer;
            vkQueueSubmitInfo.waitSemaphoreCount   = 1u;
            vkQueueSubmitInfo.pWaitSemaphores      = &m_VKImageAvailableSemaphores[frameInFlightIndex];
            vkQueueSubmitInfo.signalSemaphoreCount = 1u;
            vkQueueSubmitInfo.pSignalSemaphores    = &m_VKRenderCompleteSemaphores[frameInFlightIndex];
            vkQueueSubmitInfo.pWaitDstStageMask    = &vkWaitStageMask;
        }
        Check(vkQueueSubmit(m_VKCommandQueue, 1u, &vkQueueSubmitInfo, m_VKInFlightFences[frameInFlightIndex]), "Failed to submit commands to the Vulkan Graphics Queue.");

        VkPresentInfoKHR vkQueuePresentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        {
            vkQueuePresentInfo.waitSemaphoreCount = 1u;
            vkQueuePresentInfo.pWaitSemaphores    = &m_VKRenderCompleteSemaphores[frameInFlightIndex];
            vkQueuePresentInfo.swapchainCount     = 1u;
            vkQueuePresentInfo.pSwapchains        = &m_VKSwapchain;
            vkQueuePresentInfo.pImageIndices      = &vkCurrentSwapchainImageIndex;
        }
        Check(vkQueuePresentKHR(m_VKCommandQueue, &vkQueuePresentInfo), "Failed to submit image to the Vulkan Presentation Engine.");
        
        // Advance to the next frame. 
        frameIndex++;

        glfwPollEvents();

        // Sample the time at the end of the frame.
        auto frameTimeEnd = std::chrono::high_resolution_clock::now();

        // Update delta time.
        deltaTime = frameTimeEnd - frameTimeBegin;
    }
}

// Utilities Implementation
// ------------------------------------------------------------

bool CreateRenderingAttachments(RenderContext* pRenderContext, Image& colorAttachment, Image& depthAttachment)
{
    auto CreateAttachment = [&](Image& attachment, VkFormat imageFormat, VkImageUsageFlags imageUsageFlags, VkImageAspectFlags imageAspect)
    {
        VkImageCreateInfo imageInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        {
            imageInfo.imageType     = VK_IMAGE_TYPE_2D;
            imageInfo.arrayLayers   = 1u;
            imageInfo.format        = imageFormat;
            imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.usage         = imageUsageFlags;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.extent        = { kWindowWidth, kWindowHeight, 1};
            imageInfo.mipLevels     = 1u;
            imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.flags         = 0x0;
        }

        VmaAllocationCreateInfo imageAllocInfo = {};
        {
            imageAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        }

        Check(vmaCreateImage(pRenderContext->GetAllocator(), &imageInfo, &imageAllocInfo, &attachment.image, &attachment.imageAllocation, VK_NULL_HANDLE), "Failed to create attachment allocation.");

#ifdef _DEBUG
        auto attachmentName = std::format("{} Attachment", imageAspect & VK_IMAGE_ASPECT_COLOR_BIT ? "Color" : "Depth");
        NameVulkanObject(pRenderContext->GetDevice(), VK_OBJECT_TYPE_IMAGE, (uint64_t)attachment.image, attachmentName);
#endif

        VkImageViewCreateInfo imageViewInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        {
            imageViewInfo.image                           = attachment.image;
            imageViewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
            imageViewInfo.format                          = imageFormat;
            imageViewInfo.subresourceRange.levelCount     = 1u;
		    imageViewInfo.subresourceRange.layerCount     = 1u;
            imageViewInfo.subresourceRange.baseMipLevel   = 0u;
            imageViewInfo.subresourceRange.baseArrayLayer = 0u;
		    imageViewInfo.subresourceRange.aspectMask     = imageAspect;
        }
        Check(vkCreateImageView(pRenderContext->GetDevice(), &imageViewInfo, nullptr, &attachment.imageView), "Failed to create attachment view.");
    };

    CreateAttachment(colorAttachment, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    CreateAttachment(depthAttachment, VK_FORMAT_D32_SFLOAT,     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,                           VK_IMAGE_ASPECT_DEPTH_BIT);

    return true;
}

bool CreatePhysicallyBasedMaterialDescriptorLayout(const VkDevice& vkLogicalDevice, VkDescriptorSetLayout& vkDescriptorSetLayout)
{
    VkDescriptorSetLayoutBinding vkDescriptorSetLayoutBindings[1] =
    {
        // Albedo only for now.
        { 0u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1u, VK_SHADER_STAGE_FRAGMENT_BIT, VK_NULL_HANDLE } 
    };

    VkDescriptorSetLayoutCreateInfo vkDescriptorSetLayoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    {
        // Define a generic descriptor layout for PBR materials.
        vkDescriptorSetLayoutInfo.flags        = 0x0;
        vkDescriptorSetLayoutInfo.bindingCount = ARRAYSIZE(vkDescriptorSetLayoutBindings);
        vkDescriptorSetLayoutInfo.pBindings    = vkDescriptorSetLayoutBindings;
    }
    
    return vkCreateDescriptorSetLayout(vkLogicalDevice, &vkDescriptorSetLayoutInfo, nullptr, &vkDescriptorSetLayout) == VK_SUCCESS;
}

bool SelectVulkanPhysicalDevice(const VkInstance& vkInstance, const std::vector<const char*> requiredExtensions, VkPhysicalDevice& vkPhysicalDevice)
{    
    uint32_t deviceCount = 0u;
    vkEnumeratePhysicalDevices(vkInstance, &deviceCount, nullptr);

    std::vector<VkPhysicalDevice> vkPhysicalDevices(deviceCount);
    vkEnumeratePhysicalDevices(vkInstance, &deviceCount, vkPhysicalDevices.data());

    vkPhysicalDevice = VK_NULL_HANDLE;

    for (const auto& physicalDevice : vkPhysicalDevices)
    {
        VkPhysicalDeviceProperties physicalDeviceProperties;
        vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);

        if (physicalDeviceProperties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            continue;

        // Found the matching Vulkan Physical Device for the existing DXGI Adapter.
        vkPhysicalDevice = physicalDevice;

        break;
    }

    if (vkPhysicalDevice == VK_NULL_HANDLE)
        return false;

    VkPhysicalDeviceProperties physicalDeviceProperties;
    vkGetPhysicalDeviceProperties(vkPhysicalDevice, &physicalDeviceProperties);

    spdlog::info("Selected Vulkan Physical Device: {}", physicalDeviceProperties.deviceName);

    // Confirm that the selected physical device supports the required extensions.
    uint32_t supportedDeviceExtensionCount; 
    vkEnumerateDeviceExtensionProperties(vkPhysicalDevice, nullptr, &supportedDeviceExtensionCount, nullptr);

    std::vector<VkExtensionProperties> supportedDeviceExtensions(supportedDeviceExtensionCount);
    vkEnumerateDeviceExtensionProperties(vkPhysicalDevice, nullptr, &supportedDeviceExtensionCount, supportedDeviceExtensions.data());

    auto CheckExtension = [&](const char* extensionName)
    {
        for (const auto& deviceExtension : supportedDeviceExtensions)
        {
            if (!strcmp(deviceExtension.extensionName, extensionName))
                return true;
        }

        return false;
    };

    for (const auto& requiredExtension : requiredExtensions)
    {
        if (CheckExtension(requiredExtension))
            continue;
        
        spdlog::error("The selected Vulkan physical device does not support required Vulkan Extension: {}", requiredExtension);
        return false;
    }

    return true;
}

bool CreateVulkanLogicalDevice(const VkPhysicalDevice& vkPhysicalDevice, const std::vector<const char*>& requiredExtensions, uint32_t vkGraphicsQueueIndex, VkDevice& vkLogicalDevice)
{
    float graphicsQueuePriority = 1.0;

    VkDeviceQueueCreateInfo vkGraphicsQueueCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    vkGraphicsQueueCreateInfo.queueFamilyIndex = vkGraphicsQueueIndex;
    vkGraphicsQueueCreateInfo.queueCount       = 1u;
    vkGraphicsQueueCreateInfo.pQueuePriorities = &graphicsQueuePriority;

    VkPhysicalDeviceShaderObjectFeaturesEXT shaderObjectFeature = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT };
    VkPhysicalDeviceVulkan13Features        vulkan13Features    = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES        };
    VkPhysicalDeviceVulkan12Features        vulkan12Features    = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES        };
    VkPhysicalDeviceVulkan11Features        vulkan11Features    = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES        };
    VkPhysicalDeviceFeatures2               vulkan10Features    = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2                 };

    vulkan13Features.pNext = &shaderObjectFeature;
    vulkan12Features.pNext = &vulkan13Features;
    vulkan11Features.pNext = &vulkan12Features;
    vulkan10Features.pNext = &vulkan11Features;

    // Query for supported features.
    vkGetPhysicalDeviceFeatures2(vkPhysicalDevice, &vulkan10Features);

    for (const auto& requiredExtension : requiredExtensions)
    {
        if (!strcmp(requiredExtension, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME) && vulkan12Features.timelineSemaphore != VK_TRUE)
            return false;

        if (!strcmp(requiredExtension, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME) && vulkan13Features.synchronization2 != VK_TRUE)
            return false;

        if (!strcmp(requiredExtension, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) && vulkan13Features.dynamicRendering != VK_TRUE)
            return false;

        if (!strcmp(requiredExtension, VK_EXT_SHADER_OBJECT_EXTENSION_NAME) && shaderObjectFeature.shaderObject != VK_TRUE)
            return false;
    }

    VkDeviceCreateInfo vkLogicalDeviceCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    vkLogicalDeviceCreateInfo.pNext                   = &vulkan10Features;
    vkLogicalDeviceCreateInfo.pQueueCreateInfos       = &vkGraphicsQueueCreateInfo;
    vkLogicalDeviceCreateInfo.queueCreateInfoCount    = 1u;
    vkLogicalDeviceCreateInfo.enabledExtensionCount   = (uint32_t)requiredExtensions.size();
    vkLogicalDeviceCreateInfo.ppEnabledExtensionNames = requiredExtensions.data();

    return vkCreateDevice(vkPhysicalDevice, &vkLogicalDeviceCreateInfo, nullptr, &vkLogicalDevice) == VK_SUCCESS;
}

bool LoadByteCode(const char* filePath, std::vector<char>& byteCode)
{
    std::fstream file(std::format("..\\Shaders\\Compiled\\{}", filePath), std::ios::in | std::ios::binary);

    if (!file.is_open())
        return false;
    
    file.seekg(0, std::ios::end);
    auto byteCodeSize = file.tellg();
    file.seekg(0, std::ios::beg);

    byteCode.resize(byteCodeSize);
    file.read(byteCode.data(), byteCodeSize);

    file.close();

    return true;
}

void SetDefaultRenderState(VkCommandBuffer commandBuffer)
{
    static VkColorComponentFlags s_DefaultWriteMask =   VK_COLOR_COMPONENT_R_BIT | 
                                                        VK_COLOR_COMPONENT_G_BIT | 
                                                        VK_COLOR_COMPONENT_B_BIT | 
                                                        VK_COLOR_COMPONENT_A_BIT;

    static VkColorBlendEquationEXT s_DefaultColorBlend = 
    {
        // Color
        VK_BLEND_FACTOR_ONE,
        VK_BLEND_FACTOR_ZERO,
        VK_BLEND_OP_ADD,

        // Alpha
        VK_BLEND_FACTOR_ONE,
        VK_BLEND_FACTOR_ZERO,
        VK_BLEND_OP_ADD,
    };

    static VkBool32     s_DefaultBlendEnable = VK_FALSE;
    static VkViewport   s_DefaultViewport    = { 0, 0, kWindowWidth, kWindowHeight, 0.0, 1.0 };
    static VkRect2D     s_DefaultScissor     = { {0, 0}, {kWindowWidth, kWindowHeight} };
    static VkSampleMask s_DefaultSampleMask  = 0xFFFFFFFF;

    vkCmdSetColorBlendEnableEXT       (commandBuffer, 0u, 1u, &s_DefaultBlendEnable);
    vkCmdSetColorWriteMaskEXT         (commandBuffer, 0u, 1u, &s_DefaultWriteMask);
    vkCmdSetColorBlendEquationEXT     (commandBuffer, 0u, 1u, &s_DefaultColorBlend);
    vkCmdSetViewportWithCountEXT      (commandBuffer, 1u, &s_DefaultViewport);
    vkCmdSetScissorWithCountEXT       (commandBuffer, 1u, &s_DefaultScissor);
    vkCmdSetPrimitiveRestartEnableEXT (commandBuffer, VK_FALSE);
    vkCmdSetRasterizerDiscardEnableEXT(commandBuffer, VK_FALSE);
    vkCmdSetAlphaToOneEnableEXT       (commandBuffer, VK_FALSE);
    vkCmdSetAlphaToCoverageEnableEXT  (commandBuffer, VK_FALSE);
    vkCmdSetStencilTestEnableEXT      (commandBuffer, VK_FALSE);
    vkCmdSetDepthBiasEnableEXT        (commandBuffer, VK_FALSE);
    vkCmdSetDepthTestEnableEXT        (commandBuffer, VK_TRUE);
    vkCmdSetDepthWriteEnableEXT       (commandBuffer, VK_TRUE);
    vkCmdSetDepthCompareOpEXT         (commandBuffer, VK_COMPARE_OP_LESS_OR_EQUAL);
    vkCmdSetDepthBoundsTestEnable     (commandBuffer, VK_FALSE);
    vkCmdSetDepthClampEnableEXT       (commandBuffer, VK_FALSE);
    vkCmdSetLogicOpEnableEXT          (commandBuffer, VK_FALSE);
    vkCmdSetRasterizationSamplesEXT   (commandBuffer, VK_SAMPLE_COUNT_1_BIT);
    vkCmdSetSampleMaskEXT             (commandBuffer, VK_SAMPLE_COUNT_1_BIT, &s_DefaultSampleMask);
    vkCmdSetFrontFaceEXT              (commandBuffer, VK_FRONT_FACE_CLOCKWISE);
    vkCmdSetPolygonModeEXT            (commandBuffer, VK_POLYGON_MODE_FILL);
    vkCmdSetCullModeEXT               (commandBuffer, VK_CULL_MODE_BACK_BIT);
    vkCmdSetPrimitiveTopologyEXT      (commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
}

bool GetVulkanQueueIndices(const VkInstance& vkInstance, const VkPhysicalDevice& vkPhysicalDevice, uint32_t& vkQueueIndexGraphics)
{
    vkQueueIndexGraphics = UINT_MAX;
    
    uint32_t queueFamilyCount = 0u;
    vkGetPhysicalDeviceQueueFamilyProperties(vkPhysicalDevice, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(vkPhysicalDevice, &queueFamilyCount, queueFamilyProperties.data());

    for (uint32_t queueFamilyIndex = 0; queueFamilyIndex < queueFamilyCount; queueFamilyIndex++)
    {
        if (!glfwGetPhysicalDevicePresentationSupport(vkInstance, vkPhysicalDevice, queueFamilyIndex))
            continue;

        if (!(queueFamilyProperties[queueFamilyIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            continue;

        vkQueueIndexGraphics = queueFamilyIndex;

        break;
    }

    return vkQueueIndexGraphics != UINT_MAX;
}

bool ParseScene(const char* pFilePath, SceneParseContext& context)
{
    auto pRenderContext = context.pRenderContext;

    return false;

    // if (!reader.ParseFromFile(pFilePath))
    //     return false;

    // if (!reader.Warning().empty())
    //     spdlog::warn("[tinyobj] [{}]:\n\n{}", pFilePath, reader.Warning());

    // const auto& shapes    = reader.GetShapes();
    // const auto& attrib    = reader.GetAttrib();
    // const auto& materials = reader.GetMaterials();

    /*
    for (const auto& material : materials)
    {
        spdlog::info("Parsing Material: {}", material.name);

        VkDescriptorSetAllocateInfo vkDescriptorSetAllocateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        {
            vkDescriptorSetAllocateInfo.descriptorPool      = *context.vkDescriptorPool;
            vkDescriptorSetAllocateInfo.descriptorSetCount  = 1u;
            vkDescriptorSetAllocateInfo.pSetLayouts         = context.vkMaterialDescriptorSetLayout;
        }

        VkDescriptorSet vkDescriptorSet;
        Check(vkAllocateDescriptorSets(*context.vkLogicalDevice, &vkDescriptorSetAllocateInfo, &vkDescriptorSet), "Failed to create Vulkan Descriptor Sets.");
    }
    */

    /*
    std::vector<std::pair<VkBuffer, VmaAllocation>> stagingMemoryIndices;
    std::vector<std::pair<VkBuffer, VmaAllocation>> stagingMemoryVertices;

    auto UploadHostMemoryToStagingBuffer = [&](const void* pData, uint32_t size)
    {
        VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.size  = size;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        
        std::pair<VkBuffer, VmaAllocation> stagingBuffer;
        Check(vmaCreateBuffer(pRenderContext->GetAllocator(), &bufferInfo, &allocInfo, &stagingBuffer.first, &stagingBuffer.second, nullptr), "Failed to create staging buffer memory.");

        void* pMappedData;
        Check(vmaMapMemory(pRenderContext->GetAllocator(), stagingBuffer.second, &pMappedData), "Failed to map a pointer to staging memory.");
        {
            // Copy from Host -> Staging memory.
            memcpy(pMappedData, pData, size);

            vmaUnmapMemory(pRenderContext->GetAllocator(), stagingBuffer.second);
        }

        return stagingBuffer;
    };

    auto AllocateDedicatedBuffer = [&](uint32_t size, VkBufferUsageFlags usage)
    {
        VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.size  = size;
        bufferInfo.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        
        std::pair<VkBuffer, VmaAllocation> dedicatedBuffer;
        Check(vmaCreateBuffer(pRenderContext->GetAllocator(), &bufferInfo, &allocInfo, &dedicatedBuffer.first, &dedicatedBuffer.second, nullptr), "Failed to create staging buffer memory.");

        return dedicatedBuffer;
    };

    for (const auto& shape : shapes)
    {
        spdlog::info("Parsing Shape: {}", shape.name);

        // Pull the mesh info.
        const auto& mesh = shape.mesh;

        // Flattened mesh data.
        std::vector<uint32_t> hostMemoryIndices;
        std::vector<Vertex>   hostMemoryVertices;
        
        for (const auto& index : mesh.indices)
        {
            Vertex vertex;

            // Positions.
            vertex.positionOS[0] = attrib.vertices[3u * size_t(index.vertex_index) + 0u];
            vertex.positionOS[1] = attrib.vertices[3u * size_t(index.vertex_index) + 1u];
            vertex.positionOS[2] = attrib.vertices[3u * size_t(index.vertex_index) + 2u];
            
            // Normals.
            vertex.normalOS[0] = (index.normal_index >= 0) ? attrib.normals[3u * size_t(index.normal_index) + 0u] : 0;
            vertex.normalOS[1] = (index.normal_index >= 0) ? attrib.normals[3u * size_t(index.normal_index) + 1u] : 0;
            vertex.normalOS[2] = (index.normal_index >= 0) ? attrib.normals[3u * size_t(index.normal_index) + 2u] : 1;

            // Texture Coordinates.
            vertex.texCoord0[0] = (index.texcoord_index >= 0) ? attrib.texcoords[2u * size_t(index.texcoord_index) + 0u] : 0;
            vertex.texCoord0[1] = (index.texcoord_index >= 0) ? attrib.texcoords[2u * size_t(index.texcoord_index) + 1u] : 0;

            // Push the vertex. TODO: Deduplicate.
            hostMemoryVertices.push_back(vertex);

            // Increment indices. 
            hostMemoryIndices.push_back((uint32_t)hostMemoryIndices.size());
        }

        // Staging Memory.
        stagingMemoryIndices .push_back(UploadHostMemoryToStagingBuffer(hostMemoryIndices.data(),  sizeof(uint32_t) * (uint32_t)hostMemoryIndices.size()));
        stagingMemoryVertices.push_back(UploadHostMemoryToStagingBuffer(hostMemoryVertices.data(), sizeof(Vertex)   * (uint32_t)hostMemoryVertices.size()));

        // Dedicated Memory.
        context.dedicatedMemoryIndices .push_back(AllocateDedicatedBuffer(sizeof(uint32_t) * (uint32_t)hostMemoryIndices.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT));
        context.dedicatedMemoryVertices.push_back(AllocateDedicatedBuffer(sizeof(Vertex)   * (uint32_t)hostMemoryIndices.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
    }

    // Transfer Staging -> Dedicated Memory.

    VkCommandBufferAllocateInfo vkCommandAllocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    {
        vkCommandAllocateInfo.commandBufferCount = 1u;
        vkCommandAllocateInfo.commandPool        = pRenderContext->GetCommandPool();
    }

    VkCommandBuffer vkCommand;
    Check(vkAllocateCommandBuffers(pRenderContext->GetDevice(), &vkCommandAllocateInfo, &vkCommand), "Failed to created command buffer for uploading scene resource memory.");

    VkCommandBufferBeginInfo vkCommandsBeginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    {
        vkCommandsBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    }
    Check(vkBeginCommandBuffer(vkCommand, &vkCommandsBeginInfo), "Failed to begin recording upload commands");

    auto RecordCopy = [&](std::pair<VkBuffer, VmaAllocation>& src, std::pair<VkBuffer, VmaAllocation>& dst)
    {
        VmaAllocationInfo allocationInfo;
        vmaGetAllocationInfo(pRenderContext->GetAllocator(), src.second, &allocationInfo);

        VkBufferCopy copyInfo;
        {
            copyInfo.srcOffset = 0u;
            copyInfo.dstOffset = 0u;
            copyInfo.size      = allocationInfo.size;
        }
        vkCmdCopyBuffer(vkCommand, src.first, dst.first, 1u, &copyInfo);
    };

    for (uint32_t shapeIndex = 0u; shapeIndex < shapes.size(); shapeIndex++)
    {
        RecordCopy(stagingMemoryIndices[shapeIndex],  context.dedicatedMemoryIndices[shapeIndex]);
        RecordCopy(stagingMemoryVertices[shapeIndex], context.dedicatedMemoryVertices[shapeIndex]);
    }

    Check(vkEndCommandBuffer(vkCommand), "Failed to end recording upload commands");

    VkSubmitInfo vkSubmitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    {
        vkSubmitInfo.commandBufferCount = 1u;
        vkSubmitInfo.pCommandBuffers    = &vkCommand;
    }
    Check(vkQueueSubmit(pRenderContext->GetCommandQueue(), 1u, &vkSubmitInfo, VK_NULL_HANDLE), "Failed to submit copy commands to the graphics queue.");

    // Wait for the copies to complete. 
    Check(vkDeviceWaitIdle(pRenderContext->GetDevice()), "Failed to wait for copy commands to finish dispatching.");

    // Release staging memory. 
    for (uint32_t shapeIndex = 0u; shapeIndex < shapes.size(); shapeIndex++)
    {
        vmaDestroyBuffer(pRenderContext->GetAllocator(), stagingMemoryIndices [shapeIndex].first, stagingMemoryIndices [shapeIndex].second);
        vmaDestroyBuffer(pRenderContext->GetAllocator(), stagingMemoryVertices[shapeIndex].first, stagingMemoryVertices[shapeIndex].second);
    }

    return true;
    */
}

void GetVertexInputLayout(std::vector<VkVertexInputBindingDescription2EXT>& bindings, std::vector<VkVertexInputAttributeDescription2EXT>& attributes)
{
    VkVertexInputBindingDescription2EXT binding = { VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT };
    {
        binding.binding   = 0u;
        binding.stride    = sizeof(Vertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        binding.divisor   = 1u;
    }
    bindings.push_back( binding );

    VkVertexInputAttributeDescription2EXT attribute = { VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT };

    // Position
    {
        attribute.binding  = 0u;
        attribute.location = 0u;
        attribute.offset   = offsetof(Vertex, positionOS);
        attribute.format   = VK_FORMAT_R32G32B32_SFLOAT;
    }
    attributes.push_back( attribute );

    // Normal
    {
        attribute.binding  = 0u;
        attribute.location = 1u;
        attribute.offset   = offsetof(Vertex, normalOS);
        attribute.format   = VK_FORMAT_R32G32B32_SFLOAT;
    }
    attributes.push_back( attribute );

    // Texcoord
    {
        attribute.binding  = 0u;
        attribute.location = 2u;
        attribute.offset   = offsetof(Vertex, texCoord0);
        attribute.format   = VK_FORMAT_R32G32_SFLOAT;
    }
    attributes.push_back( attribute );
}

void NameVulkanObject(VkDevice vkLogicalDevice, VkObjectType vkObjectType, uint64_t vkObject, std::string vkObjectName)
{
    VkDebugUtilsObjectNameInfoEXT attachmentNameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
    
    attachmentNameInfo.pObjectName  = vkObjectName.c_str();
    attachmentNameInfo.objectType   = vkObjectType;
    attachmentNameInfo.objectHandle = vkObject;
        
    vkSetDebugUtilsObjectNameEXT(vkLogicalDevice, &attachmentNameInfo);
}

// Render Delegate Implementation
// ------------------------------------------------------------

void RenderDelegate::SetDrivers(HdDriverVector const& drivers)
{
    for (const auto& driver : drivers)
    {
        if (driver->name == kTokenRenderContextDriver)
            m_RenderContext = driver->driver.UncheckedGet<RenderContext*>();
    }

    Check(m_RenderContext, "Failed to find the custom Vulkan driver for Hydra.");
}

HdRenderPassSharedPtr RenderDelegate::CreateRenderPass(HdRenderIndex* pRenderIndex, HdRprimCollection const& collection)
{
    spdlog::info("Registering Hydra Renderpass.");
    return HdRenderPassSharedPtr(new RenderPass(pRenderIndex, collection, this));  
}

// Render Pass Implementation
// ------------------------------------------------------------

RenderPass::RenderPass(HdRenderIndex* pRenderIndex, HdRprimCollection const &collection, RenderDelegate* pRenderDelegate) : HdRenderPass(pRenderIndex, collection), m_Owner(pRenderDelegate)
{
    // Grab the render context.
    auto pRenderContext = m_Owner->GetRenderContext();

    // Create Rendering Attachments
    // ------------------------------------------------

    Check(CreateRenderingAttachments(pRenderContext, m_ColorAttachment, m_DepthAttachment), "Failed to create the rendering attachments.");
}

RenderPass::~RenderPass()
{
    // Grab the render context.
    auto pRenderContext = m_Owner->GetRenderContext();

    vkDeviceWaitIdle(pRenderContext->GetDevice());

    vkDestroyImageView(pRenderContext->GetDevice(), m_ColorAttachment.imageView, nullptr);
    vkDestroyImageView(pRenderContext->GetDevice(), m_DepthAttachment.imageView, nullptr);

    vmaDestroyImage(pRenderContext->GetAllocator(), m_ColorAttachment.image, m_ColorAttachment.imageAllocation);
    vmaDestroyImage(pRenderContext->GetAllocator(), m_DepthAttachment.image, m_DepthAttachment.imageAllocation);
}

void RenderPass::_Execute(HdRenderPassStateSharedPtr const& renderPassState, TfTokenVector const &renderTags)
{   
    // Grab the render context.
    auto pRenderContext = m_Owner->GetRenderContext();

    // Grab a handle to the current frame. 
    auto pFrame = m_Owner->GetRenderSetting(kTokenCurrenFrameParams).UncheckedGet<FrameParams*>();

    auto ColorAttachmentBarrier = [&](VkCommandBuffer vkCommand, VkImage vkImage, 
        VkImageLayout         vkLayoutOld, 
        VkImageLayout         vkLayoutNew, 
        VkAccessFlags2        vkAccessSrc,
        VkAccessFlags2        vkAccessDst,
        VkPipelineStageFlags2 vkStageSrc,
        VkPipelineStageFlags2 vkStageDst)
    {
        VkImageSubresourceRange imageSubresource;
        {
            imageSubresource.levelCount     = 1u;
		    imageSubresource.layerCount     = 1u;
            imageSubresource.baseMipLevel   = 0u;
            imageSubresource.baseArrayLayer = 0u;
		    imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        }

        VkImageMemoryBarrier2 vkImageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        {
            vkImageBarrier.image                = vkImage;		
            vkImageBarrier.oldLayout            = vkLayoutOld;
            vkImageBarrier.newLayout            = vkLayoutNew;
            vkImageBarrier.srcAccessMask        = vkAccessSrc;
            vkImageBarrier.dstAccessMask        = vkAccessDst;
            vkImageBarrier.srcStageMask         = vkStageSrc;
            vkImageBarrier.dstStageMask         = vkStageDst;
            vkImageBarrier.srcQueueFamilyIndex  = pRenderContext->GetCommandQueueIndex();
            vkImageBarrier.dstQueueFamilyIndex  = pRenderContext->GetCommandQueueIndex();
            vkImageBarrier.subresourceRange     = imageSubresource;
        }

        VkDependencyInfo vkDependencyInfo = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        {
            vkDependencyInfo.imageMemoryBarrierCount = 1u;
            vkDependencyInfo.pImageMemoryBarriers    = &vkImageBarrier;
        }   
        
        vkCmdPipelineBarrier2(vkCommand, &vkDependencyInfo);
    };

    // Configure Attachments
    // --------------------------------------------

    VkRenderingAttachmentInfo colorAttachmentInfo = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    {
        colorAttachmentInfo.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachmentInfo.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachmentInfo.clearValue  = {{{ 0.25, 0.5, 1.0, 1.0 }}};
        colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachmentInfo.imageView   = m_ColorAttachment.imageView;
    } 

    VkRenderingAttachmentInfo depthAttachmentInfo = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    {
        depthAttachmentInfo.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachmentInfo.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthAttachmentInfo.imageView   = m_DepthAttachment.imageView;
        depthAttachmentInfo.clearValue.depthStencil = { 1.0, 0x0 };
    } 

    // Record
    // --------------------------------------------

    ColorAttachmentBarrier
    (
        pFrame->cmd, m_ColorAttachment.image, 
        VK_IMAGE_LAYOUT_UNDEFINED, 
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 
        VK_ACCESS_2_NONE, 
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, 
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
    );

    VkRenderingInfo vkRenderingInfo = { VK_STRUCTURE_TYPE_RENDERING_INFO };
    {
        vkRenderingInfo.colorAttachmentCount = 1u;
        vkRenderingInfo.pColorAttachments    = &colorAttachmentInfo;
        vkRenderingInfo.pDepthAttachment     = &depthAttachmentInfo;
        vkRenderingInfo.pStencilAttachment   = VK_NULL_HANDLE;
        vkRenderingInfo.layerCount           = 1u;
        vkRenderingInfo.renderArea           = { {0, 0}, { kWindowWidth, kWindowHeight } };
    }
    vkCmdBeginRendering(pFrame->cmd, &vkRenderingInfo);

    vkCmdEndRendering(pFrame->cmd);

    // Copy the internal color attachment to back buffer. 

    ColorAttachmentBarrier
    (
        pFrame->cmd, m_ColorAttachment.image, 
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 
        VK_ACCESS_2_NONE, 
        VK_ACCESS_2_MEMORY_READ_BIT, 
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 
        VK_PIPELINE_STAGE_2_TRANSFER_BIT
    );

    ColorAttachmentBarrier
    (
        pFrame->cmd, pFrame->backBuffer,
        VK_IMAGE_LAYOUT_UNDEFINED, 
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
        VK_ACCESS_2_MEMORY_READ_BIT, 
        VK_ACCESS_2_MEMORY_WRITE_BIT, 
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 
        VK_PIPELINE_STAGE_2_TRANSFER_BIT
    );

    VkImageCopy backBufferCopy = {};
    {
        backBufferCopy.extent         = { kWindowWidth, kWindowHeight, 1u};
        backBufferCopy.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };
        backBufferCopy.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };
    }
    vkCmdCopyImage(pFrame->cmd, m_ColorAttachment.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, pFrame->backBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &backBufferCopy);

    ColorAttachmentBarrier
    (
        pFrame->cmd, pFrame->backBuffer, 
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 
        VK_ACCESS_2_MEMORY_WRITE_BIT, 
        VK_ACCESS_2_MEMORY_READ_BIT, 
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, 
        VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT
    );
}