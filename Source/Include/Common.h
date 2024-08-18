#ifndef COMMON_H
#define COMMON_H

// Logging + crash utility when an assertion fails.
// ---------------------------------------------------------

#ifdef _DEBUG

inline void Check(VkResult a, const char* b)
{
    if (a != VK_SUCCESS)
    {
        spdlog::critical(std::format("{} - [VkResult: {}]", b, std::to_string(a)));
        __debugbreak();
        exit(a);
    }
}
inline void Check(bool a, const char* b)
{
    if (!a)
    {
        spdlog::critical(b);
        __debugbreak();
        exit(1);
    }
}

#else

inline void Check(VkResult a, const char* b)
{
    if (a != VK_SUCCESS)
    {
        spdlog::critical(std::format("{} - [VkResult: {}]", b, std::to_string(a)));
        exit(a);
    }
}

inline void Check(bool a, const char* b)
{
    if (a != true)
    {
        spdlog::critical(b);
        exit(1);
    }
}

#endif

// Profile Macro
// ---------------------------------------------------------

#ifdef USE_SUPERLUMINAL
#define PROFILE_START(x) PerformanceAPI_BeginEvent(x, nullptr, PERFORMANCEAPI_MAKE_COLOR(255, 150, 0)) // NOLINT
#define PROFILE_END      PerformanceAPI_EndEvent()
#else
#define PROFILE_START(x)
#define PROFILE_END
#endif

// Common parameters pushed to all shaders.
// ---------------------------------------------------------

struct PushConstants
{
    GfMatrix4f MatrixM;
    GfMatrix4f MatrixVP;
    GfMatrix4f MatrixV;
};

// Layout of the standard Vertex for this application.
// ---------------------------------------------------------

struct Vertex
{
    glm::vec3 positionOS;
    glm::vec3 normalOS;
    glm::vec2 texCoord0;
};

// Collection of vulkan primitives to hold the current frame state.
// ---------------------------------------------------------

struct FrameParams
{
    VkCommandBuffer cmd;
    VkImage         backBuffer;
    VkImageView     backBufferView;
    double          deltaTime;
};

// Collection of vulkan primitives to hold a buffer.
// ---------------------------------------------------------

struct Buffer
{
    VkBuffer      buffer           = VK_NULL_HANDLE;
    VkBufferView  bufferView       = VK_NULL_HANDLE;
    VmaAllocation bufferAllocation = VK_NULL_HANDLE;
};

// Collection of vulkan primitives to hold an image.
// ---------------------------------------------------------

struct Image
{
    VkImage       image           = VK_NULL_HANDLE;
    VkImageView   imageView       = VK_NULL_HANDLE;
    VmaAllocation imageAllocation = VK_NULL_HANDLE;
};

// Utility Functions.
// ---------------------------------------------------------

class RenderContext;

bool CreatePhysicallyBasedMaterialDescriptorLayout(const VkDevice& vkLogicalDevice, VkDescriptorSetLayout& vkDescriptorSetLayout);

bool SelectVulkanPhysicalDevice(const VkInstance& vkInstance, const std::vector<const char*>& requiredExtensions, VkPhysicalDevice& vkPhysicalDevice);

bool CreateVulkanLogicalDevice(const VkPhysicalDevice&         vkPhysicalDevice,
                               const std::vector<const char*>& requiredExtensions,
                               uint32_t                        vkGraphicsQueueIndex,
                               VkDevice&                       vkLogicalDevice);

bool LoadByteCode(const char* filePath, std::vector<char>& byteCode);

void SetDefaultRenderState(VkCommandBuffer commandBuffer);

bool GetVulkanQueueIndices(const VkInstance& vkInstance, const VkPhysicalDevice& vkPhysicalDevice, uint32_t& vkQueueIndexGraphics);

void GetVertexInputLayout(std::vector<VkVertexInputBindingDescription2EXT>& bindings, std::vector<VkVertexInputAttributeDescription2EXT>& attributes);

bool CreateRenderingAttachments(RenderContext* pRenderContext, Image& colorAttachment, Image& depthAttachment);

void SingleShotCommandBegin(RenderContext* pRenderContext, VkCommandBuffer& vkCommandBuffer);

void SingleShotCommandEnd(RenderContext* pRenderContext, VkCommandBuffer& vkCommandBuffer);

void NameVulkanObject(VkDevice vkLogicalDevice, VkObjectType vkObjectType, uint64_t vkObject, const std::string& vkObjectName);

void DebugLabelImageResource(RenderContext* pRenderContext, const Image& imageResource, const char* labelName);

void DebugLabelBufferResource(RenderContext* pRenderContext, const Buffer& bufferResource, const char* labelName);

void VulkanColorImageBarrier(VkCommandBuffer       vkCommand,
                             VkImage               vkImage,
                             VkImageLayout         vkLayoutOld,
                             VkImageLayout         vkLayoutNew,
                             VkAccessFlags2        vkAccessSrc,
                             VkAccessFlags2        vkAccessDst,
                             VkPipelineStageFlags2 vkStageSrc,
                             VkPipelineStageFlags2 vkStageDst);

void InitializeUserInterface(RenderContext* pRenderContext);
void DrawUserInterface(RenderContext* pRenderContext, uint32_t swapChainImageIndex, VkCommandBuffer cmd, const std::function<void()>& interfaceFunc);

#endif
