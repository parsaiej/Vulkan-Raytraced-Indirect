#include <Mesh.h>
#include <RenderContext.h>
#include <RenderDelegate.h>
#include <RenderPass.h>
#include <ResourceRegistry.h>
#include <Scene.h>
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
    LoadShader(ShaderID::MeshVert, "Visibility.vert.spv", "Vert", vertexShaderInfo);

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
        visibilityBufferInfo.usage         = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
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
}

void RenderPass::MaterialPassCreate(RenderContext* pRenderContext)
{
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
}

RenderPass::~RenderPass()
{
    // Grab the render context.
    auto* pRenderContext = m_Owner->GetRenderContext();

    vkDeviceWaitIdle(pRenderContext->GetDevice());

    vkDestroyImageView(pRenderContext->GetDevice(), m_ColorAttachment.imageView, nullptr);
    vkDestroyImageView(pRenderContext->GetDevice(), m_DepthAttachment.imageView, nullptr);
    vkDestroyImageView(pRenderContext->GetDevice(), m_VisibilityBuffer.imageView, nullptr);

    vmaDestroyImage(pRenderContext->GetAllocator(), m_ColorAttachment.image, m_ColorAttachment.imageAllocation);
    vmaDestroyImage(pRenderContext->GetAllocator(), m_DepthAttachment.image, m_DepthAttachment.imageAllocation);
    vmaDestroyImage(pRenderContext->GetAllocator(), m_VisibilityBuffer.image, m_VisibilityBuffer.imageAllocation);

    vmaDestroyBuffer(pRenderContext->GetAllocator(), m_MaterialCountBuffer.buffer, m_MaterialCountBuffer.bufferAllocation);
    vmaDestroyBuffer(pRenderContext->GetAllocator(), m_MaterialOffsetBuffer.buffer, m_MaterialOffsetBuffer.bufferAllocation);
    vmaDestroyBuffer(pRenderContext->GetAllocator(), m_MaterialPixelBuffer.buffer, m_MaterialPixelBuffer.bufferAllocation);

    vkDestroyPipelineLayout(pRenderContext->GetDevice(), m_VisibilityPipelineLayout, nullptr);

    for (auto& shader : m_ShaderMap)
        vkDestroyShaderEXT(pRenderContext->GetDevice(), shader.second, nullptr);

    vkDestroySampler(pRenderContext->GetDevice(), m_DefaultSampler, nullptr);
}

