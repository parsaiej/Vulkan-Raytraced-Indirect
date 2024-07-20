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

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/gtc/matrix_transform.hpp>

const uint32_t kWindowWidth  = 1920u;
const uint32_t kWindowHeight = 1080u;

enum ShaderID
{
    FullscreenTriangleVert,
    LitFrag,
    MeshVert
};

struct PushConstants
{
    glm::mat4 _MatrixVP;
    glm::mat4 _MatrixM;
    float     _Time;
};

struct Vertex
{
    glm::vec3 positionOS;
    glm::vec3 normalOS;
    glm::vec2 texCoord0;
};

const uint32_t kMaxFramesInFlight = 3u;

#ifdef _DEBUG
    void Check(VkResult a, const char* b) { if (a != VK_SUCCESS) { spdlog::critical(b); __debugbreak(); exit(a); } }
    void Check(bool     a, const char* b) { if (a != true)       { spdlog::critical(b); __debugbreak(); exit(1); } }
#else
    void Check(VkResult a, const char* b) { if (a != VK_SUCCESS) { spdlog::critical(b); exit(a); } }
    void Check(bool     a, const char* b) { if (a != true)       { spdlog::critical(b); exit(1); } }
#endif

// Collection of vulkan primitives to hold the current frame state. 
// ---------------------------------------------------------

struct FrameParams
{
    VkCommandBuffer cmd;
    VkImage         backBuffer;
    VkImageView     backBufferView;
};

// Forward-declared RAII utility for boilerplat vulkan initializations.
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

struct SceneParseContext
{
    RenderContext* pRenderContext;

    // Device-uploaded resources.
    std::vector<std::pair<VkBuffer, VmaAllocation>>& dedicatedMemoryIndices;
    std::vector<std::pair<VkBuffer, VmaAllocation>>& dedicatedMemoryVertices;
};

// Forward-declared utilities. 
// ---------------------------------------------------------

bool CreatePhysicallyBasedMaterialDescriptorLayout(const VkDevice& vkLogicalDevice, VkDescriptorSetLayout& vkDescriptorSetLayout);
bool SelectVulkanPhysicalDevice(const VkInstance& vkInstance, const std::vector<const char*> requiredExtensions, VkPhysicalDevice& vkPhysicalDevice);
bool CreateVulkanLogicalDevice(const VkPhysicalDevice& vkPhysicalDevice, const std::vector<const char*>& requiredExtensions, uint32_t vkGraphicsQueueIndex, VkDevice& vkLogicalDevice);
bool LoadByteCode(const char* filePath, std::vector<char>& byteCode);
void SetDefaultRenderState(VkCommandBuffer commandBuffer);
bool GetVulkanQueueIndices(const VkInstance& vkInstance, const VkPhysicalDevice& vkPhysicalDevice, uint32_t& vkQueueIndexGraphics);
bool ParseScene(const char* pFilePath, SceneParseContext& context);
void GetVertexInputLayout(std::vector<VkVertexInputBindingDescription2EXT>& bindings, std::vector<VkVertexInputAttributeDescription2EXT>& attributes);

// Executable implementation.
// ---------------------------------------------------------

