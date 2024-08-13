#include <Common.h>
#include <RenderContext.h>
#include <Scene.h>

RenderContext::RenderContext(uint32_t width, uint32_t height)
{
    Check(glfwInit() != 0, "Failed to initialize GLFW.");

    // Initialize Vulkan
    // ------------------------------------------------

    Check(volkInitialize(), "Failed to initialize volk.");

    // Pass the dynamically loaded function pointer from volk.
    glfwInitVulkanLoader(vkGetInstanceProcAddr);

    Check(glfwVulkanSupported() != 0, "Failed to locate a Vulkan Loader for GLFW.");

    VkApplicationInfo vkApplicationInfo  = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    vkApplicationInfo.pApplicationName   = "Vulkan-Raytraced-Indirect";
    vkApplicationInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    vkApplicationInfo.pEngineName        = "No Engine";
    vkApplicationInfo.engineVersion      = VK_MAKE_VERSION(0, 0, 0);
    vkApplicationInfo.apiVersion         = VK_API_VERSION_1_3;

    std::vector<const char*> requiredInstanceLayers;
#ifdef _DEBUG
    requiredInstanceLayers.push_back("VK_LAYER_KHRONOS_validation");
#endif

    uint32_t windowExtensionCount = 0U;
    auto*    pWindowExtensions    = glfwGetRequiredInstanceExtensions(&windowExtensionCount);

    std::vector<const char*> requiredInstanceExtensions;

    for (uint32_t windowExtensionIndex = 0U; windowExtensionIndex < windowExtensionCount; windowExtensionIndex++)
        requiredInstanceExtensions.push_back(pWindowExtensions[windowExtensionIndex]); // NOLINT

#ifdef _DEBUG
    requiredInstanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    VkInstanceCreateInfo vkInstanceCreateInfo    = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    vkInstanceCreateInfo.pApplicationInfo        = &vkApplicationInfo;
    vkInstanceCreateInfo.enabledLayerCount       = static_cast<uint32_t>(requiredInstanceLayers.size());
    vkInstanceCreateInfo.ppEnabledLayerNames     = requiredInstanceLayers.data();
    vkInstanceCreateInfo.enabledExtensionCount   = static_cast<uint32_t>(requiredInstanceExtensions.size());
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
    Check(GetVulkanQueueIndices(m_VKInstance, m_VKDevicePhysical, m_VKCommandQueueIndex),
          "Failed to obtain the required Vulkan Queue Indices from the physical "
          "device.");
    Check(CreateVulkanLogicalDevice(m_VKDevicePhysical, requiredDeviceExtensions, m_VKCommandQueueIndex, m_VKDeviceLogical), "Failed to create a Vulkan Logical Device");

    volkLoadDevice(m_VKDeviceLogical);

    // Create OS Window + Vulkan Swapchain
    // ------------------------------------------------

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    m_Window = glfwCreateWindow(static_cast<int>(width), static_cast<int>(height), "Vulkan Raytraced Indirect", nullptr, nullptr);
    Check(m_Window != nullptr, "Failed to create the OS Window.");
    Check(glfwCreateWindowSurface(m_VKInstance, m_Window, nullptr, &m_VKSurface), "Failed to create the Vulkan Surface.");

    VkSurfaceCapabilitiesKHR vkSurfaceProperties;
    Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_VKDevicePhysical, m_VKSurface, &vkSurfaceProperties), "Failed to obect the Vulkan Surface Properties");

    VkSwapchainCreateInfoKHR vkSwapchainCreateInfo = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    vkSwapchainCreateInfo.surface                  = m_VKSurface;
    vkSwapchainCreateInfo.minImageCount            = vkSurfaceProperties.minImageCount + 1;
    vkSwapchainCreateInfo.imageExtent              = vkSurfaceProperties.currentExtent;
    vkSwapchainCreateInfo.imageArrayLayers         = vkSurfaceProperties.maxImageArrayLayers;
    vkSwapchainCreateInfo.imageUsage               = vkSurfaceProperties.supportedUsageFlags;
    vkSwapchainCreateInfo.preTransform             = vkSurfaceProperties.currentTransform;
    vkSwapchainCreateInfo.compositeAlpha           = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    vkSwapchainCreateInfo.imageFormat              = VK_FORMAT_R8G8B8A8_UNORM;
    vkSwapchainCreateInfo.imageColorSpace          = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    vkSwapchainCreateInfo.imageSharingMode         = VK_SHARING_MODE_EXCLUSIVE;
    vkSwapchainCreateInfo.presentMode              = VK_PRESENT_MODE_FIFO_KHR;
    vkSwapchainCreateInfo.oldSwapchain             = nullptr;
    vkSwapchainCreateInfo.clipped                  = static_cast<VkBool32>(true);
    Check(vkCreateSwapchainKHR(m_VKDeviceLogical, &vkSwapchainCreateInfo, nullptr, &m_VKSwapchain), "Failed to create the Vulkan Swapchain");

    uint32_t vkSwapchainImageCount = 0U;
    Check(vkGetSwapchainImagesKHR(m_VKDeviceLogical, m_VKSwapchain, &vkSwapchainImageCount, nullptr), "Failed to obtain Vulkan Swapchain image count.");

    m_VKSwapchainImages.resize(vkSwapchainImageCount);
    m_VKSwapchainImageViews.resize(vkSwapchainImageCount);

    Check(vkGetSwapchainImagesKHR(m_VKDeviceLogical, m_VKSwapchain, &vkSwapchainImageCount, m_VKSwapchainImages.data()), "Failed to obtain the Vulkan Swapchain images.");

