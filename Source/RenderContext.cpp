#include <Common.h>
#include <RenderContext.h>

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
    vkApplicationInfo.pApplicationName   = "Vulkan Viewport";
    vkApplicationInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    vkApplicationInfo.pEngineName        = "No Engine";
    vkApplicationInfo.engineVersion      = VK_MAKE_VERSION(0, 0, 0);
    vkApplicationInfo.apiVersion         = VK_API_VERSION_1_3;

    std::vector<const char*> requiredInstanceLayers;
#ifdef _DEBUG
    // Just use the Vulkan Configurator for now.
    // requiredInstanceLayers.push_back("VK_LAYER_KHRONOS_validation");
#endif

    uint32_t windowExtensionCount = 0U;
    auto*    pWindowExtensions    = glfwGetRequiredInstanceExtensions(&windowExtensionCount);

    std::vector<const char*> requiredInstanceExtensions;

    for (uint32_t windowExtensionIndex = 0U; windowExtensionIndex < windowExtensionCount; windowExtensionIndex++)
        requiredInstanceExtensions.push_back(pWindowExtensions[windowExtensionIndex]); // NOLINT

#ifdef USE_VK_LABELS
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
        // requiredDeviceExtensions.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
        requiredDeviceExtensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
        requiredDeviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        requiredDeviceExtensions.push_back(VK_EXT_SHADER_OBJECT_EXTENSION_NAME);
        // requiredDeviceExtensions.push_back(VK_KHR_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME);
        requiredDeviceExtensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
        requiredDeviceExtensions.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);

        // Raytracing
        // requiredDeviceExtensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
        // requiredDeviceExtensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        // requiredDeviceExtensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
        // requiredDeviceExtensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
        // requiredDeviceExtensions.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
    }

    Check(SelectVulkanPhysicalDevice(m_VKInstance, requiredDeviceExtensions, m_VKDevicePhysical), "Failed to select a Vulkan Physical Device.");
    Check(GetVulkanQueueIndices(m_VKInstance, m_VKDevicePhysical, m_VKCommandQueueIndex),
          "Failed to obtain the required Vulkan Queue Indices from the physical "
          "device.");
    Check(CreateVulkanLogicalDevice(m_VKDevicePhysical, requiredDeviceExtensions, m_VKCommandQueueIndex, m_VKDeviceLogical),
          "Failed to create a Vulkan Logical Device");

    volkLoadDevice(m_VKDeviceLogical);

    // Create OS Window + Vulkan Swapchain
    // ------------------------------------------------

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    m_Window = glfwCreateWindow(static_cast<int>(width), static_cast<int>(height), "Vulkan Viewport", nullptr, nullptr);
    Check(m_Window != nullptr, "Failed to create the OS Window.");
    Check(glfwCreateWindowSurface(m_VKInstance, m_Window, nullptr, &m_VKSurface), "Failed to create the Vulkan Surface.");

    VkSurfaceCapabilitiesKHR vkSurfaceProperties;
    Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_VKDevicePhysical, m_VKSurface, &vkSurfaceProperties),
          "Failed to obect the Vulkan Surface Properties");

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
    Check(vkGetSwapchainImagesKHR(m_VKDeviceLogical, m_VKSwapchain, &vkSwapchainImageCount, nullptr),
          "Failed to obtain Vulkan Swapchain image count.");

    m_VKSwapchainImages.resize(vkSwapchainImageCount);
    m_VKSwapchainImageViews.resize(vkSwapchainImageCount);

    Check(vkGetSwapchainImagesKHR(m_VKDeviceLogical, m_VKSwapchain, &vkSwapchainImageCount, m_VKSwapchainImages.data()),
          "Failed to obtain the Vulkan Swapchain images.");

    for (uint32_t swapChainIndex = 0U; swapChainIndex < vkSwapchainImageCount; swapChainIndex++)
    {
        auto swapChainName = std::format("Swapchain Image {}", swapChainIndex);
        NameVulkanObject(m_VKDeviceLogical, VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(m_VKSwapchainImages[swapChainIndex]), swapChainName);
    }

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

        Check(vkAllocateCommandBuffers(m_VKDeviceLogical, &vkCommandBufferInfo, &m_VKCommandBuffers.at(frameIndex)),
              "Failed to allocate Vulkan Command Buffers.");

        VkSemaphoreCreateInfo vkSemaphoreInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0x0 };
        VkFenceCreateInfo     vkFenceInfo     = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, VK_FENCE_CREATE_SIGNALED_BIT };

        // Synchronization Primitives
        Check(vkCreateSemaphore(m_VKDeviceLogical, &vkSemaphoreInfo, nullptr, &m_VKImageAvailableSemaphores.at(frameIndex)),
              "Failed to create Vulkan Semaphore.");
        Check(vkCreateSemaphore(m_VKDeviceLogical, &vkSemaphoreInfo, nullptr, &m_VKRenderCompleteSemaphores.at(frameIndex)),
              "Failed to create Vulkan Semaphore.");
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
    vmaAllocatorInfo.flags                  = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT | VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    vmaAllocatorInfo.vulkanApiVersion       = VK_API_VERSION_1_3;
    vmaAllocatorInfo.physicalDevice         = m_VKDevicePhysical;
    vmaAllocatorInfo.device                 = m_VKDeviceLogical;
    vmaAllocatorInfo.instance               = m_VKInstance;
    vmaAllocatorInfo.pVulkanFunctions       = &vmaVulkanFunctions;
    Check(vmaCreateAllocator(&vmaAllocatorInfo, &m_VKMemoryAllocator), "Failed to create Vulkan Memory Allocator.");

    // Create Descriptor Pool
    // ------------------------------------------------

    std::array<VkDescriptorPoolSize, 2> vkDescriptorPoolSizes = {
        { { VK_DESCRIPTOR_TYPE_SAMPLER, 1U }, { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 2048U } }
    };

    VkDescriptorPoolCreateInfo vkDescriptorPoolInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    {
        vkDescriptorPoolInfo.poolSizeCount = static_cast<uint32_t>(vkDescriptorPoolSizes.size());
        vkDescriptorPoolInfo.pPoolSizes    = vkDescriptorPoolSizes.data();
        vkDescriptorPoolInfo.maxSets       = 2048U;
        vkDescriptorPoolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    }
    Check(vkCreateDescriptorPool(m_VKDeviceLogical, &vkDescriptorPoolInfo, VK_NULL_HANDLE, &m_VKDescriptorPool),
          "Failed to create Vulkan Descriptor Pool.");

    // Configure Imgui
    // ------------------------------------------------

    InitializeUserInterface(this);

    // Emit warning in case forgot to add Superluminal DLL.
    // ------------------------------------------------

