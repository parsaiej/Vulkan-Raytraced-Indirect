#include <Mesh.h>
#include <RenderContext.h>
#include <RenderDelegate.h>
#include <RenderPass.h>
#include <ResourceRegistry.h>
#include <Material.h>

// Shader Creation Utility
// ------------------------------------------------

void RenderPass::LoadShader(ShaderID shaderID, const char* filePath, const char* entryName, VkShaderCreateInfoEXT vkShaderInfo)
{
    // Grab the render context.
    auto* pRenderContext = m_Owner->GetRenderContext();

    std::vector<char> shaderByteCode;
    Check(LoadByteCode(filePath, shaderByteCode), std::format("Failed to read shader bytecode: {}", filePath).c_str());

    vkShaderInfo.pName    = entryName;
    vkShaderInfo.pCode    = shaderByteCode.data();
    vkShaderInfo.codeSize = shaderByteCode.size();
    vkShaderInfo.codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT;

    VkShaderEXT vkShader = VK_NULL_HANDLE;
    Check(vkCreateShadersEXT(pRenderContext->GetDevice(), 1U, &vkShaderInfo, nullptr, &vkShader),
          std::format("Failed to load Vulkan Shader: {}", filePath).c_str());
    Check(!m_ShaderMap.contains(shaderID), "Tried to store a Vulkan Shader into an existing shader slot.");

    spdlog::info("Loaded Vulkan Shader: {}", filePath);

    m_ShaderMap[shaderID] = vkShader;
};

