#include <Mesh.h>
#include <RenderContext.h>
#include <RenderDelegate.h>
#include <RenderPass.h>
#include <ResourceRegistry.h>
#include <Scene.h>

// Render Pass Implementation
// ------------------------------------------------------------

RenderPass::RenderPass(HdRenderIndex* pRenderIndex, HdRprimCollection const& collection, RenderDelegate* pRenderDelegate) : HdRenderPass(pRenderIndex, collection), m_Owner(pRenderDelegate)
{
    // Grab the render context.
    auto* pRenderContext = m_Owner->GetRenderContext();

    // Create Rendering Attachments
    // ------------------------------------------------

    Check(CreateRenderingAttachments(pRenderContext, m_ColorAttachment, m_DepthAttachment), "Failed to create the rendering attachments.");

    // Configure Descriptor Set Layouts
    // --------------------------------------

    Check(CreatePhysicallyBasedMaterialDescriptorLayout(pRenderContext->GetDevice(), m_DescriptorSetLayout),
        "Failed to create a Vulkan Descriptor Set Layout for Physically Based "
        "Materials.");

    // Shader Creation Utility
    // ------------------------------------------------

    auto LoadShader = [&](ShaderID shaderID, const char* filePath, VkShaderCreateInfoEXT vkShaderInfo) {
        std::vector<char> shaderByteCode;
        Check(LoadByteCode(filePath, shaderByteCode), std::format("Failed to read shader bytecode: {}", filePath).c_str());

        vkShaderInfo.pName    = "Main";
        vkShaderInfo.pCode    = shaderByteCode.data();
        vkShaderInfo.codeSize = shaderByteCode.size();
        vkShaderInfo.codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT;

        VkShaderEXT vkShader = VK_NULL_HANDLE;
        Check(vkCreateShadersEXT(pRenderContext->GetDevice(), 1U, &vkShaderInfo, nullptr, &vkShader), std::format("Failed to load Vulkan Shader: {}", filePath).c_str());
        Check(!m_ShaderMap.contains(shaderID), "Tried to store a Vulkan Shader into an existing shader slot.");

        spdlog::info("Loaded Vulkan Shader: {}", filePath);

        m_ShaderMap[shaderID] = vkShader;
    };

    // Configure Push Constants
    // --------------------------------------

    VkPushConstantRange vkPushConstants;
    {
        vkPushConstants.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        vkPushConstants.offset     = 0U;
        vkPushConstants.size       = sizeof(PushConstants);
    }

    // Configure Pipeline Layouts
    // --------------------------------------

    VkPipelineLayoutCreateInfo vkPipelineLayoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    {
        vkPipelineLayoutInfo.setLayoutCount         = 1U;
        vkPipelineLayoutInfo.pSetLayouts            = &m_DescriptorSetLayout;
        vkPipelineLayoutInfo.pushConstantRangeCount = 1U;
        vkPipelineLayoutInfo.pPushConstantRanges    = &vkPushConstants;
    }
    Check(vkCreatePipelineLayout(pRenderContext->GetDevice(), &vkPipelineLayoutInfo, nullptr, &m_PipelineLayout), "Failed to create the default Vulkan Pipeline Layout");

    // Create Shader Objects
    // --------------------------------------

    VkShaderCreateInfoEXT vertexShaderInfo = { VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT };
    {
        vertexShaderInfo.stage     = VK_SHADER_STAGE_VERTEX_BIT;
        vertexShaderInfo.nextStage = VK_SHADER_STAGE_FRAGMENT_BIT;

        vertexShaderInfo.pushConstantRangeCount = 1U;
        vertexShaderInfo.pPushConstantRanges    = &vkPushConstants;
        vertexShaderInfo.setLayoutCount         = 0U;
        vertexShaderInfo.pSetLayouts            = nullptr;
    }
    LoadShader(ShaderID::FullscreenTriangleVert, "FullscreenTriangle.vert.spv", vertexShaderInfo);
    LoadShader(ShaderID::MeshVert, "Mesh.vert.spv", vertexShaderInfo);

    VkShaderCreateInfoEXT litShaderInfo = { VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT };
    {
        litShaderInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;

        litShaderInfo.pushConstantRangeCount = 1U;
        litShaderInfo.pPushConstantRanges    = &vkPushConstants;
    }
    LoadShader(ShaderID::LitFrag, "Lit.frag.spv", litShaderInfo);

    // Vertex Input Layout
    // ------------------------------------------------

    GetVertexInputLayout(m_VertexInputBindings, m_VertexInputAttributes);

    // Configure Push Constants
    // ------------------------------------------------

    m_PushConstants = {};
}