int main()
{
    std::unique_ptr<RenderContext> pRenderContext = std::make_unique<RenderContext>(kWindowWidth, kWindowHeight);
    
    // Configure Descriptor Set Layouts
    // --------------------------------------

    VkDescriptorSetLayout vkDescriptorSetLayout;
    Check(CreatePhysicallyBasedMaterialDescriptorLayout(pRenderContext->GetDevice(), vkDescriptorSetLayout), "Failed to create a Vulkan Descriptor Set Layout for Physically Based Materials.");

    // Load Scene
    // ------------------------------------------------

    std::vector<std::pair<VkBuffer, VmaAllocation>> dedicatedMemoryIndices;
    std::vector<std::pair<VkBuffer, VmaAllocation>> dedicatedMemoryVertices;

    SceneParseContext sceneContext =
    {
        pRenderContext.get(),

        // Resources
        dedicatedMemoryIndices,
        dedicatedMemoryVertices
    };

    // Check(ParseScene("..\\Assets\\sponza-dabrovic\\sponza.obj", sceneContext), "Failed to read the input scene.");
    Check(ParseScene("..\\Assets\\bunny\\bunny.obj", sceneContext), "Failed to read the input scene.");

    // Shader Creation Utility
    // ------------------------------------------------

    std::map<ShaderID, VkShaderEXT> vkShaderMap;

    auto LoadShader = [&](ShaderID shaderID, const char* filePath, VkShaderCreateInfoEXT vkShaderInfo)
    {
        std::vector<char> shaderByteCode;
        if (!LoadByteCode(filePath, shaderByteCode))
        {
            spdlog::critical("Failed to read shader bytecode: {}", filePath);
            exit(1);
        }

        vkShaderInfo.pName    = "Main";
        vkShaderInfo.pCode    = shaderByteCode.data();
        vkShaderInfo.codeSize = shaderByteCode.size();
        vkShaderInfo.codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT;

        VkShaderEXT vkShader;
        Check(vkCreateShadersEXT(pRenderContext->GetDevice(), 1u, &vkShaderInfo, nullptr, &vkShader), std::format("Failed to load Vulkan Shader: {}", filePath).c_str());

        if (vkShaderMap.contains(shaderID))
        {
            spdlog::critical("Tried to store a Vulkan Shader into an existing shader slot.");
            exit(1);
        }

        spdlog::info("Loaded Vulkan Shader: {}", filePath);

        vkShaderMap[shaderID] = vkShader;
    };

    // Configure Push Constants
    // --------------------------------------

    VkPushConstantRange vkPushConstants;
    {
        vkPushConstants.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        vkPushConstants.offset     = 0u;
        vkPushConstants.size       = sizeof(PushConstants);
    }

    // Configure Pipeline Layouts
    // --------------------------------------
    
    VkPipelineLayoutCreateInfo vkPipelineLayoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    {
        vkPipelineLayoutInfo.setLayoutCount         = 1u;
        vkPipelineLayoutInfo.pSetLayouts            = &vkDescriptorSetLayout;
        vkPipelineLayoutInfo.pushConstantRangeCount = 1u;
        vkPipelineLayoutInfo.pPushConstantRanges    = &vkPushConstants;
    }
    VkPipelineLayout vkPipelineLayout;
    Check(vkCreatePipelineLayout(pRenderContext->GetDevice(), &vkPipelineLayoutInfo, nullptr, &vkPipelineLayout), "Failed to create the default Vulkan Pipeline Layout");
    
    // Create Shader Objects
    // --------------------------------------

    VkShaderCreateInfoEXT vertexShaderInfo = { VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT };
    {
        vertexShaderInfo.stage     = VK_SHADER_STAGE_VERTEX_BIT;
        vertexShaderInfo.nextStage = VK_SHADER_STAGE_FRAGMENT_BIT;
        
        vertexShaderInfo.pushConstantRangeCount = 1u;
        vertexShaderInfo.pPushConstantRanges    = &vkPushConstants;
        vertexShaderInfo.setLayoutCount         = 0u;
        vertexShaderInfo.pSetLayouts            = nullptr;
    }
    LoadShader(ShaderID::FullscreenTriangleVert, "FullscreenTriangle.vert.spv", vertexShaderInfo);
    LoadShader(ShaderID::MeshVert,               "Mesh.vert.spv"              , vertexShaderInfo);
    
    VkShaderCreateInfoEXT litShaderInfo = { VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT };
    {
        litShaderInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        
        litShaderInfo.pushConstantRangeCount = 1u;
        litShaderInfo.pPushConstantRanges    = &vkPushConstants;
    }
    LoadShader(ShaderID::LitFrag, "Lit.frag.spv", litShaderInfo);

    float globalTime = 0.0;

    // Vertex Input Layout
    // ------------------------------------------------

    std::vector<VkVertexInputBindingDescription2EXT>   vertexInputBindings;
    std::vector<VkVertexInputAttributeDescription2EXT> vertexInputAttributes;
    GetVertexInputLayout(vertexInputBindings, vertexInputAttributes);

    // Configure Push Constants
    // ------------------------------------------------

    PushConstants pushConstants;
    {
        pushConstants._MatrixM  = glm::identity<glm::mat4>();
        pushConstants._MatrixVP = glm::perspectiveFov(glm::radians(80.0f), 1920.0f, 1080.0f, 0.01f, 1000.0f) * glm::lookAt(glm::vec3(0, 0, 2), glm::vec3(0, 1, 0), glm::vec3(0, -1, 0));
        pushConstants._Time     = 1.0;
    }

    // Command Recording
    // ------------------------------------------------

    auto RecordCommands = [&](FrameParams frameParams)
    {
        // Configure Resource Barriers
        // --------------------------------------------

        VkImageMemoryBarrier2 vkImageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        {
            vkImageBarrier.image                           = frameParams.backBuffer;		
            vkImageBarrier.srcQueueFamilyIndex             = pRenderContext->GetCommandQueueIndex();
            vkImageBarrier.dstQueueFamilyIndex             = pRenderContext->GetCommandQueueIndex();
            vkImageBarrier.subresourceRange.levelCount     = 1u;
		    vkImageBarrier.subresourceRange.layerCount     = 1u;
            vkImageBarrier.subresourceRange.baseMipLevel   = 0u;
            vkImageBarrier.subresourceRange.baseArrayLayer = 0u;
		    vkImageBarrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        }

        VkDependencyInfo vkDependencyInfo = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        {
            vkDependencyInfo.imageMemoryBarrierCount = 1u;
            vkDependencyInfo.pImageMemoryBarriers    = &vkImageBarrier;
        }

        // Configure Attachments
        // --------------------------------------------

        VkRenderingAttachmentInfo colorAttachmentInfo = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        {
            colorAttachmentInfo.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachmentInfo.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachmentInfo.clearValue  = {{{ 0.0, 0.0, 0.0, 1.0 }}};
            colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachmentInfo.imageView   = frameParams.backBufferView;
        } 

        // Record
        // --------------------------------------------

        {   
            vkImageBarrier.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
            vkImageBarrier.newLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            vkImageBarrier.srcAccessMask = VK_ACCESS_2_NONE;
            vkImageBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            vkImageBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            vkImageBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        }
        vkCmdPipelineBarrier2(frameParams.cmd, &vkDependencyInfo);

        VkRenderingInfo vkRenderingInfo = { VK_STRUCTURE_TYPE_RENDERING_INFO };
        {
            vkRenderingInfo.colorAttachmentCount = 1u;
            vkRenderingInfo.pColorAttachments    = &colorAttachmentInfo;
            vkRenderingInfo.pDepthAttachment     = VK_NULL_HANDLE;
            vkRenderingInfo.pStencilAttachment   = VK_NULL_HANDLE;
            vkRenderingInfo.layerCount           = 1u;
            vkRenderingInfo.renderArea           = { {0, 0}, { kWindowWidth, kWindowHeight } };
        }
        vkCmdBeginRendering(frameParams.cmd, &vkRenderingInfo);

        VkShaderStageFlagBits vkGraphicsShaderStageBits[5] =
        {
            VK_SHADER_STAGE_VERTEX_BIT,
            VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
            VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
            VK_SHADER_STAGE_GEOMETRY_BIT,
            VK_SHADER_STAGE_FRAGMENT_BIT
        };

        VkShaderEXT vkGraphicsShaders[5] =
        {
            vkShaderMap[ShaderID::MeshVert],
            VK_NULL_HANDLE,
            VK_NULL_HANDLE,
            VK_NULL_HANDLE,
            vkShaderMap[ShaderID::LitFrag]
        };

        vkCmdBindShadersEXT(frameParams.cmd, 5u, vkGraphicsShaderStageBits, vkGraphicsShaders);

        SetDefaultRenderState(frameParams.cmd);

        vkCmdPushConstants(frameParams.cmd, vkPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0u, sizeof(PushConstants), &pushConstants);

        vkCmdSetVertexInputEXT(frameParams.cmd, (uint32_t)vertexInputBindings.size(), vertexInputBindings.data(), (uint32_t)vertexInputAttributes.size(), vertexInputAttributes.data());

        for (uint32_t instanceIndex = 0u; instanceIndex < sceneContext.dedicatedMemoryIndices.size(); instanceIndex++)
        {
            const auto& indexBuffer  = sceneContext.dedicatedMemoryIndices[instanceIndex];
            const auto& vertexBuffer = sceneContext.dedicatedMemoryVertices[instanceIndex];

            VmaAllocationInfo allocationInfo;
            vmaGetAllocationInfo(pRenderContext->GetAllocator(), indexBuffer.second, &allocationInfo);

            vkCmdBindIndexBuffer(frameParams.cmd, indexBuffer.first, 0u, VK_INDEX_TYPE_UINT32);

            VkDeviceSize vertexBufferOffset = 0u;
            vkCmdBindVertexBuffers(frameParams.cmd, 0u, 1u, &vertexBuffer.first, &vertexBufferOffset);
            
            vkCmdDrawIndexed(frameParams.cmd, (uint32_t)allocationInfo.size / sizeof(uint32_t), 1u, 0u, 0u, 0u);
        }

        vkCmdEndRendering(frameParams.cmd);

        {
            vkImageBarrier.oldLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            vkImageBarrier.newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            vkImageBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            vkImageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
            vkImageBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            vkImageBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        }
        vkCmdPipelineBarrier2(frameParams.cmd, &vkDependencyInfo);
    };
    
    // Kick off render-loop.
    // ------------------------------------------------

    pRenderContext->Dispatch(RecordCommands);

    // Release
    // ------------------------------------------------

    vkDeviceWaitIdle(pRenderContext->GetDevice());

    for (auto& indexBuffer : sceneContext.dedicatedMemoryIndices)
        vmaDestroyBuffer(pRenderContext->GetAllocator(), indexBuffer.first, indexBuffer.second);

    for (auto& vertexBuffer : sceneContext.dedicatedMemoryVertices)
        vmaDestroyBuffer(pRenderContext->GetAllocator(), vertexBuffer.first, vertexBuffer.second);

    vkDestroyDescriptorSetLayout(pRenderContext->GetDevice(), vkDescriptorSetLayout, nullptr);
    vkDestroyPipelineLayout     (pRenderContext->GetDevice(), vkPipelineLayout,      nullptr);

    for (auto& shader : vkShaderMap)
        vkDestroyShaderEXT(pRenderContext->GetDevice(), shader.second, nullptr);

    return 0;
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

    VkInstanceCreateInfo vkInstanceCreateInfo    = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    vkInstanceCreateInfo.pApplicationInfo        = &vkApplicationInfo;
    vkInstanceCreateInfo.enabledLayerCount       = (uint32_t)requiredInstanceLayers.size();
    vkInstanceCreateInfo.ppEnabledLayerNames     = requiredInstanceLayers.data();
    vkInstanceCreateInfo.enabledExtensionCount   = windowExtensionCount;
    vkInstanceCreateInfo.ppEnabledExtensionNames = windowExtensions;
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
            m_VKSwapchainImageViews[vkCurrentSwapchainImageIndex]
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
    vkCmdSetDepthTestEnableEXT        (commandBuffer, VK_FALSE);
    vkCmdSetDepthBiasEnableEXT        (commandBuffer, VK_FALSE);
    vkCmdSetDepthWriteEnableEXT       (commandBuffer, VK_FALSE);
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
    
    tinyobj::ObjReader reader;

    if (!reader.ParseFromFile(pFilePath))
        return false;

    if (!reader.Warning().empty())
        spdlog::warn("[tinyobj] [{}]:\n\n{}", pFilePath, reader.Warning());

    const auto& shapes    = reader.GetShapes();
    const auto& attrib    = reader.GetAttrib();
    const auto& materials = reader.GetMaterials();

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