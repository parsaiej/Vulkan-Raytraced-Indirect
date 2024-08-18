#include <Common.h>
#include <RenderContext.h>

// Utilities Implementation
// ------------------------------------------------------------

bool CreateRenderingAttachments(RenderContext* pRenderContext, Image& colorAttachment, Image& depthAttachment)
{
    auto CreateAttachment = [&](Image& attachment, VkFormat imageFormat, VkImageUsageFlags imageUsageFlags, VkImageAspectFlags imageAspect)
    {
        VkImageCreateInfo imageInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        {
            imageInfo.imageType     = VK_IMAGE_TYPE_2D;
            imageInfo.arrayLayers   = 1U;
            imageInfo.format        = imageFormat;
            imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.usage         = imageUsageFlags;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.extent        = { kWindowWidth, kWindowHeight, 1 };
            imageInfo.mipLevels     = 1U;
            imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.flags         = 0x0;
        }

        VmaAllocationCreateInfo imageAllocInfo = {};
        {
            imageAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        }

        Check(vmaCreateImage(pRenderContext->GetAllocator(),
                             &imageInfo,
                             &imageAllocInfo,
                             &attachment.image,
                             &attachment.imageAllocation,
                             VK_NULL_HANDLE),
              "Failed to create attachment allocation.");

        VkImageViewCreateInfo imageViewInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        {
            imageViewInfo.image                           = attachment.image;
            imageViewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
            imageViewInfo.format                          = imageFormat;
            imageViewInfo.subresourceRange.levelCount     = 1U;
            imageViewInfo.subresourceRange.layerCount     = 1U;
            imageViewInfo.subresourceRange.baseMipLevel   = 0U;
            imageViewInfo.subresourceRange.baseArrayLayer = 0U;
            imageViewInfo.subresourceRange.aspectMask     = imageAspect;
        }
        Check(vkCreateImageView(pRenderContext->GetDevice(), &imageViewInfo, nullptr, &attachment.imageView), "Failed to create attachment view.");
    };

    CreateAttachment(colorAttachment,
                     VK_FORMAT_R8G8B8A8_UNORM,
                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                     VK_IMAGE_ASPECT_COLOR_BIT);
    CreateAttachment(depthAttachment, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);

    DebugLabelImageResource(pRenderContext, colorAttachment, "Color Attachment");
    DebugLabelImageResource(pRenderContext, depthAttachment, "Depth Attachment");

    // Transition the color resource

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    SingleShotCommandBegin(pRenderContext, cmd);

    VulkanColorImageBarrier(cmd,
                            colorAttachment.image,
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            VK_ACCESS_2_NONE,
                            VK_ACCESS_2_MEMORY_READ_BIT,
                            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                            VK_PIPELINE_STAGE_2_TRANSFER_BIT);

    SingleShotCommandEnd(pRenderContext, cmd);

    return true;
}

bool CreatePhysicallyBasedMaterialDescriptorLayout(const VkDevice& vkLogicalDevice, VkDescriptorSetLayout& vkDescriptorSetLayout)
{
    std::array<VkDescriptorSetLayoutBinding, 5U> vkDescriptorSetLayoutBindings = {

        // Albedo
        VkDescriptorSetLayoutBinding(0U, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1U, VK_SHADER_STAGE_FRAGMENT_BIT, VK_NULL_HANDLE),

        // Normal
        VkDescriptorSetLayoutBinding(1U, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1U, VK_SHADER_STAGE_FRAGMENT_BIT, VK_NULL_HANDLE),

        // Metallic
        VkDescriptorSetLayoutBinding(2U, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1U, VK_SHADER_STAGE_FRAGMENT_BIT, VK_NULL_HANDLE),

        // Roughness
        VkDescriptorSetLayoutBinding(3U, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1U, VK_SHADER_STAGE_FRAGMENT_BIT, VK_NULL_HANDLE),

        // Sampler
        VkDescriptorSetLayoutBinding(4U, VK_DESCRIPTOR_TYPE_SAMPLER, 1U, VK_SHADER_STAGE_FRAGMENT_BIT, VK_NULL_HANDLE)
    };

    VkDescriptorSetLayoutCreateInfo vkDescriptorSetLayoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    {
        // Define a generic descriptor layout for PBR materials.
        vkDescriptorSetLayoutInfo.flags        = 0x0;
        vkDescriptorSetLayoutInfo.bindingCount = static_cast<uint32_t>(vkDescriptorSetLayoutBindings.size());
        vkDescriptorSetLayoutInfo.pBindings    = vkDescriptorSetLayoutBindings.data();
    }

    return vkCreateDescriptorSetLayout(vkLogicalDevice, &vkDescriptorSetLayoutInfo, nullptr, &vkDescriptorSetLayout) == VK_SUCCESS;
}