#ifdef _DEBUG
    for (uint32_t swapChainIndex = 0U; swapChainIndex < vkSwapchainImageCount; swapChainIndex++)
    {
        auto swapChainName = std::format("Swapchain Image {}", swapChainIndex);
        NameVulkanObject(m_VKDeviceLogical, VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(m_VKSwapchainImages[swapChainIndex]), swapChainName);
    }
#endif

    VkImageSubresourceRange vkSwapchainImageSubresourceRange;
    {
        vkSwapchainImageSubresourceRange.levelCount     = 1U;
        vkSwapchainImageSubresourceRange.layerCount     = 1U;
        vkSwapchainImageSubresourceRange.baseMipLevel   = 0U;
        vkSwapchainImageSubresourceRange.baseArrayLayer = 0U;
        vkSwapchainImageSubresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    for (uint32_t imageIndex = 0; imageIndex < vkSwapchainImageCount; imageIndex++)
    {
        // Create an image view which we can render into.
        VkImageViewCreateInfo vkImageViewInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };

        vkImageViewInfo.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        vkImageViewInfo.format           = VK_FORMAT_R8G8B8A8_UNORM;
        vkImageViewInfo.image            = m_VKSwapchainImages[imageIndex];
        vkImageViewInfo.subresourceRange = vkSwapchainImageSubresourceRange;
        vkImageViewInfo.components.r     = VK_COMPONENT_SWIZZLE_R;
        vkImageViewInfo.components.g     = VK_COMPONENT_SWIZZLE_G;
        vkImageViewInfo.components.b     = VK_COMPONENT_SWIZZLE_B;
        vkImageViewInfo.components.a     = VK_COMPONENT_SWIZZLE_A;

        VkImageView vkImageView = VK_NULL_HANDLE;
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
    for (uint32_t frameIndex = 0U; frameIndex < kMaxFramesInFlight; frameIndex++)
    {
        VkCommandBufferAllocateInfo vkCommandBufferInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        {
            vkCommandBufferInfo.commandPool        = m_VKCommandPool;
            vkCommandBufferInfo.commandBufferCount = 1U;
            vkCommandBufferInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        }

        Check(vkAllocateCommandBuffers(m_VKDeviceLogical, &vkCommandBufferInfo, &m_VKCommandBuffers.at(frameIndex)), "Failed to allocate Vulkan Command Buffers.");

        VkSemaphoreCreateInfo vkSemaphoreInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0x0 };
        VkFenceCreateInfo     vkFenceInfo     = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, VK_FENCE_CREATE_SIGNALED_BIT };

        // Synchronization Primitives
        Check(vkCreateSemaphore(m_VKDeviceLogical, &vkSemaphoreInfo, nullptr, &m_VKImageAvailableSemaphores.at(frameIndex)), "Failed to create Vulkan Semaphore.");
        Check(vkCreateSemaphore(m_VKDeviceLogical, &vkSemaphoreInfo, nullptr, &m_VKRenderCompleteSemaphores.at(frameIndex)), "Failed to create Vulkan Semaphore.");
        Check(vkCreateFence(m_VKDeviceLogical, &vkFenceInfo, nullptr, &m_VKInFlightFences.at(frameIndex)), "Failed to create Vulkan Fence.");
    }

    // Obtain Queues (just Graphics for now).
    // ------------------------------------------------

    vkGetDeviceQueue(m_VKDeviceLogical, m_VKCommandQueueIndex, 0U, &m_VKCommandQueue);

    // Create Memory Allocator
    // ------------------------------------------------

    VmaVulkanFunctions vmaVulkanFunctions    = {};
    vmaVulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vmaVulkanFunctions.vkGetDeviceProcAddr   = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo vmaAllocatorInfo = {};
    vmaAllocatorInfo.flags                  = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
    vmaAllocatorInfo.vulkanApiVersion       = VK_API_VERSION_1_3;
    vmaAllocatorInfo.physicalDevice         = m_VKDevicePhysical;
    vmaAllocatorInfo.device                 = m_VKDeviceLogical;
    vmaAllocatorInfo.instance               = m_VKInstance;
    vmaAllocatorInfo.pVulkanFunctions       = &vmaVulkanFunctions;
    Check(vmaCreateAllocator(&vmaAllocatorInfo, &m_VKMemoryAllocator), "Failed to create Vulkan Memory Allocator.");

    // Create Descriptor Pool
    // ------------------------------------------------

    std::array<VkDescriptorPoolSize, 2> vkDescriptorPoolSizes = {
        { { VK_DESCRIPTOR_TYPE_SAMPLER, 1U }, { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 128U } }
    };

    VkDescriptorPoolCreateInfo vkDescriptorPoolInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    {
        vkDescriptorPoolInfo.poolSizeCount = static_cast<uint32_t>(vkDescriptorPoolSizes.size());
        vkDescriptorPoolInfo.pPoolSizes    = vkDescriptorPoolSizes.data();
        vkDescriptorPoolInfo.maxSets       = 256U;
        vkDescriptorPoolInfo.flags         = 0x0;
    }
    Check(vkCreateDescriptorPool(m_VKDeviceLogical, &vkDescriptorPoolInfo, VK_NULL_HANDLE, &m_VKDescriptorPool), "Failed to create Vulkan Descriptor Pool.");

    m_Scene = std::make_unique<Scene>();

    // Done.
    // ------------------------------------------------

    spdlog::info("Initialized Render Context.");
}

