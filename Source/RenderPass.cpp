#include <Mesh.h>
#include <RenderContext.h>
#include <RenderDelegate.h>
#include <RenderPass.h>
#include <ResourceRegistry.h>
#include <Scene.h>
#include <Material.h>

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

    // Configure Descriptor Set Layouts
    // --------------------------------------

    Check(CreatePhysicallyBasedMaterialDescriptorLayout(pRenderContext->GetDevice(), m_DescriptorSetLayout),
          "Failed to create a Vulkan Descriptor Set Layout for Physically Based "
          "Materials.");

    // Obtain the resource registry
    auto pResourceRegistry = std::static_pointer_cast<ResourceRegistry>(m_Owner->GetResourceRegistry());

    // Shader Creation Utility
    // ------------------------------------------------

    auto LoadShader = [&](ShaderID shaderID, const char* filePath, VkShaderCreateInfoEXT vkShaderInfo)
    {
        std::vector<char> shaderByteCode;
        Check(LoadByteCode(filePath, shaderByteCode), std::format("Failed to read shader bytecode: {}", filePath).c_str());

        vkShaderInfo.pName    = "Main";
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
    Check(vkCreatePipelineLayout(pRenderContext->GetDevice(), &vkPipelineLayoutInfo, nullptr, &m_PipelineLayout),
          "Failed to create the default Vulkan Pipeline Layout");

    // Create Shader Objects
    // --------------------------------------

    VkShaderCreateInfoEXT vertexShaderInfo = { VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT };
    {
        vertexShaderInfo.stage     = VK_SHADER_STAGE_VERTEX_BIT;
        vertexShaderInfo.nextStage = VK_SHADER_STAGE_FRAGMENT_BIT;

        vertexShaderInfo.pushConstantRangeCount = 1U;
        vertexShaderInfo.pPushConstantRanges    = &vkPushConstants;
        vertexShaderInfo.setLayoutCount         = 1U;
        vertexShaderInfo.pSetLayouts            = &m_DescriptorSetLayout;
    }
    LoadShader(ShaderID::FullscreenTriangleVert, "FullscreenTriangle.vert.spv", vertexShaderInfo);
    LoadShader(ShaderID::MeshVert, "Mesh.vert.spv", vertexShaderInfo);

    VkShaderCreateInfoEXT litShaderInfo = { VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT };
    {
        litShaderInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;

        litShaderInfo.pushConstantRangeCount = 1U;
        litShaderInfo.pPushConstantRanges    = &vkPushConstants;
        litShaderInfo.setLayoutCount         = 1U;
        litShaderInfo.pSetLayouts            = &m_DescriptorSetLayout;
    }
    LoadShader(ShaderID::LitFrag, "Lit.frag.spv", litShaderInfo);

    // Vertex Input Layout
    // ------------------------------------------------

    GetVertexInputLayout(m_VertexInputBindings, m_VertexInputAttributes);

    // Configure Push Constants
    // ------------------------------------------------

    m_PushConstants = {};

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

    vkDestroySampler(pRenderContext->GetDevice(), m_DefaultSampler, nullptr);
}