bool SelectVulkanPhysicalDevice(const VkInstance& vkInstance, const std::vector<const char*>& requiredExtensions, VkPhysicalDevice& vkPhysicalDevice)
{
    uint32_t deviceCount = 0U;
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
    uint32_t supportedDeviceExtensionCount = 0U;
    vkEnumerateDeviceExtensionProperties(vkPhysicalDevice, nullptr, &supportedDeviceExtensionCount, nullptr);

    std::vector<VkExtensionProperties> supportedDeviceExtensions(supportedDeviceExtensionCount);
    vkEnumerateDeviceExtensionProperties(vkPhysicalDevice, nullptr, &supportedDeviceExtensionCount, supportedDeviceExtensions.data());

    auto CheckExtension = [&](const char* extensionName)
    {
        for (const auto& deviceExtension : supportedDeviceExtensions)
        {
            if (strcmp(deviceExtension.extensionName, extensionName) == 0) // NOLINT
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

bool CreateVulkanLogicalDevice(const VkPhysicalDevice&         vkPhysicalDevice,
                               const std::vector<const char*>& requiredExtensions,
                               uint32_t                        vkGraphicsQueueIndex,
                               VkDevice&                       vkLogicalDevice)
{
    float graphicsQueuePriority = 1.0;

    VkDeviceQueueCreateInfo vkGraphicsQueueCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    vkGraphicsQueueCreateInfo.queueFamilyIndex        = vkGraphicsQueueIndex;
    vkGraphicsQueueCreateInfo.queueCount              = 1U;
    vkGraphicsQueueCreateInfo.pQueuePriorities        = &graphicsQueuePriority;

    VkPhysicalDeviceShaderObjectFeaturesEXT shaderObjectFeature = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT };
    VkPhysicalDeviceVulkan13Features        vulkan13Features    = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    VkPhysicalDeviceVulkan12Features        vulkan12Features    = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    VkPhysicalDeviceVulkan11Features        vulkan11Features    = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
    VkPhysicalDeviceFeatures2               vulkan10Features    = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };

    vulkan13Features.pNext = &shaderObjectFeature;
    vulkan12Features.pNext = &vulkan13Features;
    vulkan11Features.pNext = &vulkan12Features;
    vulkan10Features.pNext = &vulkan11Features;

    // Query for supported features.
    vkGetPhysicalDeviceFeatures2(vkPhysicalDevice, &vulkan10Features);

    for (const auto& requiredExtension : requiredExtensions)
    {
        if ((strcmp(requiredExtension, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME) == 0) && vulkan12Features.timelineSemaphore != VK_TRUE)
            return false;

        if ((strcmp(requiredExtension, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME) == 0) && vulkan13Features.synchronization2 != VK_TRUE)
            return false;

        if ((strcmp(requiredExtension, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) == 0) && vulkan13Features.dynamicRendering != VK_TRUE)
            return false;

        if ((strcmp(requiredExtension, VK_EXT_SHADER_OBJECT_EXTENSION_NAME) == 0) && shaderObjectFeature.shaderObject != VK_TRUE)
            return false;
    }

    VkDeviceCreateInfo vkLogicalDeviceCreateInfo      = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    vkLogicalDeviceCreateInfo.pNext                   = &vulkan10Features;
    vkLogicalDeviceCreateInfo.pQueueCreateInfos       = &vkGraphicsQueueCreateInfo;
    vkLogicalDeviceCreateInfo.queueCreateInfoCount    = 1U;
    vkLogicalDeviceCreateInfo.enabledExtensionCount   = static_cast<uint32_t>(requiredExtensions.size());
    vkLogicalDeviceCreateInfo.ppEnabledExtensionNames = requiredExtensions.data();

    return vkCreateDevice(vkPhysicalDevice, &vkLogicalDeviceCreateInfo, nullptr, &vkLogicalDevice) == VK_SUCCESS;
}

bool LoadByteCode(const char* filePath, std::vector<char>& byteCode)
{
    std::fstream file(std::format(R"(..\Shaders\Compiled\{})", filePath), std::ios::in | std::ios::binary);

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
    static VkColorComponentFlags s_DefaultWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    static VkColorBlendEquationEXT s_DefaultColorBlend = {
        // Color
        VK_BLEND_FACTOR_ONE,
        VK_BLEND_FACTOR_ZERO,
        VK_BLEND_OP_ADD,

        // Alpha
        VK_BLEND_FACTOR_ONE,
        VK_BLEND_FACTOR_ZERO,
        VK_BLEND_OP_ADD,
    };

    static VkBool32   s_DefaultBlendEnable = VK_FALSE;
    static VkViewport s_DefaultViewport    = { 0, kWindowHeight, kWindowWidth, -static_cast<int32_t>(kWindowHeight), 0.0, 1.0 };
    static VkRect2D   s_DefaultScissor     = {
        {            0,             0 },
        { kWindowWidth, kWindowHeight }
    };
    static VkSampleMask s_DefaultSampleMask = 0xFFFFFFFF;

    vkCmdSetColorBlendEnableEXT(commandBuffer, 0U, 1U, &s_DefaultBlendEnable);
    vkCmdSetColorWriteMaskEXT(commandBuffer, 0U, 1U, &s_DefaultWriteMask);
    vkCmdSetColorBlendEquationEXT(commandBuffer, 0U, 1U, &s_DefaultColorBlend);
    vkCmdSetViewportWithCountEXT(commandBuffer, 1U, &s_DefaultViewport);
    vkCmdSetScissorWithCountEXT(commandBuffer, 1U, &s_DefaultScissor);
    vkCmdSetPrimitiveRestartEnableEXT(commandBuffer, VK_FALSE);
    vkCmdSetRasterizerDiscardEnableEXT(commandBuffer, VK_FALSE);
    vkCmdSetAlphaToOneEnableEXT(commandBuffer, VK_FALSE);
    vkCmdSetAlphaToCoverageEnableEXT(commandBuffer, VK_FALSE);
    vkCmdSetStencilTestEnableEXT(commandBuffer, VK_FALSE);
    vkCmdSetDepthBiasEnableEXT(commandBuffer, VK_FALSE);
    vkCmdSetDepthTestEnableEXT(commandBuffer, VK_TRUE);
    vkCmdSetDepthWriteEnableEXT(commandBuffer, VK_TRUE);
    vkCmdSetDepthCompareOpEXT(commandBuffer, VK_COMPARE_OP_LESS_OR_EQUAL);
    vkCmdSetDepthBoundsTestEnable(commandBuffer, VK_FALSE);
    vkCmdSetDepthClampEnableEXT(commandBuffer, VK_FALSE);
    vkCmdSetLogicOpEnableEXT(commandBuffer, VK_FALSE);
    vkCmdSetRasterizationSamplesEXT(commandBuffer, VK_SAMPLE_COUNT_1_BIT);
    vkCmdSetSampleMaskEXT(commandBuffer, VK_SAMPLE_COUNT_1_BIT, &s_DefaultSampleMask);
    vkCmdSetFrontFaceEXT(commandBuffer, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    vkCmdSetPolygonModeEXT(commandBuffer, VK_POLYGON_MODE_FILL);
    vkCmdSetCullModeEXT(commandBuffer, VK_CULL_MODE_NONE);
    vkCmdSetPrimitiveTopologyEXT(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
}

bool GetVulkanQueueIndices(const VkInstance& vkInstance, const VkPhysicalDevice& vkPhysicalDevice, uint32_t& vkQueueIndexGraphics)
{
    vkQueueIndexGraphics = UINT_MAX;

    uint32_t queueFamilyCount = 0U;
    vkGetPhysicalDeviceQueueFamilyProperties(vkPhysicalDevice, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(vkPhysicalDevice, &queueFamilyCount, queueFamilyProperties.data());

    for (uint32_t queueFamilyIndex = 0; queueFamilyIndex < queueFamilyCount; queueFamilyIndex++)
    {
        if (glfwGetPhysicalDevicePresentationSupport(vkInstance, vkPhysicalDevice, queueFamilyIndex) == 0)
            continue;

        if ((queueFamilyProperties[queueFamilyIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0U)
            continue;

        vkQueueIndexGraphics = queueFamilyIndex;

        break;
    }

    return vkQueueIndexGraphics != UINT_MAX;
}

void GetVertexInputLayout(std::vector<VkVertexInputBindingDescription2EXT>& bindings, std::vector<VkVertexInputAttributeDescription2EXT>& attributes)
{
    VkVertexInputBindingDescription2EXT binding = { VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT };

    {
        binding.binding   = 0U;
        binding.stride    = sizeof(GfVec3f);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        binding.divisor   = 1U;
    }
    bindings.push_back(binding);

    {
        binding.binding   = 1U;
        binding.stride    = sizeof(GfVec3f);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        binding.divisor   = 1U;
    }
    bindings.push_back(binding);

    {
        binding.binding   = 2U;
        binding.stride    = sizeof(GfVec2f);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        binding.divisor   = 1U;
    }
    bindings.push_back(binding);

    VkVertexInputAttributeDescription2EXT attribute = { VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT };

    // Position
    {
        attribute.binding  = 0U;
        attribute.location = 0U;
        attribute.offset   = 0U;
        attribute.format   = VK_FORMAT_R32G32B32_SFLOAT;
    }
    attributes.push_back(attribute);

    // Normal
    {
        attribute.binding  = 1U;
        attribute.location = 1U;
        attribute.offset   = 0U;
        attribute.format   = VK_FORMAT_R32G32B32_SFLOAT;
    }
    attributes.push_back(attribute);

    // Texcoord
    {
        attribute.binding  = 2U;
        attribute.location = 2U;
        attribute.offset   = 0;
        attribute.format   = VK_FORMAT_R32G32_SFLOAT;
    }
    attributes.push_back(attribute);
}

void NameVulkanObject(VkDevice vkLogicalDevice, VkObjectType vkObjectType, uint64_t vkObject, const std::string& vkObjectName)
{
#ifdef _DEBUG
    VkDebugUtilsObjectNameInfoEXT attachmentNameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };

    attachmentNameInfo.pObjectName  = vkObjectName.c_str();
    attachmentNameInfo.objectType   = vkObjectType;
    attachmentNameInfo.objectHandle = vkObject;

    vkSetDebugUtilsObjectNameEXT(vkLogicalDevice, &attachmentNameInfo);
#endif
}

void VulkanColorImageBarrier(VkCommandBuffer       vkCommand,
                             VkImage               vkImage,
                             VkImageLayout         vkLayoutOld,
                             VkImageLayout         vkLayoutNew,
                             VkAccessFlags2        vkAccessSrc,
                             VkAccessFlags2        vkAccessDst,
                             VkPipelineStageFlags2 vkStageSrc,
                             VkPipelineStageFlags2 vkStageDst)
{
    VkImageMemoryBarrier2 vkImageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    {
        vkImageBarrier.image               = vkImage;
        vkImageBarrier.oldLayout           = vkLayoutOld;
        vkImageBarrier.newLayout           = vkLayoutNew;
        vkImageBarrier.srcAccessMask       = vkAccessSrc;
        vkImageBarrier.dstAccessMask       = vkAccessDst;
        vkImageBarrier.srcStageMask        = vkStageSrc;
        vkImageBarrier.dstStageMask        = vkStageDst;
        vkImageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        vkImageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        vkImageBarrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0U, 1U, 0U, 1U };
    }

    VkDependencyInfo vkDependencyInfo = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    {
        vkDependencyInfo.imageMemoryBarrierCount = 1U;
        vkDependencyInfo.pImageMemoryBarriers    = &vkImageBarrier;
    }

    vkCmdPipelineBarrier2(vkCommand, &vkDependencyInfo);
}

void DebugLabelImageResource(RenderContext* pRenderContext, const Image& imageResource, const char* labelName)
{
#ifdef _DEBUG
    // Label the allocation.
    vmaSetAllocationName(pRenderContext->GetAllocator(), imageResource.imageAllocation, std::format("Image Alloc - [{}]", labelName).c_str());

    // Label the image object.
    NameVulkanObject(pRenderContext->GetDevice(),
                     VK_OBJECT_TYPE_IMAGE,
                     reinterpret_cast<uint64_t>(imageResource.image),
                     std::format("Image - [{}]", labelName));

    if (imageResource.imageView != VK_NULL_HANDLE)
    {
        // Label the image view object.
        NameVulkanObject(pRenderContext->GetDevice(),
                         VK_OBJECT_TYPE_IMAGE_VIEW,
                         reinterpret_cast<uint64_t>(imageResource.imageView),
                         std::format("Image View - [{}]", labelName));
    }
#endif
}

void DebugLabelBufferResource(RenderContext* pRenderContext, const Buffer& bufferResource, const char* labelName)
{
#ifdef _DEBUG
    // Label the allocation.
    vmaSetAllocationName(pRenderContext->GetAllocator(), bufferResource.bufferAllocation, std::format("Buffer Alloc - [{}]", labelName).c_str());

    // Label the buffer object.
    NameVulkanObject(pRenderContext->GetDevice(),
                     VK_OBJECT_TYPE_BUFFER,
                     reinterpret_cast<uint64_t>(bufferResource.buffer),
                     std::format("Buffer - [{}]", labelName));

    if (bufferResource.bufferView != VK_NULL_HANDLE)
    {
        // Label the image view object.
        NameVulkanObject(pRenderContext->GetDevice(),
                         VK_OBJECT_TYPE_BUFFER_VIEW,
                         reinterpret_cast<uint64_t>(bufferResource.bufferView),
                         std::format("Buffer View - [{}]", labelName));
    }
#endif
}

void SingleShotCommandBegin(RenderContext* pRenderContext, VkCommandBuffer& vkCommandBuffer)
{
    VkCommandBufferAllocateInfo vkCommandAllocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    {
        vkCommandAllocateInfo.commandBufferCount = 1U;
        vkCommandAllocateInfo.commandPool        = pRenderContext->GetCommandPool();
    }

    Check(vkAllocateCommandBuffers(pRenderContext->GetDevice(), &vkCommandAllocateInfo, &vkCommandBuffer), "Failed to created command buffer");

    VkCommandBufferBeginInfo vkCommandsBeginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    {
        vkCommandsBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    }
    Check(vkBeginCommandBuffer(vkCommandBuffer, &vkCommandsBeginInfo), "Failed to begin recording commands");
}

void SingleShotCommandEnd(RenderContext* pRenderContext, VkCommandBuffer& vkCommandBuffer)
{
    Check(vkEndCommandBuffer(vkCommandBuffer), "Failed to end recording commands");

    VkSubmitInfo vkSubmitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    {
        vkSubmitInfo.commandBufferCount = 1U;
        vkSubmitInfo.pCommandBuffers    = &vkCommandBuffer;
    }
    Check(vkQueueSubmit(pRenderContext->GetCommandQueue(), 1U, &vkSubmitInfo, VK_NULL_HANDLE), "Failed to submit commands to the graphics queue.");

    // Wait for the commands to complete.
    // -----------------------------------------------------
    Check(vkDeviceWaitIdle(pRenderContext->GetDevice()), "Failed to wait for commands to finish dispatching.");
}

void InitializeUserInterface(RenderContext* pRenderContext)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(pRenderContext->GetWindow(), true);

    VkPipelineRenderingCreateInfo pipelineRenderingInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };

    std::vector<VkFormat> colorFormats(1U, VK_FORMAT_R8G8B8A8_UNORM);
    pipelineRenderingInfo.colorAttachmentCount    = static_cast<uint32_t>(colorFormats.size());
    pipelineRenderingInfo.pColorAttachmentFormats = colorFormats.data();

    ImGui_ImplVulkan_InitInfo imguiVulkanInitInfo = {};
    {
        imguiVulkanInitInfo.Instance                    = pRenderContext->GetInstance();
        imguiVulkanInitInfo.PhysicalDevice              = pRenderContext->GetDevicePhysical();
        imguiVulkanInitInfo.Device                      = pRenderContext->GetDevice();
        imguiVulkanInitInfo.QueueFamily                 = pRenderContext->GetCommandQueueIndex();
        imguiVulkanInitInfo.Queue                       = pRenderContext->GetCommandQueue();
        imguiVulkanInitInfo.DescriptorPool              = pRenderContext->GetDescriptorPool();
        imguiVulkanInitInfo.MinImageCount               = 3U;
        imguiVulkanInitInfo.ImageCount                  = 3U;
        imguiVulkanInitInfo.MSAASamples                 = VK_SAMPLE_COUNT_1_BIT;
        imguiVulkanInitInfo.UseDynamicRendering         = VK_TRUE;
        imguiVulkanInitInfo.PipelineRenderingCreateInfo = pipelineRenderingInfo;
    }
    ImGui_ImplVulkan_Init(&imguiVulkanInitInfo);
}

void DrawUserInterface(RenderContext* pRenderContext, uint32_t swapChainImageIndex, VkCommandBuffer cmd)
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Dispatch UI commands
    ImGui::ShowDemoWindow();

    ImGui::Render();

    VulkanColorImageBarrier(cmd,
                            pRenderContext->GetSwapchainImage(swapChainImageIndex),
                            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_ACCESS_2_MEMORY_READ_BIT,
                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

    VkRenderingAttachmentInfo colorAttachmentInfo = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    {
        colorAttachmentInfo.loadOp      = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachmentInfo.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachmentInfo.imageView   = pRenderContext->GetSwapchainImageView(swapChainImageIndex);
    }

    VkRenderingInfo vkRenderingInfo = { VK_STRUCTURE_TYPE_RENDERING_INFO };
    {
        vkRenderingInfo.colorAttachmentCount = 1U;
        vkRenderingInfo.pColorAttachments    = &colorAttachmentInfo;
        vkRenderingInfo.pDepthAttachment     = VK_NULL_HANDLE;
        vkRenderingInfo.pStencilAttachment   = VK_NULL_HANDLE;
        vkRenderingInfo.layerCount           = 1U;
        vkRenderingInfo.renderArea           = {
            {            0,             0 },
            { kWindowWidth, kWindowHeight }
        };
    }
    vkCmdBeginRendering(cmd, &vkRenderingInfo);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

    vkCmdEndRendering(cmd);

    VulkanColorImageBarrier(cmd,
                            pRenderContext->GetSwapchainImage(swapChainImageIndex),
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_ACCESS_2_MEMORY_READ_BIT,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
}
