#include <RenderPass.h>
#include <RenderContext.h>
#include <RenderDelegate.h>
#include <Scene.h>

// Render Pass Implementation
// ------------------------------------------------------------

RenderPass::RenderPass(HdRenderIndex* pRenderIndex, HdRprimCollection const &collection, RenderDelegate* pRenderDelegate) : HdRenderPass(pRenderIndex, collection), m_Owner(pRenderDelegate)
{
    // Grab the render context.
    auto pRenderContext = m_Owner->GetRenderContext();

    // Create Rendering Attachments
    // ------------------------------------------------

    Check(CreateRenderingAttachments(pRenderContext, m_ColorAttachment, m_DepthAttachment), "Failed to create the rendering attachments.");
}

RenderPass::~RenderPass()
{
    // Grab the render context.
    auto pRenderContext = m_Owner->GetRenderContext();

    vkDeviceWaitIdle(pRenderContext->GetDevice());

    vkDestroyImageView(pRenderContext->GetDevice(), m_ColorAttachment.imageView, nullptr);
    vkDestroyImageView(pRenderContext->GetDevice(), m_DepthAttachment.imageView, nullptr);

    vmaDestroyImage(pRenderContext->GetAllocator(), m_ColorAttachment.image, m_ColorAttachment.imageAllocation);
    vmaDestroyImage(pRenderContext->GetAllocator(), m_DepthAttachment.image, m_DepthAttachment.imageAllocation);
}

void RenderPass::_Execute(HdRenderPassStateSharedPtr const& renderPassState, TfTokenVector const &renderTags)
{   
    // Grab the render context.
    auto pRenderContext = m_Owner->GetRenderContext();

    // Grab a handle to the current frame. 
    auto pFrame = m_Owner->GetRenderSetting(kTokenCurrenFrameParams).UncheckedGet<FrameParams*>();

    auto ColorAttachmentBarrier = [&](VkCommandBuffer vkCommand, VkImage vkImage, 
        VkImageLayout         vkLayoutOld, 
        VkImageLayout         vkLayoutNew, 
        VkAccessFlags2        vkAccessSrc,
        VkAccessFlags2        vkAccessDst,
        VkPipelineStageFlags2 vkStageSrc,
        VkPipelineStageFlags2 vkStageDst)
    {
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
            vkImageBarrier.image                = vkImage;		
            vkImageBarrier.oldLayout            = vkLayoutOld;
            vkImageBarrier.newLayout            = vkLayoutNew;
            vkImageBarrier.srcAccessMask        = vkAccessSrc;
            vkImageBarrier.dstAccessMask        = vkAccessDst;
            vkImageBarrier.srcStageMask         = vkStageSrc;
            vkImageBarrier.dstStageMask         = vkStageDst;
            vkImageBarrier.srcQueueFamilyIndex  = pRenderContext->GetCommandQueueIndex();
            vkImageBarrier.dstQueueFamilyIndex  = pRenderContext->GetCommandQueueIndex();
            vkImageBarrier.subresourceRange     = imageSubresource;
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
        colorAttachmentInfo.clearValue  = {{{ 0.25, 0.5, 1.0, 1.0 }}};
        colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachmentInfo.imageView   = m_ColorAttachment.imageView;
    } 

    VkRenderingAttachmentInfo depthAttachmentInfo = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    {
        depthAttachmentInfo.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachmentInfo.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthAttachmentInfo.imageView   = m_DepthAttachment.imageView;
        depthAttachmentInfo.clearValue.depthStencil = { 1.0, 0x0 };
    } 

    // Record
    // --------------------------------------------

    ColorAttachmentBarrier
    (
        pFrame->cmd, m_ColorAttachment.image, 
        VK_IMAGE_LAYOUT_UNDEFINED, 
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 
        VK_ACCESS_2_NONE, 
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, 
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
    );

    VkRenderingInfo vkRenderingInfo = { VK_STRUCTURE_TYPE_RENDERING_INFO };
    {
        vkRenderingInfo.colorAttachmentCount = 1u;
        vkRenderingInfo.pColorAttachments    = &colorAttachmentInfo;
        vkRenderingInfo.pDepthAttachment     = &depthAttachmentInfo;
        vkRenderingInfo.pStencilAttachment   = VK_NULL_HANDLE;
        vkRenderingInfo.layerCount           = 1u;
        vkRenderingInfo.renderArea           = { {0, 0}, { kWindowWidth, kWindowHeight } };
    }
    vkCmdBeginRendering(pFrame->cmd, &vkRenderingInfo);

    for (const auto& pMesh : m_Owner->GetRenderContext()->GetScene()->GetMeshList())
    {
        spdlog::info("Rendering Mesh...");
    }

    vkCmdEndRendering(pFrame->cmd);

    // Copy the internal color attachment to back buffer. 

    ColorAttachmentBarrier
    (
        pFrame->cmd, m_ColorAttachment.image, 
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 
        VK_ACCESS_2_NONE, 
        VK_ACCESS_2_MEMORY_READ_BIT, 
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 
        VK_PIPELINE_STAGE_2_TRANSFER_BIT
    );

    ColorAttachmentBarrier
    (
        pFrame->cmd, pFrame->backBuffer,
        VK_IMAGE_LAYOUT_UNDEFINED, 
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
        VK_ACCESS_2_MEMORY_READ_BIT, 
        VK_ACCESS_2_MEMORY_WRITE_BIT, 
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 
        VK_PIPELINE_STAGE_2_TRANSFER_BIT
    );

    VkImageCopy backBufferCopy = {};
    {
        backBufferCopy.extent         = { kWindowWidth, kWindowHeight, 1u};
        backBufferCopy.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };
        backBufferCopy.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };
    }
    vkCmdCopyImage(pFrame->cmd, m_ColorAttachment.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, pFrame->backBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &backBufferCopy);

    ColorAttachmentBarrier
    (
        pFrame->cmd, pFrame->backBuffer, 
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 
        VK_ACCESS_2_MEMORY_WRITE_BIT, 
        VK_ACCESS_2_MEMORY_READ_BIT, 
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, 
        VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT
    );
}