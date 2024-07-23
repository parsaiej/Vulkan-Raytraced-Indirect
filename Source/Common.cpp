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