void CreateMaterialDescriptor(RenderContext*                      pRenderContext,
                              VkSampler                           defaultSampler,
                              ResourceRegistry::MaterialResources materialResources,
                              VkDescriptorSetLayout               vkDescriptorSetLayout,
                              VkDescriptorSet*                    pDescriptorSet)
{
    VkDescriptorSetAllocateInfo descriptorSetAllocationInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    {
        descriptorSetAllocationInfo.descriptorPool     = pRenderContext->GetDescriptorPool();
        descriptorSetAllocationInfo.descriptorSetCount = 1U;
        descriptorSetAllocationInfo.pSetLayouts        = &vkDescriptorSetLayout;
    }
    Check(vkAllocateDescriptorSets(pRenderContext->GetDevice(), &descriptorSetAllocationInfo, pDescriptorSet),
          "Failed to allocate material descriptors.");

    // Update the descriptor sets.
    std::vector<VkDescriptorImageInfo> descriptorImageInfos;
    std::vector<VkWriteDescriptorSet>  descriptorSetWrites;

    // Import to note invalidate the back() pointer as we push images.
    descriptorImageInfos.reserve(4U);

    auto PushSampledImage = [&](const Image& sampledImage)
    {
        descriptorImageInfos.push_back(VkDescriptorImageInfo(VK_NULL_HANDLE, sampledImage.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));

        VkWriteDescriptorSet writeInfo = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        {
            writeInfo.descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            writeInfo.descriptorCount = 1U;
            writeInfo.dstBinding      = static_cast<uint32_t>(descriptorSetWrites.size());
            writeInfo.dstSet          = *pDescriptorSet;
            writeInfo.pImageInfo      = &descriptorImageInfos.back();
        }
        descriptorSetWrites.push_back(writeInfo);
    };

    // WARNING: Match the layout defined in Common::CreatePhysicallyBasedMaterialDescriptorLayout
    PushSampledImage(materialResources.albedo);
    PushSampledImage(materialResources.normal);
    PushSampledImage(materialResources.metallic);
    PushSampledImage(materialResources.roughness);

    // Also push the sampler.

    VkDescriptorImageInfo descriptorSamplerInfo;
    {
        descriptorSamplerInfo.sampler = defaultSampler;
    }

    VkWriteDescriptorSet writeInfo = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    {
        writeInfo.descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLER;
        writeInfo.descriptorCount = 1U;
        writeInfo.dstBinding      = static_cast<uint32_t>(descriptorSetWrites.size());
        writeInfo.dstSet          = *pDescriptorSet;
        writeInfo.pImageInfo      = &descriptorSamplerInfo;
    }
    descriptorSetWrites.push_back(writeInfo);

    vkUpdateDescriptorSets(pRenderContext->GetDevice(), static_cast<uint32_t>(descriptorSetWrites.size()), descriptorSetWrites.data(), 0U, nullptr);
}

