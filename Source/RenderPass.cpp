#include <Mesh.h>
#include <RenderContext.h>
#include <RenderDelegate.h>
#include <RenderPass.h>
#include <ResourceRegistry.h>
#include <Scene.h>

// Render Pass Implementation
// ------------------------------------------------------------

RenderPass::RenderPass(HdRenderIndex* pRenderIndex, HdRprimCollection const& collection,
    RenderDelegate* pRenderDelegate) :
    HdRenderPass(pRenderIndex, collection), m_Owner(pRenderDelegate)
{
    // Grab the render context.
    auto pRenderContext = m_Owner->GetRenderContext();

    // Create Rendering Attachments
    // ------------------------------------------------

    Check(CreateRenderingAttachments(pRenderContext, m_ColorAttachment, m_DepthAttachment),
        "Failed to create the rendering attachments.");

    // Configure Descriptor Set Layouts
    // --------------------------------------

    Check(CreatePhysicallyBasedMaterialDescriptorLayout(
              pRenderContext->GetDevice(), m_DescriptorSetLayout),
        "Failed to create a Vulkan Descriptor Set Layout for Physically Based "
        "Materials.");

    // Shader Creation Utility
    // ------------------------------------------------

    auto LoadShader = [&](ShaderID shaderID, const char* filePath,
                          VkShaderCreateInfoEXT vkShaderInfo) {
        std::vector<char> shaderByteCode;
        Check(LoadByteCode(filePath, shaderByteCode),
            std::format("Failed to read shader bytecode: {}", filePath).c_str());

        vkShaderInfo.pName    = "Main";
        vkShaderInfo.pCode    = shaderByteCode.data();
        vkShaderInfo.codeSize = shaderByteCode.size();
        vkShaderInfo.codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT;

        VkShaderEXT vkShader;
        Check(
            vkCreateShadersEXT(pRenderContext->GetDevice(), 1u, &vkShaderInfo, nullptr, &vkShader),
            std::format("Failed to load Vulkan Shader: {}", filePath).c_str());
        Check(!m_ShaderMap.contains(shaderID),
            "Tried to store a Vulkan Shader into an existing shader slot.");

        spdlog::info("Loaded Vulkan Shader: {}", filePath);

        m_ShaderMap[shaderID] = vkShader;
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

    VkPipelineLayoutCreateInfo vkPipelineLayoutInfo = {
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
    };
    {
        vkPipelineLayoutInfo.setLayoutCount         = 1u;
        vkPipelineLayoutInfo.pSetLayouts            = &m_DescriptorSetLayout;
        vkPipelineLayoutInfo.pushConstantRangeCount = 1u;
        vkPipelineLayoutInfo.pPushConstantRanges    = &vkPushConstants;
    }
    Check(vkCreatePipelineLayout(
              pRenderContext->GetDevice(), &vkPipelineLayoutInfo, nullptr, &m_PipelineLayout),
        "Failed to create the default Vulkan Pipeline Layout");

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
    LoadShader(ShaderID::MeshVert, "Mesh.vert.spv", vertexShaderInfo);

    VkShaderCreateInfoEXT litShaderInfo = { VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT };
    {
        litShaderInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;

        litShaderInfo.pushConstantRangeCount = 1u;
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
    auto pRenderContext = m_Owner->GetRenderContext();

    vkDeviceWaitIdle(pRenderContext->GetDevice());

    vkDestroyImageView(pRenderContext->GetDevice(), m_ColorAttachment.imageView, nullptr);
    vkDestroyImageView(pRenderContext->GetDevice(), m_DepthAttachment.imageView, nullptr);

    vmaDestroyImage(
        pRenderContext->GetAllocator(), m_ColorAttachment.image, m_ColorAttachment.imageAllocation);
    vmaDestroyImage(
        pRenderContext->GetAllocator(), m_DepthAttachment.image, m_DepthAttachment.imageAllocation);

    vkDestroyPipelineLayout(pRenderContext->GetDevice(), m_PipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(pRenderContext->GetDevice(), m_DescriptorSetLayout, nullptr);

    for (auto& shader : m_ShaderMap)
        vkDestroyShaderEXT(pRenderContext->GetDevice(), shader.second, nullptr);
}

void RenderPass::_Execute(
    HdRenderPassStateSharedPtr const& renderPassState, TfTokenVector const& renderTags)
{
    // Grab the render context.
    auto pRenderContext = m_Owner->GetRenderContext();

    // Grab a handle to the current frame.
    auto pFrame = m_Owner->GetRenderSetting(kTokenCurrenFrameParams).UncheckedGet<FrameParams*>();

    auto ColorAttachmentBarrier =
        [&](VkCommandBuffer vkCommand, VkImage vkImage, VkImageLayout vkLayoutOld,
            VkImageLayout vkLayoutNew, VkAccessFlags2 vkAccessSrc, VkAccessFlags2 vkAccessDst,
            VkPipelineStageFlags2 vkStageSrc, VkPipelineStageFlags2 vkStageDst) {
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

    ColorAttachmentBarrier(pFrame->cmd, m_ColorAttachment.image, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_2_NONE,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

    VkRenderingInfo vkRenderingInfo = { VK_STRUCTURE_TYPE_RENDERING_INFO };
    {
        vkRenderingInfo.colorAttachmentCount = 1u;
        vkRenderingInfo.pColorAttachments    = &colorAttachmentInfo;
        vkRenderingInfo.pDepthAttachment     = &depthAttachmentInfo;
        vkRenderingInfo.pStencilAttachment   = VK_NULL_HANDLE;
        vkRenderingInfo.layerCount           = 1u;
        vkRenderingInfo.renderArea           = { { 0, 0 }, { kWindowWidth, kWindowHeight } };
    }
    vkCmdBeginRendering(pFrame->cmd, &vkRenderingInfo);

    VkShaderStageFlagBits vkGraphicsShaderStageBits[5] = { VK_SHADER_STAGE_VERTEX_BIT,
        VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
        VK_SHADER_STAGE_GEOMETRY_BIT, VK_SHADER_STAGE_FRAGMENT_BIT };

    VkShaderEXT vkGraphicsShaders[5] = { m_ShaderMap[ShaderID::MeshVert], VK_NULL_HANDLE,
        VK_NULL_HANDLE, VK_NULL_HANDLE, m_ShaderMap[ShaderID::LitFrag] };

    vkCmdBindShadersEXT(pFrame->cmd, 5u, vkGraphicsShaderStageBits, vkGraphicsShaders);

    SetDefaultRenderState(pFrame->cmd);

    vkCmdSetVertexInputEXT(pFrame->cmd, (uint32_t)m_VertexInputBindings.size(),
        m_VertexInputBindings.data(), (uint32_t)m_VertexInputAttributes.size(),
        m_VertexInputAttributes.data());

    auto pScene     = pRenderContext->GetScene();
    auto pResources = std::static_pointer_cast<ResourceRegistry>(m_Owner->GetResourceRegistry());

    // Update camera matrices.
    m_PushConstants._MatrixVP = (GfMatrix4f)renderPassState->GetWorldToViewMatrix() *
        (GfMatrix4f)renderPassState->GetProjectionMatrix();

    for (const auto& pMesh : pScene->GetMeshList())
    {
        std::pair<VkBuffer, VmaAllocation> positionBuffer, normalBuffer, indexBuffer;
        pResources->GetMeshResources(
            pMesh->GetResourceHandle(), positionBuffer, normalBuffer, indexBuffer);

        VmaAllocationInfo allocationInfo;
        vmaGetAllocationInfo(pRenderContext->GetAllocator(), indexBuffer.second, &allocationInfo);

        vkCmdBindIndexBuffer(pFrame->cmd, indexBuffer.first, 0u, VK_INDEX_TYPE_UINT32);

        VkDeviceSize vertexBufferOffset[2] = { 0u, 0u };

        VkBuffer vertexBuffers[2] = { positionBuffer.first, normalBuffer.first };
        vkCmdBindVertexBuffers(pFrame->cmd, 0u, 2u, vertexBuffers, vertexBufferOffset);

        m_PushConstants._MatrixM = pMesh->GetLocalToWorld();
        vkCmdPushConstants(pFrame->cmd, m_PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0u,
            sizeof(PushConstants), &m_PushConstants);

        vkCmdDrawIndexed(
            pFrame->cmd, (uint32_t)allocationInfo.size / sizeof(uint32_t), 1u, 0u, 0u, 0u);
    }

    vkCmdEndRendering(pFrame->cmd);

    // Copy the internal color attachment to back buffer.

    ColorAttachmentBarrier(pFrame->cmd, m_ColorAttachment.image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_2_NONE, VK_ACCESS_2_MEMORY_READ_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT);

    ColorAttachmentBarrier(pFrame->cmd, pFrame->backBuffer, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_2_MEMORY_READ_BIT,
        VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT);

    VkImageCopy backBufferCopy = {};
    {
        backBufferCopy.extent         = { kWindowWidth, kWindowHeight, 1u };
        backBufferCopy.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };
        backBufferCopy.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };
    }
    vkCmdCopyImage(pFrame->cmd, m_ColorAttachment.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        pFrame->backBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &backBufferCopy);

    ColorAttachmentBarrier(pFrame->cmd, pFrame->backBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_2_MEMORY_WRITE_BIT, VK_ACCESS_2_MEMORY_READ_BIT,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
}