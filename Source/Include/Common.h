#ifndef COMMON_H
#define COMMON_H

// Limits
// ---------------------------------------------------------

constexpr uint64_t kHostBufferPoolMaxBytes = 512LL * 1024 * 1024;
constexpr uint64_t kHostImagePoolMaxBytes  = 2048LL * 1024 * 1024;

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

inline void Check(FfxErrorCode a, const char* b)
{
    if (a != FFX_OK)
    {
        spdlog::critical(b);
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

inline void Check(FfxErrorCode a, const char* b)
{
    if (a != FFX_OK)
    {
        spdlog::critical(b);
        exit(1);
    }
}

#endif

// CPU Profile Macro
// ---------------------------------------------------------

#ifdef USE_SUPERLUMINAL
#define PROFILE_START(x) PerformanceAPI_BeginEvent(x, nullptr, PERFORMANCEAPI_MAKE_COLOR(255, 150, 0)) // NOLINT
#define PROFILE_END      PerformanceAPI_EndEvent()
#else
#define PROFILE_START(x)
#define PROFILE_END
#endif

// GPU Profile RAII
// ---------------------------------------------------------

class GPUProfileScope
{
private:

    VkCommandBuffer m_Cmd;

public:

    explicit GPUProfileScope(VkCommandBuffer cmd, const char* label) : m_Cmd(cmd)
    {
        VkDebugUtilsLabelEXT startLabel = {};
        startLabel.sType                = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        startLabel.pLabelName           = label;
        startLabel.color[0]             = 1.0F; // Red
        startLabel.color[1]             = 0.0F;
        startLabel.color[2]             = 0.0F;
        startLabel.color[3]             = 1.0F;
        vkCmdBeginDebugUtilsLabelEXT(m_Cmd, &startLabel);
    }

    ~GPUProfileScope() { vkCmdEndDebugUtilsLabelEXT(m_Cmd); }
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
    VkBuffer           buffer           = VK_NULL_HANDLE;
    VkBufferView       bufferView       = VK_NULL_HANDLE;
    VmaAllocation      bufferAllocation = VK_NULL_HANDLE;
    VkBufferCreateInfo bufferInfo       = {};
};

// Collection of vulkan primitives to hold an image.
// ---------------------------------------------------------

struct Image
{
    VkImage           image           = VK_NULL_HANDLE;
    VkImageView       imageView       = VK_NULL_HANDLE;
    VmaAllocation     imageAllocation = VK_NULL_HANDLE;
    VkImageCreateInfo imageInfo       = {};
};

// Utility Functions.
// ---------------------------------------------------------

class RenderContext;

bool CreatePhysicallyBasedMaterialDescriptorLayout(const VkDevice& vkLogicalDevice, VkDescriptorSetLayout& vkDescriptorSetLayout);

bool CreateMeshDataDescriptorLayout(const VkDevice& vkLogicalDevice, VkDescriptorSetLayout& vkDescriptorSetLayout);

bool SelectVulkanPhysicalDevice(const VkInstance& vkInstance, const std::vector<const char*>& requiredExtensions, VkPhysicalDevice& vkPhysicalDevice);

bool CreateVulkanLogicalDevice(const VkPhysicalDevice&         vkPhysicalDevice,
                               const std::vector<const char*>& requiredExtensions,
                               uint32_t                        vkGraphicsQueueIndex,
                               VkDevice&                       vkLogicalDevice);

bool LoadByteCode(const char* filePath, std::vector<char>& byteCode);

void SetDefaultRenderState(VkCommandBuffer commandBuffer);

bool GetVulkanQueueIndices(const VkInstance& vkInstance, const VkPhysicalDevice& vkPhysicalDevice, uint32_t& vkQueueIndexGraphics);

bool CreateRenderingAttachments(RenderContext* pRenderContext, Image& colorAttachment, Image& depthAttachment);

void SingleShotCommandBegin(RenderContext* pRenderContext, VkCommandBuffer& vkCommandBuffer, VkCommandPool vkCommandPool = VK_NULL_HANDLE);

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

void BindGraphicsShaders(VkCommandBuffer cmd, VkShaderEXT vkVertexShader, VkShaderEXT vkFragmentShader);

void InterleaveImageAlpha(stbi_uc** pImageData, int& width, int& height, int& channels);

#endif