RenderContext::~RenderContext()
{
    glfwDestroyWindow(m_Window);
    glfwTerminate();

    vmaDestroyAllocator(m_VKMemoryAllocator);

    for (uint32_t frameIndex = 0U; frameIndex < kMaxFramesInFlight; frameIndex++)
    {
        vkDestroySemaphore(m_VKDeviceLogical, m_VKImageAvailableSemaphores.at(frameIndex), nullptr);
        vkDestroySemaphore(m_VKDeviceLogical, m_VKRenderCompleteSemaphores.at(frameIndex), nullptr);
        vkDestroyFence(m_VKDeviceLogical, m_VKInFlightFences.at(frameIndex), nullptr);
    }

    for (auto& vkImageView : m_VKSwapchainImageViews)
        vkDestroyImageView(m_VKDeviceLogical, vkImageView, nullptr);

    vkDestroyDescriptorPool(m_VKDeviceLogical, m_VKDescriptorPool, nullptr);
    vkDestroyCommandPool(m_VKDeviceLogical, m_VKCommandPool, nullptr);
    vkDestroySwapchainKHR(m_VKDeviceLogical, m_VKSwapchain, nullptr);
    vkDestroyDevice(m_VKDeviceLogical, nullptr);
    vkDestroySurfaceKHR(m_VKInstance, m_VKSurface, nullptr);
    vkDestroyInstance(m_VKInstance, nullptr);
}

