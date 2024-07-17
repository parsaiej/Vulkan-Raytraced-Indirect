#define VOLK_IMPLEMENTATION
#include <volk.h>

#include <spdlog/spdlog.h>
#include <GLFW/glfw3.h>

const uint32_t kWindowWidth  = 1920u;
const uint32_t kWindowHeight = 1080u;

// Target 60Hz. 
constexpr const std::chrono::duration<double> kTargetFrameDuration(1.0 / 60.0);

const uint32_t kMaxFramesInFlight = 3u;

void Check(VkResult a, const char* b) { if (a != VK_SUCCESS) { spdlog::critical(b); exit(1); } }
void Check(bool     a, const char* b) { if (a != true)       { spdlog::critical(b); exit(1); } }

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

    VkPhysicalDeviceVulkan13Features vulkan13Features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    VkPhysicalDeviceVulkan12Features vulkan12Features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    VkPhysicalDeviceVulkan11Features vulkan11Features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
    VkPhysicalDeviceFeatures2        vulkan10Features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2          };

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
    }

    VkDeviceCreateInfo vkLogicalDeviceCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    vkLogicalDeviceCreateInfo.pNext                   = &vulkan10Features;
    vkLogicalDeviceCreateInfo.pQueueCreateInfos       = &vkGraphicsQueueCreateInfo;
    vkLogicalDeviceCreateInfo.queueCreateInfoCount    = 1u;
    vkLogicalDeviceCreateInfo.enabledExtensionCount   = (uint32_t)requiredExtensions.size();
    vkLogicalDeviceCreateInfo.ppEnabledExtensionNames = requiredExtensions.data();

    return vkCreateDevice(vkPhysicalDevice, &vkLogicalDeviceCreateInfo, nullptr, &vkLogicalDevice) == VK_SUCCESS;
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