void RenderPass::_Execute(const HdRenderPassStateSharedPtr& renderPassState, const TfTokenVector& renderTags)
{
    // Grab the render context.
    auto* pRenderContext = m_Owner->GetRenderContext();

    // Grab a handle to the current frame.
    auto* pFrame = m_Owner->GetRenderSetting(kTokenCurrenFrameParams).UncheckedGet<FrameParams*>();

    // Configure Attachments
    // --------------------------------------------

    VkRenderingAttachmentInfo colorAttachmentInfo = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    {
        colorAttachmentInfo.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachmentInfo.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachmentInfo.imageView   = m_ColorAttachment.imageView;
        colorAttachmentInfo.clearValue  = { { { 0.0, 0.0, 0.0, 1.0 } } };
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

    VulkanColorImageBarrier(pFrame->cmd,
                            m_ColorAttachment.image,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_ACCESS_2_MEMORY_READ_BIT,
                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

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
    vkCmdBeginRendering(pFrame->cmd, &vkRenderingInfo);

    std::array<VkShaderStageFlagBits, 5> vkGraphicsShaderStageBits = { VK_SHADER_STAGE_VERTEX_BIT,
                                                                       VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
                                                                       VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
                                                                       VK_SHADER_STAGE_GEOMETRY_BIT,
                                                                       VK_SHADER_STAGE_FRAGMENT_BIT };

    std::array<VkShaderEXT, 5> vkGraphicsShaders = { m_ShaderMap[ShaderID::MeshVert],
                                                     VK_NULL_HANDLE,
                                                     VK_NULL_HANDLE,
                                                     VK_NULL_HANDLE,
                                                     m_ShaderMap[ShaderID::LitFrag] };

    vkCmdBindShadersEXT(pFrame->cmd,
                        static_cast<uint32_t>(vkGraphicsShaderStageBits.size()),
                        vkGraphicsShaderStageBits.data(),
                        vkGraphicsShaders.data());

    SetDefaultRenderState(pFrame->cmd);

    vkCmdSetVertexInputEXT(pFrame->cmd,
                           static_cast<uint32_t>(m_VertexInputBindings.size()),
                           m_VertexInputBindings.data(),
                           static_cast<uint32_t>(m_VertexInputAttributes.size()),
                           m_VertexInputAttributes.data());

    auto* pScene     = pRenderContext->GetScene();
    auto  pResources = std::static_pointer_cast<ResourceRegistry>(m_Owner->GetResourceRegistry());

    // Update camera matrices.
    m_PushConstants.MatrixVP = GfMatrix4f(renderPassState->GetWorldToViewMatrix()) * GfMatrix4f(renderPassState->GetProjectionMatrix());
    m_PushConstants.MatrixV  = GfMatrix4f(renderPassState->GetWorldToViewMatrix());

    PROFILE_START("Record Mesh Rendering Commands");

    auto RenderSceneMeshList = [&]()
    {
        for (const auto& pMesh : pScene->GetMeshList())
        {
            ResourceRegistry::MeshResources mesh;
            if (!pResources->GetMeshResources(pMesh->GetResourceHandle(), mesh))
                return;

            if (pMesh->GetMaterialHash() != UINT_MAX)
            {
                auto* pMaterialDescriptor = &m_MaterialDescriptors[pMesh->GetMaterialHash()];

                if (*pMaterialDescriptor == VK_NULL_HANDLE)
                {
                    ResourceRegistry::MaterialResources material;
                    pResources->GetMaterialResources(pMesh->GetMaterialHash(), material);

                    // Build the descriptor if it doesn't exit.
                    CreateMaterialDescriptor(pRenderContext, m_DefaultSampler, material, m_DescriptorSetLayout, pMaterialDescriptor);
                }

                // Bind material.
                vkCmdBindDescriptorSets(pFrame->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout, 0U, 1U, pMaterialDescriptor, 0U, nullptr);
            }

            VmaAllocationInfo allocationInfo;
            vmaGetAllocationInfo(pRenderContext->GetAllocator(), mesh.indices.bufferAllocation, &allocationInfo);

            vkCmdBindIndexBuffer(pFrame->cmd, mesh.indices.buffer, 0U, VK_INDEX_TYPE_UINT32);

            std::array<VkDeviceSize, 3> vertexBufferOffset = { 0U, 0U, 0U };
            std::array<VkBuffer, 3>     vertexBuffers      = { mesh.positions.buffer, mesh.normals.buffer, mesh.texCoords.buffer };

            vkCmdBindVertexBuffers(pFrame->cmd, 0U, 3U, vertexBuffers.data(), vertexBufferOffset.data());

            m_PushConstants.MatrixM = pMesh->GetLocalToWorld();
            vkCmdPushConstants(pFrame->cmd, m_PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0U, sizeof(PushConstants), &m_PushConstants);

            vkCmdDrawIndexed(pFrame->cmd, static_cast<uint32_t>(allocationInfo.size) / sizeof(uint32_t), 1U, 0U, 0U, 0U);
        }
    };

    // Warning: not really robust yet since the meshes could be added to scene before resource system is kicked off.
    if (!pResources->IsBusy())
        RenderSceneMeshList();

    PROFILE_END;

    vkCmdEndRendering(pFrame->cmd);

    // Copy the internal color attachment to back buffer.

    VulkanColorImageBarrier(pFrame->cmd,
                            m_ColorAttachment.image,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_ACCESS_2_TRANSFER_READ_BIT,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                            VK_PIPELINE_STAGE_2_TRANSFER_BIT);

    VulkanColorImageBarrier(pFrame->cmd,
                            pFrame->backBuffer,
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

    vkCmdCopyImage(pFrame->cmd,
                   m_ColorAttachment.image,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   pFrame->backBuffer,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1U,
                   &backBufferCopy);

    VulkanColorImageBarrier(pFrame->cmd,
                            pFrame->backBuffer,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                            VK_ACCESS_2_MEMORY_WRITE_BIT,
                            VK_ACCESS_2_MEMORY_READ_BIT,
                            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
}