void RenderPass::VisibilityPassExecute(RenderPassContext* pCtx)
{
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
    vkCmdBeginRendering(pCtx->pFrame->cmd, &vkRenderingInfo);

    std::array<VkShaderStageFlagBits, 5> vkGraphicsShaderStageBits = { VK_SHADER_STAGE_VERTEX_BIT,
                                                                       VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
                                                                       VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
                                                                       VK_SHADER_STAGE_GEOMETRY_BIT,
                                                                       VK_SHADER_STAGE_FRAGMENT_BIT };

    std::array<VkShaderEXT, 5> vkGraphicsShaders = { m_ShaderMap[ShaderID::MeshVert],
                                                     VK_NULL_HANDLE,
                                                     VK_NULL_HANDLE,
                                                     VK_NULL_HANDLE,
                                                     m_ShaderMap[ShaderID::VisibilityFrag] };

    vkCmdBindShadersEXT(pCtx->pFrame->cmd,
                        static_cast<uint32_t>(vkGraphicsShaderStageBits.size()),
                        vkGraphicsShaderStageBits.data(),
                        vkGraphicsShaders.data());

    SetDefaultRenderState(pCtx->pFrame->cmd);

    vkCmdSetVertexInputEXT(pCtx->pFrame->cmd,
                           static_cast<uint32_t>(m_VertexInputBindings.size()),
                           m_VertexInputBindings.data(),
                           static_cast<uint32_t>(m_VertexInputAttributes.size()),
                           m_VertexInputAttributes.data());

    // Update camera matrices.
    auto matrixVP = GfMatrix4f(pCtx->pPassState->GetWorldToViewMatrix()) * GfMatrix4f(pCtx->pPassState->GetProjectionMatrix());

    PROFILE_START("Record Visibility Buffer Commands");

    const auto& meshList = pCtx->pScene->GetMeshList();

    m_VisibilityPushConstants.MeshCount = static_cast<uint32_t>(meshList.size());

    // TODO(parsa): Go wide on all cores to record these commands on a secondary command list.
    for (uint32_t meshIndex = 0U; meshIndex < meshList.size(); meshIndex++)
    {
        auto* pMesh = meshList[meshIndex];

        ResourceRegistry::MeshResources mesh;
        if (!pCtx->pResourceRegistry->GetMeshResources(pMesh->GetResourceHandle(), mesh))
            return;

        vkCmdBindIndexBuffer(pCtx->pFrame->cmd, mesh.indices.buffer, 0U, VK_INDEX_TYPE_UINT32);

        std::array<VkDeviceSize, 1> vertexBufferOffset = { 0U };
        std::array<VkBuffer, 1>     vertexBuffers      = { mesh.positions.buffer };

        vkCmdBindVertexBuffers(pCtx->pFrame->cmd, 0U, 1U, vertexBuffers.data(), vertexBufferOffset.data());

        m_VisibilityPushConstants.MatrixMVP = pMesh->GetLocalToWorld() * matrixVP;
        m_VisibilityPushConstants.MeshID    = meshIndex;

        vkCmdPushConstants(pCtx->pFrame->cmd,
                           m_VisibilityPipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0U,
                           sizeof(VisibilityPushConstants),
                           &m_VisibilityPushConstants);

        vkCmdDrawIndexed(pCtx->pFrame->cmd, pMesh->GetIndexCount(), 1U, 0U, 0U, 0U);
    }

    PROFILE_END;

    vkCmdEndRendering(pCtx->pFrame->cmd);
}

void RenderPass::_Execute(const HdRenderPassStateSharedPtr& renderPassState, const TfTokenVector& renderTags)
{
    RenderPassContext ctx {};
    {
        ctx.pRenderContext    = m_Owner->GetRenderContext();
        ctx.pFrame            = m_Owner->GetRenderSetting(kTokenCurrenFrameParams).UncheckedGet<FrameParams*>();
        ctx.pScene            = m_Owner->GetRenderContext()->GetScene();
        ctx.pPassState        = renderPassState.get();
        ctx.pResourceRegistry = std::static_pointer_cast<ResourceRegistry>(m_Owner->GetResourceRegistry()).get();
    };

    VulkanColorImageBarrier(ctx.pFrame->cmd,
                            m_ColorAttachment.image,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_ACCESS_2_MEMORY_READ_BIT,
                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

    // 1) Rasterize V-Buffer

    if (ctx.pResourceRegistry->IsComplete())
        VisibilityPassExecute(&ctx);

    // 2) Resolve G-Buffer from V-Buffer.

    // 3) Lighting Pass

    // Copy the internal color attachment to back buffer.

    VulkanColorImageBarrier(ctx.pFrame->cmd,
                            m_ColorAttachment.image,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_ACCESS_2_TRANSFER_READ_BIT,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                            VK_PIPELINE_STAGE_2_TRANSFER_BIT);

    VulkanColorImageBarrier(ctx.pFrame->cmd,
                            ctx.pFrame->backBuffer,
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

    vkCmdCopyImage(ctx.pFrame->cmd,
                   m_ColorAttachment.image,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   ctx.pFrame->backBuffer,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1U,
                   &backBufferCopy);

    VulkanColorImageBarrier(ctx.pFrame->cmd,
                            ctx.pFrame->backBuffer,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                            VK_ACCESS_2_MEMORY_WRITE_BIT,
                            VK_ACCESS_2_MEMORY_READ_BIT,
                            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
}