void RenderPass::VisibilityPassCreate(RenderContext* pRenderContext)
{
    // Pipeline Layout
    // --------------------------------------

    VkPushConstantRange pushConstantRange;
    {
        pushConstantRange.offset     = 0U;
        pushConstantRange.size       = sizeof(VisibilityPushConstants);
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkPipelineLayoutCreateInfo pipelineInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    {
        pipelineInfo.pushConstantRangeCount = 1U;
        pipelineInfo.pPushConstantRanges    = &pushConstantRange;
    }
    Check(vkCreatePipelineLayout(pRenderContext->GetDevice(), &pipelineInfo, nullptr, &m_VisibilityPipelineLayout),
          "Failed to create pipeline layout for visibility pipeline.");

    // Shaders
    // --------------------------------------

    VkShaderCreateInfoEXT vertexShaderInfo = { VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT };
    {
        vertexShaderInfo.stage     = VK_SHADER_STAGE_VERTEX_BIT;
        vertexShaderInfo.nextStage = VK_SHADER_STAGE_FRAGMENT_BIT;

        vertexShaderInfo.pushConstantRangeCount = 1U;
        vertexShaderInfo.pPushConstantRanges    = &pushConstantRange;
    }
    LoadShader(ShaderID::VisibilityVert, "Visibility.vert.spv", "Vert", vertexShaderInfo);

    VkShaderCreateInfoEXT visShaderInfo = { VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT };
    {
        visShaderInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;

        visShaderInfo.pushConstantRangeCount = 1U;
        visShaderInfo.pPushConstantRanges    = &pushConstantRange;
    }
    LoadShader(ShaderID::VisibilityFrag, "Visibility.frag.spv", "Frag", visShaderInfo);

    // Vertex Input Layout
    // ------------------------------------------------

    VkVertexInputBindingDescription2EXT binding = { VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT };

    {
        binding.binding   = 0U;
        binding.stride    = sizeof(GfVec3f);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        binding.divisor   = 1U;
    }
    m_VertexInputBindings.push_back(binding);

    VkVertexInputAttributeDescription2EXT attribute = { VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT };

    {
        attribute.binding  = 0U;
        attribute.location = 0U;
        attribute.offset   = 0U;
        attribute.format   = VK_FORMAT_R32G32B32_SFLOAT;
    }
    m_VertexInputAttributes.push_back(attribute);

    // Create Visibility Buffer
    // ------------------------------------------------

    VkImageCreateInfo visibilityBufferInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    {
        visibilityBufferInfo.imageType     = VK_IMAGE_TYPE_2D;
        visibilityBufferInfo.arrayLayers   = 1U;
        visibilityBufferInfo.format        = VK_FORMAT_R32_UINT;
        visibilityBufferInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
        visibilityBufferInfo.usage         = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        visibilityBufferInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        visibilityBufferInfo.extent        = { kWindowWidth, kWindowHeight, 1 };
        visibilityBufferInfo.mipLevels     = 1U;
        visibilityBufferInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        visibilityBufferInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        visibilityBufferInfo.flags         = 0x0;
    }

    VmaAllocationCreateInfo imageAllocInfo = {};
    {
        imageAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    }

    Check(vmaCreateImage(pRenderContext->GetAllocator(),
                         &visibilityBufferInfo,
                         &imageAllocInfo,
                         &m_VisibilityBuffer.image,
                         &m_VisibilityBuffer.imageAllocation,
                         VK_NULL_HANDLE),
          "Failed to create attachment allocation.");

    VkImageViewCreateInfo imageViewInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    {
        imageViewInfo.image                           = m_VisibilityBuffer.image;
        imageViewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        imageViewInfo.format                          = visibilityBufferInfo.format;
        imageViewInfo.subresourceRange.levelCount     = 1U;
        imageViewInfo.subresourceRange.layerCount     = 1U;
        imageViewInfo.subresourceRange.baseMipLevel   = 0U;
        imageViewInfo.subresourceRange.baseArrayLayer = 0U;
        imageViewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    }
    Check(vkCreateImageView(pRenderContext->GetDevice(), &imageViewInfo, nullptr, &m_VisibilityBuffer.imageView),
          "Failed to create attachment view.");

    // Initialize barrier for visibility.

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    SingleShotCommandBegin(pRenderContext, cmd);

    VulkanColorImageBarrier(cmd,
                            m_VisibilityBuffer.image,
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_GENERAL,
                            VK_ACCESS_2_NONE,
                            VK_ACCESS_2_MEMORY_READ_BIT,
                            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);

    SingleShotCommandEnd(pRenderContext, cmd);
}

void RenderPass::MaterialPassCreate(RenderContext* pRenderContext)
{
    // Allocate buffers
    // ----------------------------------------

    auto CreateDeviceBuffer = [&](Buffer& buffer, uint32_t size, VkBufferUsageFlags usage)
    {
        VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.size               = size;
        bufferInfo.usage              = usage;

        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage                   = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        Check(vmaCreateBuffer(pRenderContext->GetAllocator(), &bufferInfo, &allocInfo, &buffer.buffer, &buffer.bufferAllocation, nullptr),
              "Failed to create dedicated buffer memory.");
    };

    const uint32_t kMaxMaterial = 4096U;

    CreateDeviceBuffer(m_MaterialCountBuffer, sizeof(uint32_t) * kMaxMaterial, VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR);
    CreateDeviceBuffer(m_MaterialOffsetBuffer, sizeof(uint32_t) * kMaxMaterial, VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR);
    CreateDeviceBuffer(m_MaterialPixelBuffer, sizeof(GfVec2f) * kWindowWidth * kWindowHeight, VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR);
}

void RenderPass::DebugPassCreate(RenderContext* pRenderContext)
{
    // Descriptor Layout
    // --------------------------------------

    std::vector<VkDescriptorSetLayoutBinding> descriptorLayoutBindings;
    {
        descriptorLayoutBindings.push_back(
            VkDescriptorSetLayoutBinding(0U, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1U, VK_SHADER_STAGE_FRAGMENT_BIT, VK_NULL_HANDLE));
        descriptorLayoutBindings.push_back(
            VkDescriptorSetLayoutBinding(1U, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1U, VK_SHADER_STAGE_FRAGMENT_BIT, VK_NULL_HANDLE));
    }

    VkDescriptorSetLayoutCreateInfo descriptorLayoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    {
        descriptorLayoutInfo.bindingCount = static_cast<uint32_t>(descriptorLayoutBindings.size());
        descriptorLayoutInfo.pBindings    = descriptorLayoutBindings.data();
        descriptorLayoutInfo.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    }
    Check(vkCreateDescriptorSetLayout(pRenderContext->GetDevice(), &descriptorLayoutInfo, nullptr, &m_DebugDescriptorSetLayout),
          "Failed to create debug descriptor layout.");

    // Specify the descriptor set layouts for the pipeline.
    // --------------------------------------

    // Obtain the resource registry
    auto pResourceRegistry = std::static_pointer_cast<ResourceRegistry>(m_Owner->GetResourceRegistry());

    std::vector<VkDescriptorSetLayout> debugPipelineSetLayouts;
    {
        debugPipelineSetLayouts.push_back(m_DebugDescriptorSetLayout);
        debugPipelineSetLayouts.push_back(pResourceRegistry->GetDrawItemDataDescriptorLayout());
        debugPipelineSetLayouts.push_back(pResourceRegistry->GetMaterialDataDescriptorLayout());
    }

    // Pipeline Layout
    // --------------------------------------

    VkPushConstantRange pushConstantRange;
    {
        pushConstantRange.offset     = 0U;
        pushConstantRange.size       = sizeof(DebugPushConstants);
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkPipelineLayoutCreateInfo pipelineInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    {
        pipelineInfo.pushConstantRangeCount = 1U;
        pipelineInfo.pPushConstantRanges    = &pushConstantRange;
        pipelineInfo.setLayoutCount         = static_cast<uint32_t>(debugPipelineSetLayouts.size());
        pipelineInfo.pSetLayouts            = debugPipelineSetLayouts.data();
    }
    Check(vkCreatePipelineLayout(pRenderContext->GetDevice(), &pipelineInfo, nullptr, &m_DebugPipelineLayout),
          "Failed to create pipeline layout for visibility pipeline.");

    // Shaders
    // --------------------------------------

    VkShaderCreateInfoEXT vertexShaderInfo = { VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT };
    {
        vertexShaderInfo.stage          = VK_SHADER_STAGE_VERTEX_BIT;
        vertexShaderInfo.nextStage      = VK_SHADER_STAGE_FRAGMENT_BIT;
        vertexShaderInfo.setLayoutCount = static_cast<uint32_t>(debugPipelineSetLayouts.size());
        vertexShaderInfo.pSetLayouts    = debugPipelineSetLayouts.data();
    }
    LoadShader(ShaderID::DebugVert, "FullscreenTriangle.vert.spv", "Vert", vertexShaderInfo);

    VkShaderCreateInfoEXT debugShaderInfo = { VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT };
    {
        debugShaderInfo.stage          = VK_SHADER_STAGE_FRAGMENT_BIT;
        debugShaderInfo.setLayoutCount = static_cast<uint32_t>(debugPipelineSetLayouts.size());
        debugShaderInfo.pSetLayouts    = debugPipelineSetLayouts.data();
    }
    LoadShader(ShaderID::DebugFrag, "Debug.frag.spv", "Frag", debugShaderInfo);
}

PFN_vkVoidFunction VKAPI_PTR CustomVulkanDeviceProcAddr(VkDevice device, const char* pName)
{
    // Brixelizer uses an old version of this function:
    // https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/issues/73
    // So we patch it with the correct one.
    if (strcmp(pName, "vkGetBufferMemoryRequirements2KHR") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(vkGetBufferMemoryRequirements2);

    // Forward as normal.
    return vkGetDeviceProcAddr(device, pName);
}

// Render Pass Implementation
// ------------------------------------------------------------

RenderPass::RenderPass(HdRenderIndex* pRenderIndex, const HdRprimCollection& collection, RenderDelegate* pRenderDelegate) :
    HdRenderPass(pRenderIndex, collection), m_Owner(pRenderDelegate)
{
    // Grab the render context.
    auto* pRenderContext = m_Owner->GetRenderContext();

    // Create Rendering Attachments
    // ------------------------------------------------

    Check(CreateRenderingAttachments(pRenderContext, m_ColorAttachment, m_DepthAttachment), "Failed to create the rendering attachments.");

    // Obtain the resource registry
    auto pResourceRegistry = std::static_pointer_cast<ResourceRegistry>(m_Owner->GetResourceRegistry());

    // Create default sampler.
    // ------------------------------------------------

    VkSamplerCreateInfo defaultSamplerInfo = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    {
        defaultSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        defaultSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        defaultSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
    vkCreateSampler(pRenderContext->GetDevice(), &defaultSamplerInfo, nullptr, &m_DefaultSampler);

    NameVulkanObject(pRenderContext->GetDevice(), VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<uint64_t>(m_DefaultSampler), "Default Sampler");

    // Initialize Passes
    // --------------------------------------

    VisibilityPassCreate(pRenderContext);

    // --------------------------------------

    MaterialPassCreate(pRenderContext);

    // --------------------------------------

    DebugPassCreate(pRenderContext);

    // Initialize AMD Brixelizer + GI.
    // --------------------------------------

    VkDeviceContext ffxDeviceContext {};
    {
        ffxDeviceContext.vkDevice         = pRenderContext->GetDevice();
        ffxDeviceContext.vkPhysicalDevice = pRenderContext->GetDevicePhysical();
        ffxDeviceContext.vkDeviceProcAddr = CustomVulkanDeviceProcAddr;
    }
    m_FFXDevice = ffxGetDeviceVK(&ffxDeviceContext);

    m_FFXBackendScratch.resize(32LL * 1024 * 1024); // 32mb.
    Check(ffxGetInterfaceVK(&m_FFXInterface, m_FFXDevice, m_FFXBackendScratch.data(), m_FFXBackendScratch.size(), 1U),
          "Failed to resolve a FideltyFX VK backend.");

    FfxBrixelizerContextDescription brixelizerContextDesc = {};
    {
        brixelizerContextDesc.backendInterface = m_FFXInterface;

        // Four cascades for now (maybe just one in our case?).
        brixelizerContextDesc.numCascades = 4U;

        // Configure per-cascade info.
        for (uint32_t cascadeIndex = 0U; cascadeIndex < brixelizerContextDesc.numCascades; cascadeIndex++)
        {
            auto* pCascadeDesc = &brixelizerContextDesc.cascadeDescs[cascadeIndex]; // NOLINT

            pCascadeDesc->flags = FFX_BRIXELIZER_CASCADE_STATIC;

            // Double the voxel size ever cascade.
            pCascadeDesc->voxelSize = 4.0F * (static_cast<float>(cascadeIndex + 1) / static_cast<float>(brixelizerContextDesc.numCascades));
        }
    }
    Check(ffxBrixelizerContextCreate(&brixelizerContextDesc, &m_FFXBrixelizerContext), "Failed to intiliaze a Brixelizer context.");
}

RenderPass::~RenderPass()
{
    // Grab the render context.
    auto* pRenderContext = m_Owner->GetRenderContext();

    vkDeviceWaitIdle(pRenderContext->GetDevice());

    Check(ffxBrixelizerContextDestroy(&m_FFXBrixelizerContext), "Failed to destroy Brixelizer context.");

    vkDestroyImageView(pRenderContext->GetDevice(), m_ColorAttachment.imageView, nullptr);
    vkDestroyImageView(pRenderContext->GetDevice(), m_DepthAttachment.imageView, nullptr);
    vkDestroyImageView(pRenderContext->GetDevice(), m_VisibilityBuffer.imageView, nullptr);

    vmaDestroyImage(pRenderContext->GetAllocator(), m_ColorAttachment.image, m_ColorAttachment.imageAllocation);
    vmaDestroyImage(pRenderContext->GetAllocator(), m_DepthAttachment.image, m_DepthAttachment.imageAllocation);
    vmaDestroyImage(pRenderContext->GetAllocator(), m_VisibilityBuffer.image, m_VisibilityBuffer.imageAllocation);

    vmaDestroyBuffer(pRenderContext->GetAllocator(), m_MaterialCountBuffer.buffer, m_MaterialCountBuffer.bufferAllocation);
    vmaDestroyBuffer(pRenderContext->GetAllocator(), m_MaterialOffsetBuffer.buffer, m_MaterialOffsetBuffer.bufferAllocation);
    vmaDestroyBuffer(pRenderContext->GetAllocator(), m_MaterialPixelBuffer.buffer, m_MaterialPixelBuffer.bufferAllocation);

    vkDestroyDescriptorSetLayout(pRenderContext->GetDevice(), m_DebugDescriptorSetLayout, nullptr);

    vkDestroyPipelineLayout(pRenderContext->GetDevice(), m_VisibilityPipelineLayout, nullptr);
    vkDestroyPipelineLayout(pRenderContext->GetDevice(), m_DebugPipelineLayout, nullptr);

    for (auto& shader : m_ShaderMap)
        vkDestroyShaderEXT(pRenderContext->GetDevice(), shader.second, nullptr);

    vkDestroySampler(pRenderContext->GetDevice(), m_DefaultSampler, nullptr);
}

void RenderPass::VisibilityPassExecute(FrameContext* pFrameContext)
{
    GPUProfileScope profileScope(pFrameContext->pFrame->cmd, "Visibility Pass");

    VulkanColorImageBarrier(pFrameContext->pFrame->cmd,
                            m_VisibilityBuffer.image,
                            VK_IMAGE_LAYOUT_GENERAL,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_ACCESS_2_MEMORY_READ_BIT,
                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

    // Configure Attachments
    // --------------------------------------------

    VkRenderingAttachmentInfo colorAttachmentInfo = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    {
        colorAttachmentInfo.loadOp           = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachmentInfo.storeOp          = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachmentInfo.imageLayout      = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachmentInfo.imageView        = m_VisibilityBuffer.imageView;
        colorAttachmentInfo.clearValue.color = {
            { 0U, 0U, 0U, 0U }
        };
    }

    VkRenderingAttachmentInfo depthAttachmentInfo = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    {
        depthAttachmentInfo.loadOp                  = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachmentInfo.storeOp                 = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachmentInfo.imageLayout             = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthAttachmentInfo.imageView               = m_DepthAttachment.imageView;
        depthAttachmentInfo.clearValue.depthStencil = { 1.0, 0x0 };
    }

    VkRenderingInfo vkRenderingInfo = { VK_STRUCTURE_TYPE_RENDERING_INFO };
    {
        vkRenderingInfo.colorAttachmentCount = 1U;
        vkRenderingInfo.pColorAttachments    = &colorAttachmentInfo;
        vkRenderingInfo.pDepthAttachment     = &depthAttachmentInfo;
        vkRenderingInfo.pStencilAttachment   = VK_NULL_HANDLE;
        vkRenderingInfo.layerCount           = 1U;
        vkRenderingInfo.renderArea           = {
            {            0,             0 },
            { kWindowWidth, kWindowHeight }
        };
    }
    vkCmdBeginRendering(pFrameContext->pFrame->cmd, &vkRenderingInfo);

    SetDefaultRenderState(pFrameContext->pFrame->cmd);

    BindGraphicsShaders(pFrameContext->pFrame->cmd, m_ShaderMap[ShaderID::VisibilityVert], m_ShaderMap[ShaderID::VisibilityFrag]);

    vkCmdSetVertexInputEXT(pFrameContext->pFrame->cmd,
                           static_cast<uint32_t>(m_VertexInputBindings.size()),
                           m_VertexInputBindings.data(),
                           static_cast<uint32_t>(m_VertexInputAttributes.size()),
                           m_VertexInputAttributes.data());

    // Update camera matrices.
    auto matrixVP = GfMatrix4f(pFrameContext->pPassState->GetWorldToViewMatrix()) * GfMatrix4f(pFrameContext->pPassState->GetProjectionMatrix());

    PROFILE_START("Record Visibility Buffer Commands");

    const auto& drawItems = pFrameContext->pResourceRegistry->GetDrawItems();

    m_VisibilityPushConstants.MeshCount = static_cast<uint32_t>(drawItems.size());

    // TODO(parsa): Go wide on all cores to record these commands on a secondary command list.
    for (uint32_t drawItemIndex = 0U; drawItemIndex < drawItems.size(); drawItemIndex++)
    {
        auto drawItem = drawItems[drawItemIndex];

        vkCmdBindIndexBuffer(pFrameContext->pFrame->cmd, drawItem.bufferI.buffer, 0U, VK_INDEX_TYPE_UINT32);

        std::array<VkDeviceSize, 1> vertexBufferOffset = { 0U };
        std::array<VkBuffer, 1>     vertexBuffers      = { drawItem.bufferV.buffer };

        vkCmdBindVertexBuffers(pFrameContext->pFrame->cmd, 0U, 1U, vertexBuffers.data(), vertexBufferOffset.data());

        m_VisibilityPushConstants.MatrixMVP = drawItem.pMesh->GetLocalToWorld() * matrixVP;
        m_VisibilityPushConstants.MeshID    = drawItemIndex;

        vkCmdPushConstants(pFrameContext->pFrame->cmd,
                           m_VisibilityPipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0U,
                           sizeof(VisibilityPushConstants),
                           &m_VisibilityPushConstants);

        vkCmdDrawIndexed(pFrameContext->pFrame->cmd, drawItem.indexCount, 1U, 0U, 0U, 0U);
    }

    PROFILE_END;

    vkCmdEndRendering(pFrameContext->pFrame->cmd);

    VulkanColorImageBarrier(pFrameContext->pFrame->cmd,
                            m_VisibilityBuffer.image,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_LAYOUT_GENERAL,
                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_ACCESS_2_MEMORY_READ_BIT,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
}

void RenderPass::DebugPassExecute(FrameContext* pFrameContext)
{
    GPUProfileScope profileScope(pFrameContext->pFrame->cmd, "Debug Pass");

    VulkanColorImageBarrier(pFrameContext->pFrame->cmd,
                            m_VisibilityBuffer.image,
                            VK_IMAGE_LAYOUT_GENERAL,
                            VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
                            VK_ACCESS_2_MEMORY_READ_BIT,
                            VK_ACCESS_2_MEMORY_READ_BIT,
                            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);

    VkRenderingAttachmentInfo colorAttachmentInfo = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    {
        colorAttachmentInfo.loadOp           = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachmentInfo.storeOp          = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachmentInfo.imageLayout      = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachmentInfo.imageView        = m_ColorAttachment.imageView;
        colorAttachmentInfo.clearValue.color = {
            { 0U, 0U, 0U, 0U }
        };
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
    vkCmdBeginRendering(pFrameContext->pFrame->cmd, &vkRenderingInfo);

    SetDefaultRenderState(pFrameContext->pFrame->cmd);

    m_DebugPushConstants.MatrixVP =
        GfMatrix4f(pFrameContext->pPassState->GetWorldToViewMatrix()) * GfMatrix4f(pFrameContext->pPassState->GetProjectionMatrix());
    m_DebugPushConstants.DebugModeValue = static_cast<uint32_t>(pFrameContext->debugMode);
    m_DebugPushConstants.MeshCount      = static_cast<uint32_t>(pFrameContext->pResourceRegistry->GetDrawItems().size());

    vkCmdPushConstants(pFrameContext->pFrame->cmd,
                       m_DebugPipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0U,
                       sizeof(DebugPushConstants),
                       &m_DebugPushConstants);

    std::array<VkDescriptorImageInfo, 2> imageInfo {};
    std::array<VkWriteDescriptorSet, 2>  writeDescriptorSets {};
    {
        {
            imageInfo[0].imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
            imageInfo[0].imageView   = m_VisibilityBuffer.imageView;
        }

        {
            imageInfo[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            imageInfo[1].imageView   = m_DepthAttachment.imageView;
        }

        writeDescriptorSets[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSets[0].dstSet          = 0;
        writeDescriptorSets[0].dstBinding      = 0;
        writeDescriptorSets[0].descriptorCount = 1;
        writeDescriptorSets[0].descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writeDescriptorSets[0].pImageInfo      = &imageInfo[0];

        writeDescriptorSets[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSets[1].dstSet          = 0;
        writeDescriptorSets[1].dstBinding      = 1;
        writeDescriptorSets[1].descriptorCount = 1;
        writeDescriptorSets[1].descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writeDescriptorSets[1].pImageInfo      = &imageInfo[1];
    }

    // NOTE: Validation layers complain that descriptors aren't bound or bound incorrectly when we are using VK_EXT_shader_object:
    //       1) VUID-vkCmdDraw-format-07753
    //       2) VUID-vkCmdDraw-None-08600
    //       File a bug report with Khronos or follow up in this thread: https://github.com/KhronosGroup/Vulkan-ValidationLayers/issues/7677
    vkCmdPushDescriptorSetKHR(pFrameContext->pFrame->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_DebugPipelineLayout, 0, 2, writeDescriptorSets.data());

    // For the second descriptor set, we use traditional descriptors that are pre-created during the alst resource registry update.
    // We bind this set to the second slot.
    {
        vkCmdBindDescriptorSets(pFrameContext->pFrame->cmd,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_DebugPipelineLayout,
                                1U,
                                1U,
                                &pFrameContext->pResourceRegistry->GetDrawItemDataDescriptorSet(),
                                0U,
                                nullptr);
    }

    // Similarly, bind material data.
    {
        vkCmdBindDescriptorSets(pFrameContext->pFrame->cmd,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_DebugPipelineLayout,
                                2U,
                                1U,
                                &pFrameContext->pResourceRegistry->GetMaterialDataDescriptorSet(),
                                0U,
                                nullptr);
    }

    BindGraphicsShaders(pFrameContext->pFrame->cmd, m_ShaderMap[ShaderID::DebugVert], m_ShaderMap[ShaderID::DebugFrag]);

    // Fullscreen triangle (three procedural vertices).
    vkCmdDraw(pFrameContext->pFrame->cmd, 3U, 1U, 0U, 0U);

    vkCmdEndRendering(pFrameContext->pFrame->cmd);

    VulkanColorImageBarrier(pFrameContext->pFrame->cmd,
                            m_VisibilityBuffer.image,
                            VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
                            VK_IMAGE_LAYOUT_GENERAL,
                            VK_ACCESS_2_MEMORY_READ_BIT,
                            VK_ACCESS_2_MEMORY_READ_BIT,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
}

void RenderPass::RebuildAccelerationStructure(FrameContext* pFrameContext)
{
    PROFILE_START("Build Acceleration Structure");

    auto& drawItems = pFrameContext->pResourceRegistry->GetDrawItems();

    if (drawItems.size() == 0)
        return;

    // 1) Register instance buffer bindings
    //    Should pre-allocate these to avoid frame-time mallocs
    // ---------------------------------------------

    // Combined list of descriptions for vertex and index buffers.
    std::vector<FfxBrixelizerBufferDescription> instanceBufferDescs(2U * drawItems.size());

    // List of acceleration structure instance data.
    std::vector<FfxBrixelizerInstanceDescription> instanceDescs(drawItems.size());

    // 2) Register instance buffer bindings
    // ---------------------------------------------

    for (uint32_t drawItemIndex = 0U; drawItemIndex < drawItems.size(); drawItemIndex++)
    {
        const auto& drawItem = drawItems.at(drawItemIndex);

        // Index
        instanceBufferDescs[2U * drawItemIndex + 0U].outIndex = &instanceDescs.at(drawItemIndex).indexBuffer;
        instanceBufferDescs[2U * drawItemIndex + 0U].buffer =
            ffxGetResourceVK(drawItem.bufferI.buffer,
                             ffxGetBufferResourceDescriptionVK(drawItem.bufferI.buffer, drawItem.bufferI.bufferInfo),
                             L"Brixelizer Buffer");

        // Vertex
        instanceBufferDescs[2U * drawItemIndex + 1U].outIndex = &instanceDescs.at(drawItemIndex).vertexBuffer;
        instanceBufferDescs[2U * drawItemIndex + 1U].buffer =
            ffxGetResourceVK(drawItem.bufferV.buffer,
                             ffxGetBufferResourceDescriptionVK(drawItem.bufferV.buffer, drawItem.bufferV.bufferInfo),
                             L"Brixelizer Buffer");
    }

    Check(ffxBrixelizerRegisterBuffers(&m_FFXBrixelizerContext, instanceBufferDescs.data(), static_cast<uint32_t>(instanceBufferDescs.size())),
          "Failed to register draw item buffers to Brixelizer acceleration structure.");

    // 3) Create instances from draw items.
    // ---------------------------------------------

    for (uint32_t drawItemIndex = 0U; drawItemIndex < drawItems.size(); drawItemIndex++)
    {
        auto& drawItem = drawItems.at(drawItemIndex);

        auto* pDesc = &instanceDescs.at(drawItemIndex);

        // Configure the acceleration structure instance.
        // NOTE: Vertex and index buffer indices where set in the prior pass.
        pDesc->maxCascade         = 4U;
        pDesc->aabb               = drawItem.pMesh->GetAABB();
        pDesc->triangleCount      = drawItem.indexCount / 3U;
        pDesc->indexFormat        = FFX_INDEX_TYPE_UINT32;
        pDesc->indexBufferOffset  = 0U;
        pDesc->vertexCount        = static_cast<uint32_t>(drawItem.bufferV.bufferInfo.size) / sizeof(GfVec3f);
        pDesc->vertexStride       = sizeof(GfVec3f);
        pDesc->vertexBufferOffset = 0U;
        pDesc->vertexFormat       = FFX_SURFACE_FORMAT_R32G32B32_FLOAT;
        pDesc->flags              = FFX_BRIXELIZER_INSTANCE_FLAG_NONE;

        // Copy the transform.
        memcpy(&pDesc->transform[0], &drawItem.pMesh->GetLocalToWorld3x4()[0], sizeof(FfxFloat32x3x4));

        // Update the draw item with the instance ID inside brixelizer.
        pDesc->outInstanceID = &drawItem.brixelizerID;
    }

    Check(ffxBrixelizerCreateInstances(&m_FFXBrixelizerContext, instanceDescs.data(), static_cast<uint32_t>(instanceDescs.size())),
          "Failed to add draw item to Brixelizer acceleration structure.");

    // Done.
    m_RebuildAccelerationStructure = false;

    PROFILE_END;
}

void RenderPass::_Execute(const HdRenderPassStateSharedPtr& renderPassState, const TfTokenVector& renderTags)
{
    FrameContext frameContext {};
    {
        frameContext.pRenderContext    = m_Owner->GetRenderContext();
        frameContext.pFrame            = m_Owner->GetRenderSetting(kTokenCurrenFrameParams).UncheckedGet<FrameParams*>();
        frameContext.debugMode         = static_cast<DebugMode>(*m_Owner->GetRenderSetting(kTokenDebugMode).UncheckedGet<int*>());
        frameContext.pPassState        = renderPassState.get();
        frameContext.pResourceRegistry = std::static_pointer_cast<ResourceRegistry>(m_Owner->GetResourceRegistry()).get();
    };

    VulkanColorImageBarrier(frameContext.pFrame->cmd,
                            m_ColorAttachment.image,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_ACCESS_2_MEMORY_READ_BIT,
                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

    // 1) New Frame

    if (!frameContext.pResourceRegistry->IsBusy() && m_RebuildAccelerationStructure)
        RebuildAccelerationStructure(&frameContext);

    // 2) Rasterize V-Buffer
    //    Ref: https://jcgt.org/published/0002/02/04/
    //    Ref: https://www.gdcvault.com/play/1023792/4K-Rendering-Breakthrough-The-Filtered
    //    Ref: http://filmicworlds.com/blog/visibility-buffer-rendering-with-material-graphs/
    //
    //    TODO(parsa): Use https://github.com/zeux/meshoptimizer to produce optimized meshlets
    //                 that can be culled in a mesh shader.

    if (!frameContext.pResourceRegistry->IsBusy())
        VisibilityPassExecute(&frameContext);

    // 3) Material Pass

    // 4) Resolve G-Buffer from V-Buffer.

    // 5) Lighting Pass

    // 6) Debug

    if (frameContext.debugMode != DebugMode::None)
        DebugPassExecute(&frameContext);

    // Copy the internal color attachment to back buffer.

    VulkanColorImageBarrier(frameContext.pFrame->cmd,
                            m_ColorAttachment.image,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_ACCESS_2_TRANSFER_READ_BIT,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                            VK_PIPELINE_STAGE_2_TRANSFER_BIT);

    VulkanColorImageBarrier(frameContext.pFrame->cmd,
                            frameContext.pFrame->backBuffer,
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_ACCESS_2_MEMORY_READ_BIT,
                            VK_ACCESS_2_MEMORY_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                            VK_PIPELINE_STAGE_2_TRANSFER_BIT);

    VkImageCopy backBufferCopy = {};
    {
        backBufferCopy.extent         = { kWindowWidth, kWindowHeight, 1U };
        backBufferCopy.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0U, 0U, 1U };
        backBufferCopy.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0U, 0U, 1U };
    }

    vkCmdCopyImage(frameContext.pFrame->cmd,
                   m_ColorAttachment.image,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   frameContext.pFrame->backBuffer,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1U,
                   &backBufferCopy);

    VulkanColorImageBarrier(frameContext.pFrame->cmd,
                            frameContext.pFrame->backBuffer,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                            VK_ACCESS_2_MEMORY_WRITE_BIT,
                            VK_ACCESS_2_MEMORY_READ_BIT,
                            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
}