RenderPass::~RenderPass()
{
    // Grab the render context.
    auto* pRenderContext = m_Owner->GetRenderContext();

    vkDeviceWaitIdle(pRenderContext->GetDevice());

    vkDestroyImageView(pRenderContext->GetDevice(), m_ColorAttachment.imageView, nullptr);
    vkDestroyImageView(pRenderContext->GetDevice(), m_DepthAttachment.imageView, nullptr);

    vmaDestroyImage(pRenderContext->GetAllocator(), m_ColorAttachment.image, m_ColorAttachment.imageAllocation);
    vmaDestroyImage(pRenderContext->GetAllocator(), m_DepthAttachment.image, m_DepthAttachment.imageAllocation);

    vkDestroyPipelineLayout(pRenderContext->GetDevice(), m_PipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(pRenderContext->GetDevice(), m_DescriptorSetLayout, nullptr);

    for (auto& shader : m_ShaderMap)
        vkDestroyShaderEXT(pRenderContext->GetDevice(), shader.second, nullptr);
}

void RenderPass::_Execute(HdRenderPassStateSharedPtr const& renderPassState, TfTokenVector const& renderTags)
{
    // Grab the render context.
    auto* pRenderContext = m_Owner->GetRenderContext();

    // Grab a handle to the current frame.
    auto* pFrame = m_Owner->GetRenderSetting(kTokenCurrenFrameParams).UncheckedGet<FrameParams*>();

    auto ColorAttachmentBarrier = [&](VkCommandBuffer vkCommand, VkImage vkImage, VkImageLayout vkLayoutOld, VkImageLayout vkLayoutNew, VkAccessFlags2 vkAccessSrc, VkAccessFlags2 vkAccessDst,
                                      VkPipelineStageFlags2 vkStageSrc, VkPipelineStageFlags2 vkStageDst) {
        VkImageSubresourceRange imageSubresource;
        {
            imageSubresource.levelCount     = 1U;
            imageSubresource.layerCount     = 1U;
            imageSubresource.baseMipLevel   = 0U;
            imageSubresource.baseArrayLayer = 0U;
            imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        }

        VkImageMemoryBarrier2 vkImageBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        {
            vkImageBarrier.image               = vkImage;
            vkImageBarrier.oldLayout           = vkLayoutOld;
            vkImageBarrier.newLayout           = vkLayoutNew;
            vkImageBarrier.srcAccessMask       = vkAccessSrc;
            vkImageBarrier.dstAccessMask       = vkAccessDst;
            vkImageBarrier.srcStageMask        = vkStageSrc;
            vkImageBarrier.dstStageMask        = vkStageDst;
            vkImageBarrier.srcQueueFamilyIndex = pRenderContext->GetCommandQueueIndex();
            vkImageBarrier.dstQueueFamilyIndex = pRenderContext->GetCommandQueueIndex();
            vkImageBarrier.subresourceRange    = imageSubresource;
        }

        VkDependencyInfo vkDependencyInfo = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        {
            vkDependencyInfo.imageMemoryBarrierCount = 1U;
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
        colorAttachmentInfo.clearValue  = { { { 0.0, 0.0, 0.0, 1.0 } } };
        colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachmentInfo.imageView   = m_ColorAttachment.imageView;
    }

    VkRenderingAttachmentInfo depthAttachmentInfo = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    {
        depthAttachmentInfo.loadOp                  = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachmentInfo.storeOp                 = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachmentInfo.imageLayout             = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthAttachmentInfo.imageView               = m_DepthAttachment.imageView;
        depthAttachmentInfo.clearValue.depthStencil = { 1.0, 0x0 };
    }

    // Record
    // --------------------------------------------

    ColorAttachmentBarrier(pFrame->cmd, m_ColorAttachment.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_2_NONE, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

    VkRenderingInfo vkRenderingInfo = { VK_STRUCTURE_TYPE_RENDERING_INFO };
    {
        vkRenderingInfo.colorAttachmentCount = 1U;
        vkRenderingInfo.pColorAttachments    = &colorAttachmentInfo;
        vkRenderingInfo.pDepthAttachment     = &depthAttachmentInfo;
        vkRenderingInfo.pStencilAttachment   = VK_NULL_HANDLE;
        vkRenderingInfo.layerCount           = 1U;
        vkRenderingInfo.renderArea           = { { 0, 0 }, { kWindowWidth, kWindowHeight } };
    }
    vkCmdBeginRendering(pFrame->cmd, &vkRenderingInfo);

    std::array<VkShaderStageFlagBits, 5> vkGraphicsShaderStageBits = { VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
        VK_SHADER_STAGE_GEOMETRY_BIT, VK_SHADER_STAGE_FRAGMENT_BIT };

    std::array<VkShaderEXT, 5> vkGraphicsShaders = { m_ShaderMap[ShaderID::MeshVert], VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, m_ShaderMap[ShaderID::LitFrag] };

    vkCmdBindShadersEXT(pFrame->cmd, static_cast<uint32_t>(vkGraphicsShaderStageBits.size()), vkGraphicsShaderStageBits.data(), vkGraphicsShaders.data());

    SetDefaultRenderState(pFrame->cmd);

    vkCmdSetVertexInputEXT(
        pFrame->cmd, static_cast<uint32_t>(m_VertexInputBindings.size()), m_VertexInputBindings.data(), static_cast<uint32_t>(m_VertexInputAttributes.size()), m_VertexInputAttributes.data());

    auto* pScene    = pRenderContext->GetScene();
    auto pResources = std::static_pointer_cast<ResourceRegistry>(m_Owner->GetResourceRegistry());

    // Update camera matrices.
    m_PushConstants.MatrixVP = GfMatrix4f(renderPassState->GetWorldToViewMatrix()) * GfMatrix4f(renderPassState->GetProjectionMatrix());

    for (const auto& pMesh : pScene->GetMeshList())
    {
        std::pair<VkBuffer, VmaAllocation> positionBuffer;
        std::pair<VkBuffer, VmaAllocation> normalBuffer;
        std::pair<VkBuffer, VmaAllocation> indexBuffer;
        pResources->GetMeshResources(pMesh->GetResourceHandle(), positionBuffer, normalBuffer, indexBuffer);

        VmaAllocationInfo allocationInfo;
        vmaGetAllocationInfo(pRenderContext->GetAllocator(), indexBuffer.second, &allocationInfo);

        vkCmdBindIndexBuffer(pFrame->cmd, indexBuffer.first, 0U, VK_INDEX_TYPE_UINT32);

        std::array<VkDeviceSize, 2> vertexBufferOffset = { 0U, 0U };
        std::array<VkBuffer, 2> vertexBuffers          = { positionBuffer.first, normalBuffer.first };

        vkCmdBindVertexBuffers(pFrame->cmd, 0U, 2U, vertexBuffers.data(), vertexBufferOffset.data());

        m_PushConstants.MatrixM = pMesh->GetLocalToWorld();
        vkCmdPushConstants(pFrame->cmd, m_PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0U, sizeof(PushConstants), &m_PushConstants);

        vkCmdDrawIndexed(pFrame->cmd, static_cast<uint32_t>(allocationInfo.size) / sizeof(uint32_t), 1U, 0U, 0U, 0U);
    }

    vkCmdEndRendering(pFrame->cmd);

    // Copy the internal color attachment to back buffer.

    ColorAttachmentBarrier(pFrame->cmd, m_ColorAttachment.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_2_NONE, VK_ACCESS_2_MEMORY_READ_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT);

    ColorAttachmentBarrier(pFrame->cmd, pFrame->backBuffer, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_2_MEMORY_READ_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT);

    VkImageCopy backBufferCopy = {};
    {
        backBufferCopy.extent         = { kWindowWidth, kWindowHeight, 1U };
        backBufferCopy.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0U, 0U, 1U };
        backBufferCopy.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0U, 0U, 1U };
    }
    vkCmdCopyImage(pFrame->cmd, m_ColorAttachment.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, pFrame->backBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1U, &backBufferCopy);

    ColorAttachmentBarrier(pFrame->cmd, pFrame->backBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_2_MEMORY_WRITE_BIT, VK_ACCESS_2_MEMORY_READ_BIT,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
}