#ifdef USE_SUPERLUMINAL
    if (!std::filesystem::exists("PerformanceAPI.dll"))
        spdlog::warn("PerformanceAPI.dll was not found in the working directory. Sample instrumentation will not work in Superluminal.");
#endif

    // Done.
    // ------------------------------------------------

    spdlog::info("Initialized Render Context.");
}

RenderContext::~RenderContext()
{
    vkDeviceWaitIdle(m_VKDeviceLogical);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

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

void RenderContext::Dispatch(const std::function<void(FrameParams)>& commandsFunc, const std::function<void()>& interfaceFunc)
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
        Check(vkWaitForFences(m_VKDeviceLogical, 1U, &m_VKInFlightFences.at(frameInFlightIndex), VK_TRUE, UINT64_MAX),
              "Failed to wait for frame fence");

        // Acquire the next swap chain image available.
        uint32_t vkCurrentSwapchainImageIndex = 0U;
        Check(vkAcquireNextImageKHR(m_VKDeviceLogical,
                                    m_VKSwapchain,
                                    UINT64_MAX,
                                    m_VKImageAvailableSemaphores.at(frameInFlightIndex),
                                    VK_NULL_HANDLE,
                                    &vkCurrentSwapchainImageIndex),
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
        FrameParams frameParams = { vkCurrentCommandBuffer,
                                    m_VKSwapchainImages[vkCurrentSwapchainImageIndex],
                                    m_VKSwapchainImageViews[vkCurrentSwapchainImageIndex],
                                    deltaTime.count(),
                                    frameIndex };

        PROFILE_START("Process Frame");

        commandsFunc(frameParams);

        PROFILE_END;

        DrawUserInterface(this, vkCurrentSwapchainImageIndex, vkCurrentCommandBuffer, interfaceFunc);

        // Close command recording.
        Check(vkEndCommandBuffer(vkCurrentCommandBuffer), "Failed to close frame command buffer for recording");

        // Reset the frame fence to re-signal.
        Check(vkResetFences(m_VKDeviceLogical, 1U, &m_VKInFlightFences.at(frameInFlightIndex)), "Failed to reset the frame fence.");

        VkSubmitInfo vkQueueSubmitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };

        VkPipelineStageFlags vkWaitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        vkQueueSubmitInfo.commandBufferCount   = 1U;
        vkQueueSubmitInfo.pCommandBuffers      = &vkCurrentCommandBuffer;
        vkQueueSubmitInfo.waitSemaphoreCount   = 1U;
        vkQueueSubmitInfo.pWaitSemaphores      = &m_VKImageAvailableSemaphores.at(frameInFlightIndex);
        vkQueueSubmitInfo.signalSemaphoreCount = 1U;
        vkQueueSubmitInfo.pSignalSemaphores    = &m_VKRenderCompleteSemaphores.at(frameInFlightIndex);
        vkQueueSubmitInfo.pWaitDstStageMask    = &vkWaitStageMask;

        std::lock_guard<std::mutex> commandQueueLock(GetCommandQueueMutex());

        Check(vkQueueSubmit(m_VKCommandQueue, 1U, &vkQueueSubmitInfo, m_VKInFlightFences.at(frameInFlightIndex)),
              "Failed to submit commands to the Vulkan Graphics Queue.");

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

// Misc. helpers.
// --------------------------------------------------

void RenderContext::CreateCommandPool(VkCommandPool* pCommandPool)
{
    VkCommandPoolCreateInfo vkCommandPoolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    {
        vkCommandPoolInfo.queueFamilyIndex = m_VKCommandQueueIndex;
        vkCommandPoolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    }
    Check(vkCreateCommandPool(m_VKDeviceLogical, &vkCommandPoolInfo, nullptr, pCommandPool), "Failed to create a thread-local Vulkan Command Pool");
}

void RenderContext::CreateStagingBuffer(VkDeviceSize size, Buffer* pStagingBuffer)
{
    VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.usage              = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.size               = size;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage                   = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags                   = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    Check(vmaCreateBuffer(GetAllocator(), &bufferInfo, &allocInfo, &pStagingBuffer->buffer, &pStagingBuffer->bufferAllocation, nullptr),
          "Failed to create staging buffer memory.");
}

void RenderContext::CreateDeviceBufferWithData(CreateDeviceBufferWithDataParams& params)
{
    if (params.pData == nullptr || params.size == 0U)
        return;

    // Create dedicate device memory for the mesh buffer.
    // -----------------------------------------------------

    VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size               = params.size;
    bufferInfo.usage              = params.usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage                   = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    Check(vmaCreateBuffer(GetAllocator(), &bufferInfo, &allocInfo, &params.pBufferDevice->buffer, &params.pBufferDevice->bufferAllocation, nullptr),
          "Failed to create dedicated buffer memory.");

    // Keep information about the buffer.
    params.pBufferDevice->bufferInfo = bufferInfo;

    // Copy Host -> Staging Memory.
    // -----------------------------------------------------

    void* pMappedData = nullptr;
    Check(vmaMapMemory(GetAllocator(), params.pBufferStaging->bufferAllocation, &pMappedData), "Failed to map a pointer to staging memory.");
    {
        // Copy from Host -> Staging memory.
        memcpy(pMappedData, params.pData, params.size);

        vmaUnmapMemory(GetAllocator(), params.pBufferStaging->bufferAllocation);
    }

    // Copy Staging -> Device Memory.
    // -----------------------------------------------------

    VkCommandBuffer vkCommand = VK_NULL_HANDLE;
    SingleShotCommandBegin(this, vkCommand, params.commandPool);
    {
        VmaAllocationInfo allocationInfo;
        vmaGetAllocationInfo(GetAllocator(), params.pBufferDevice->bufferAllocation, &allocationInfo);

        VkBufferCopy copyInfo;
        {
            copyInfo.srcOffset = 0U;
            copyInfo.dstOffset = 0U;
            copyInfo.size      = params.size;
        }
        vkCmdCopyBuffer(vkCommand, params.pBufferStaging->buffer, params.pBufferDevice->buffer, 1U, &copyInfo);
    }
    SingleShotCommandEnd(this, vkCommand);
}

void RenderContext::CreateDeviceImageWithData(CreateDeviceImageWithDataParams& params)
{
    // Handle case where the params are invalid.
    if (params.pData == nullptr || params.info.extent.width == 0 || params.info.extent.height == 0)
        return;

    // Patch the sType if it wasn't set.
    params.info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;

    // Create dedicate device memory for the image
    // -----------------------------------------------------

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage                   = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    Check(vmaCreateImage(GetAllocator(), &params.info, &allocInfo, &params.pImageDevice->image, &params.pImageDevice->imageAllocation, nullptr),
          "Failed to create dedicated image memory.");

    // Create Image View.
    // -----------------------------------------------------

    VkImageViewCreateInfo imageViewInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    {
        imageViewInfo.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        imageViewInfo.image            = params.pImageDevice->image;
        imageViewInfo.format           = params.info.format;
        imageViewInfo.components       = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
        imageViewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0U, 1U, 0U, 1U };
    }
    Check(vkCreateImageView(GetDevice(), &imageViewInfo, nullptr, &params.pImageDevice->imageView), "Failed to create sampled image view.");

    // Copy Host -> Staging Memory.
    // -----------------------------------------------------

    void* pMappedStagingMemory = nullptr;
    Check(vmaMapMemory(GetAllocator(), params.pBufferStaging->bufferAllocation, &pMappedStagingMemory), "Failed to map a pointer to staging memory.");

    // Copy the host image to staging memory.
    memcpy(pMappedStagingMemory, params.pData, params.bytesPerTexel * params.info.extent.width * params.info.extent.height); // NOLINT

    vmaUnmapMemory(GetAllocator(), params.pBufferStaging->bufferAllocation);

    // Copy Staging -> Device Memory.
    // -----------------------------------------------------

    VkCommandBuffer vkCommand = VK_NULL_HANDLE;
    SingleShotCommandBegin(this, vkCommand, params.commandPool);
    {
        VmaAllocationInfo allocationInfo;
        vmaGetAllocationInfo(GetAllocator(), params.pImageDevice->imageAllocation, &allocationInfo);

        VulkanColorImageBarrier(vkCommand,
                                params.pImageDevice->image,
                                VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_ACCESS_2_NONE,
                                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT_KHR,
                                VK_PIPELINE_STAGE_2_TRANSFER_BIT);

        VkBufferImageCopy bufferImageCopyInfo;
        {
            bufferImageCopyInfo.bufferOffset      = 0U;
            bufferImageCopyInfo.bufferImageHeight = 0U;
            bufferImageCopyInfo.bufferRowLength   = 0U;
            bufferImageCopyInfo.imageExtent       = { static_cast<uint32_t>(params.info.extent.width),
                                                      static_cast<uint32_t>(params.info.extent.height),
                                                      1U };
            bufferImageCopyInfo.imageOffset       = { 0U, 0U, 0U };
            bufferImageCopyInfo.imageSubresource  = { VK_IMAGE_ASPECT_COLOR_BIT, 0U, 0U, 1U };
        }

        vkCmdCopyBufferToImage(vkCommand,
                               params.pBufferStaging->buffer,
                               params.pImageDevice->image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1U,
                               &bufferImageCopyInfo);

        VulkanColorImageBarrier(vkCommand,
                                params.pImageDevice->image,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                VK_ACCESS_2_SHADER_READ_BIT,
                                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
    }
    SingleShotCommandEnd(this, vkCommand);
}