int main()
{
    Check(glfwInit(), "Failed to initialize GLFW.");

    // Initialize Vulkan
    // ------------------------------------------------

    Check(volkInitialize(), "Failed to initialize volk.");

    // Pass the dynamically loaded function pointer from volk. 
    glfwInitVulkanLoader(vkGetInstanceProcAddr);

    Check(glfwVulkanSupported(), "Failed to locate a Vulkan Loader for GLFW.");

    VkApplicationInfo vkApplicationInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    vkApplicationInfo.pApplicationName   = "SharedMemory-Vulkan-D3D11";
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

    VkInstance vkInstance;
    Check(vkCreateInstance(&vkInstanceCreateInfo, nullptr, &vkInstance), "Failed to create the Vulkan Instance.");

    volkLoadInstanceOnly(vkInstance);

    std::vector<const char*> requiredDeviceExtensions;
    {
        requiredDeviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        requiredDeviceExtensions.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
        requiredDeviceExtensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    }

    VkPhysicalDevice vkPhysicalDevice;
    Check(SelectVulkanPhysicalDevice(vkInstance, requiredDeviceExtensions, vkPhysicalDevice), "Failed to select a Vulkan Physical Device.");

    uint32_t vkQueueIndexGraphics;
    Check(GetVulkanQueueIndices(vkInstance, vkPhysicalDevice, vkQueueIndexGraphics), "Failed to obtain the required Vulkan Queue Indices from the physical device.");
    
    VkDevice vkLogicalDevice;
    Check(CreateVulkanLogicalDevice(vkPhysicalDevice, requiredDeviceExtensions, vkQueueIndexGraphics, vkLogicalDevice), "Failed to create a Vulkan Logical Device");
 
    volkLoadDevice(vkLogicalDevice);

    // Create OS Window + Vulkan Swapchain
    // ------------------------------------------------

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto pWindow = glfwCreateWindow(kWindowWidth, kWindowHeight, "Vulkan Raytraced Indirect", NULL, NULL);

    if (!pWindow)
    {
        spdlog::critical("Failed to create the OS Window.");
        return 1;
    }

    VkSurfaceKHR vkSurface;
    Check(glfwCreateWindowSurface(vkInstance, pWindow, NULL, &vkSurface), "Failed to create the Vulkan Surface.");

	VkSurfaceCapabilitiesKHR vkSurfaceProperties;
	Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vkPhysicalDevice, vkSurface, &vkSurfaceProperties), "Failed to obect the Vulkan Surface Properties");

    VkSwapchainCreateInfoKHR vkSwapchainCreateInfo = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    vkSwapchainCreateInfo.surface          = vkSurface;
    vkSwapchainCreateInfo.minImageCount    = vkSurfaceProperties.minImageCount;
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

    VkSwapchainKHR vkSwapchain;
    Check(vkCreateSwapchainKHR(vkLogicalDevice, &vkSwapchainCreateInfo, nullptr, &vkSwapchain), "Failed to create the Vulkan Swapchain");

	uint32_t vkSwapchainImageCount;
	Check(vkGetSwapchainImagesKHR(vkLogicalDevice, vkSwapchain, &vkSwapchainImageCount, nullptr), "Failed to obtain Vulkan Swapchain image count.");

	std::vector<VkImage>     vkSwapchainImages     (vkSwapchainImageCount);
    std::vector<VkImageView> vkSwapchainImageViews (vkSwapchainImageCount);

	Check(vkGetSwapchainImagesKHR(vkLogicalDevice, vkSwapchain, &vkSwapchainImageCount, vkSwapchainImages.data()), "Failed to obtain the Vulkan Swapchain images.");

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
		vkImageViewInfo.image                       = vkSwapchainImages[imageIndex];
        vkImageViewInfo.subresourceRange            = vkSwapchainImageSubresourceRange;
		vkImageViewInfo.components.r                = VK_COMPONENT_SWIZZLE_R;
		vkImageViewInfo.components.g                = VK_COMPONENT_SWIZZLE_G;
		vkImageViewInfo.components.b                = VK_COMPONENT_SWIZZLE_B;
		vkImageViewInfo.components.a                = VK_COMPONENT_SWIZZLE_A;

		VkImageView vkImageView;
		Check(vkCreateImageView(vkLogicalDevice, &vkImageViewInfo, nullptr, &vkImageView), "Failed to create a Swapchain Image View.");

        vkSwapchainImageViews[imageIndex] = vkImageView;
	}

    VkCommandPoolCreateInfo vkCommandPoolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    {
        vkCommandPoolInfo.queueFamilyIndex = vkQueueIndexGraphics;
        vkCommandPoolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    }
    VkCommandPool vkCommandPool;
    Check(vkCreateCommandPool(vkLogicalDevice, &vkCommandPoolInfo, nullptr, &vkCommandPool), "Failed to create a Vulkan Command Pool");

    // Per-frame resources. 
    VkCommandBuffer vkCommandBuffers           [kMaxFramesInFlight];
    VkSemaphore     vkImageAvailableSemaphores [kMaxFramesInFlight];
    VkSemaphore     vkRenderCompleteSemaphores [kMaxFramesInFlight];
    VkFence         vkInFlightFences           [kMaxFramesInFlight];

    for (uint32_t frameIndex = 0u; frameIndex < kMaxFramesInFlight; frameIndex++)
    {
        VkCommandBufferAllocateInfo vkCommandBufferInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        {
            vkCommandBufferInfo.commandPool        = vkCommandPool;
            vkCommandBufferInfo.commandBufferCount = 1u;
            vkCommandBufferInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        }

        Check(vkAllocateCommandBuffers(vkLogicalDevice, &vkCommandBufferInfo, &vkCommandBuffers[frameIndex]), "Failed to allocate Vulkan Command Buffers.");
        
        VkSemaphoreCreateInfo vkSemaphoreInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0x0                          };
        VkFenceCreateInfo     vkFenceInfo     = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,     nullptr, VK_FENCE_CREATE_SIGNALED_BIT };

        // Synchronization Primitives
        Check(vkCreateSemaphore (vkLogicalDevice, &vkSemaphoreInfo, NULL, &vkImageAvailableSemaphores[frameIndex]), "Failed to create Vulkan Semaphore.");
        Check(vkCreateSemaphore (vkLogicalDevice, &vkSemaphoreInfo, NULL, &vkRenderCompleteSemaphores[frameIndex]), "Failed to create Vulkan Semaphore.");
        Check(vkCreateFence     (vkLogicalDevice, &vkFenceInfo,     NULL, &vkInFlightFences[frameIndex]),           "Failed to create Vulkan Fence.");
    }

    VkQueue vkGraphicsQueue;
    vkGetDeviceQueue(vkLogicalDevice, vkQueueIndexGraphics, 0u, &vkGraphicsQueue);

    spdlog::info("Initialized Vulkan.");

    // Command Recording
    // ------------------------------------------------

    auto RecordCommands = [&](VkCommandBuffer vkCurrentCmd, uint32_t vkCurrentSwapchainImageIndex)
    {
        spdlog::info("Recording command buffer...");

        // Configure Resource Barriers
        // --------------------------------------------

        VkImageMemoryBarrier2 vkImageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        {
            vkImageBarrier.image               = vkSwapchainImages[vkCurrentSwapchainImageIndex];
            vkImageBarrier.subresourceRange    = vkSwapchainImageSubresourceRange;
            vkImageBarrier.srcQueueFamilyIndex = vkQueueIndexGraphics;
            vkImageBarrier.dstQueueFamilyIndex = vkQueueIndexGraphics;
        }

        VkDependencyInfo vkDependencyInfo = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        {
            vkDependencyInfo.imageMemoryBarrierCount = 1u;
            vkDependencyInfo.pImageMemoryBarriers    = &vkImageBarrier;
        }

        // Record
        // --------------------------------------------

        {   
            vkImageBarrier.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
            vkImageBarrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            vkImageBarrier.srcAccessMask = VK_ACCESS_2_NONE;
            vkImageBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkImageBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            vkImageBarrier.dstStageMask  = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        vkCmdPipelineBarrier2(vkCurrentCmd, &vkDependencyInfo);

        VkClearColorValue clearColor = { { 0.25, 0.5, 1.0, 1.0 } };
        vkCmdClearColorImage(vkCurrentCmd, vkSwapchainImages[vkCurrentSwapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1u, &vkSwapchainImageSubresourceRange);

        {
            vkImageBarrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            vkImageBarrier.newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            vkImageBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkImageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
            vkImageBarrier.srcStageMask  = VK_PIPELINE_STAGE_TRANSFER_BIT;
            vkImageBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        }
        vkCmdPipelineBarrier2(vkCurrentCmd, &vkDependencyInfo);
    };

    // Render-loop
    // ------------------------------------------------

    uint64_t frameIndex = 0u;

    // Initialize delta time.
    auto deltaTime = kTargetFrameDuration;

    while (!glfwWindowShouldClose(pWindow))
    {
        // Determine frame-in-flight index.
        uint32_t frameInFlightIndex = frameIndex % kMaxFramesInFlight;

        // Sample the time at the beginning of the frame.
        auto frameTimeBegin = std::chrono::high_resolution_clock::now();

        // Wait for the current frame fence to be signaled.
        Check(vkWaitForFences(vkLogicalDevice, 1u, &vkInFlightFences[frameInFlightIndex], VK_TRUE, UINT64_MAX), "Failed to wait for frame fence");

        // Acquire the next swap chain image available.
        uint32_t vkCurrentSwapchainImageIndex;
        Check(vkAcquireNextImageKHR(vkLogicalDevice, vkSwapchain, UINT64_MAX, vkImageAvailableSemaphores[frameInFlightIndex], VK_NULL_HANDLE, &vkCurrentSwapchainImageIndex), "Failed to acquire swapchain image.");

        // Get the current frame's command buffer.
        auto& vkCurrentCommandBuffer = vkCommandBuffers[frameInFlightIndex];

        // Clear previous work. 
        Check(vkResetCommandBuffer(vkCurrentCommandBuffer, 0x0), "Failed to reset frame command buffer");

        // Open command recording.
        VkCommandBufferBeginInfo vkCommandBufferBeginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        {
            vkCommandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        }
        Check(vkBeginCommandBuffer(vkCurrentCommandBuffer, &vkCommandBufferBeginInfo), "Failed to open frame command buffer for recording");

        // Dispatch command recording.
        RecordCommands(vkCurrentCommandBuffer, vkCurrentSwapchainImageIndex);

        // Close command recording.
        Check(vkEndCommandBuffer(vkCurrentCommandBuffer), "Failed to close frame command buffer for recording");

        // Reset the frame fence to re-signal. 
        Check(vkResetFences(vkLogicalDevice, 1u, &vkInFlightFences[frameInFlightIndex]), "Failed to reset the frame fence.");

        VkSubmitInfo vkQueueSubmitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        {
            VkPipelineStageFlags vkWaitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

            vkQueueSubmitInfo.commandBufferCount   = 1u;
            vkQueueSubmitInfo.pCommandBuffers      = &vkCurrentCommandBuffer;
            vkQueueSubmitInfo.waitSemaphoreCount   = 1u;
            vkQueueSubmitInfo.pWaitSemaphores      = &vkImageAvailableSemaphores[frameInFlightIndex];
            vkQueueSubmitInfo.signalSemaphoreCount = 1u;
            vkQueueSubmitInfo.pSignalSemaphores    = &vkRenderCompleteSemaphores[frameInFlightIndex];
            vkQueueSubmitInfo.pWaitDstStageMask    = &vkWaitStageMask;
        }
        Check(vkQueueSubmit(vkGraphicsQueue, 1u, &vkQueueSubmitInfo, vkInFlightFences[frameInFlightIndex]), "Failed to submit commands to the Vulkan Graphics Queue.");

        VkPresentInfoKHR vkQueuePresentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        {
            vkQueuePresentInfo.waitSemaphoreCount = 1u;
            vkQueuePresentInfo.pWaitSemaphores    = &vkRenderCompleteSemaphores[frameInFlightIndex];
            vkQueuePresentInfo.swapchainCount     = 1u;
            vkQueuePresentInfo.pSwapchains        = &vkSwapchain;
            vkQueuePresentInfo.pImageIndices      = &vkCurrentSwapchainImageIndex;
        }
        Check(vkQueuePresentKHR(vkGraphicsQueue, &vkQueuePresentInfo), "Failed to submit image to the Vulkan Presentation Engine.");

        // Sample the time at the end of the frame.
        auto frameTimeEnd = std::chrono::high_resolution_clock::now();

        // Update delta time.
        deltaTime = frameTimeEnd - frameTimeBegin;
        
        // Advance to the next frame. 
        frameIndex++;

        if (kTargetFrameDuration - deltaTime > std::chrono::duration<double>::zero())
            std::this_thread::sleep_for(kTargetFrameDuration - deltaTime);

        glfwPollEvents();
    }

    // Release
    // ------------------------------------------------

    glfwDestroyWindow(pWindow);
    glfwTerminate();

    vkDeviceWaitIdle(vkLogicalDevice);

    for (uint32_t frameIndex = 0u; frameIndex < kMaxFramesInFlight; frameIndex++)
    {
        vkDestroySemaphore (vkLogicalDevice, vkImageAvailableSemaphores[frameIndex], nullptr);
        vkDestroySemaphore (vkLogicalDevice, vkRenderCompleteSemaphores[frameIndex], nullptr);
        vkDestroyFence     (vkLogicalDevice, vkInFlightFences[frameIndex],           nullptr);
    }

    for (auto& vkImageView : vkSwapchainImageViews)
        vkDestroyImageView(vkLogicalDevice, vkImageView, nullptr);

    vkDestroyCommandPool  (vkLogicalDevice, vkCommandPool, nullptr);
    vkDestroySwapchainKHR (vkLogicalDevice, vkSwapchain,   nullptr);
    vkDestroyDevice       (vkLogicalDevice,                nullptr);
    vkDestroySurfaceKHR   (vkInstance,      vkSurface,     nullptr);
    vkDestroyInstance     (vkInstance,                     nullptr);

    return 0;
}