void RenderContext::Dispatch(const std::function<void(FrameParams)>& commandsFunc)
{
    uint64_t frameIndex = 0U;

    // Initialize delta time.
    auto deltaTime = std::chrono::duration<double>(0.0);

    // Render-loop
    // ------------------------------------------------

    while (glfwWindowShouldClose(m_Window) == 0)
    {
        // Sample the time at the beginning of the frame.
        auto frameTimeBegin = std::chrono::high_resolution_clock::now();

        // Determine frame-in-flight index.
        uint32_t frameInFlightIndex = frameIndex % kMaxFramesInFlight;

        // Wait for the current frame fence to be signaled.
        Check(vkWaitForFences(m_VKDeviceLogical, 1U, &m_VKInFlightFences.at(frameInFlightIndex), VK_TRUE, UINT64_MAX), "Failed to wait for frame fence");

        // Acquire the next swap chain image available.
        uint32_t vkCurrentSwapchainImageIndex = 0U;
        Check(vkAcquireNextImageKHR(m_VKDeviceLogical, m_VKSwapchain, UINT64_MAX, m_VKImageAvailableSemaphores.at(frameInFlightIndex), VK_NULL_HANDLE, &vkCurrentSwapchainImageIndex),
              "Failed to acquire swapchain image.");

        // Get the current frame's command buffer.
        auto& vkCurrentCommandBuffer = m_VKCommandBuffers.at(frameInFlightIndex);

        // Clear previous work.
        Check(vkResetCommandBuffer(vkCurrentCommandBuffer, 0x0), "Failed to reset frame command buffer");

        // Open command recording.
        VkCommandBufferBeginInfo vkCommandBufferBeginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        {
            vkCommandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        }
        Check(vkBeginCommandBuffer(vkCurrentCommandBuffer, &vkCommandBufferBeginInfo), "Failed to open frame command buffer for recording");

        // Dispatch command recording.
        FrameParams frameParams = { vkCurrentCommandBuffer, m_VKSwapchainImages[vkCurrentSwapchainImageIndex], m_VKSwapchainImageViews[vkCurrentSwapchainImageIndex], deltaTime.count() };

        commandsFunc(frameParams);

        // Close command recording.
        Check(vkEndCommandBuffer(vkCurrentCommandBuffer), "Failed to close frame command buffer for recording");

        // Reset the frame fence to re-signal.
        Check(vkResetFences(m_VKDeviceLogical, 1U, &m_VKInFlightFences.at(frameInFlightIndex)), "Failed to reset the frame fence.");

        VkSubmitInfo vkQueueSubmitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        {
            VkPipelineStageFlags vkWaitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

            vkQueueSubmitInfo.commandBufferCount   = 1U;
            vkQueueSubmitInfo.pCommandBuffers      = &vkCurrentCommandBuffer;
            vkQueueSubmitInfo.waitSemaphoreCount   = 1U;
            vkQueueSubmitInfo.pWaitSemaphores      = &m_VKImageAvailableSemaphores.at(frameInFlightIndex);
            vkQueueSubmitInfo.signalSemaphoreCount = 1U;
            vkQueueSubmitInfo.pSignalSemaphores    = &m_VKRenderCompleteSemaphores.at(frameInFlightIndex);
            vkQueueSubmitInfo.pWaitDstStageMask    = &vkWaitStageMask;
        }
        Check(vkQueueSubmit(m_VKCommandQueue, 1U, &vkQueueSubmitInfo, m_VKInFlightFences.at(frameInFlightIndex)), "Failed to submit commands to the Vulkan Graphics Queue.");

        VkPresentInfoKHR vkQueuePresentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        {
            vkQueuePresentInfo.waitSemaphoreCount = 1U;
            vkQueuePresentInfo.pWaitSemaphores    = &m_VKRenderCompleteSemaphores.at(frameInFlightIndex);
            vkQueuePresentInfo.swapchainCount     = 1U;
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
