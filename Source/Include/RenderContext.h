#ifndef RENDER_CONTEXT_H
#define RENDER_CONTEXT_H

// Context for OS-Window, Vulkan Initialization, Swapchain Management.
// ---------------------------------------------------------

const uint32_t kWindowWidth       = 1920U; // NOLINT
const uint32_t kWindowHeight      = 1080U; // NOLINT
const uint32_t kMaxFramesInFlight = 3U;

struct FrameParams;
class Scene;

class RenderContext
{
public:
    RenderContext(uint32_t windowWidth, uint32_t windowHeight);
    ~RenderContext();

    // Dispatch a render loop into the OS window, invoking a provided command recording callback
    // each frame.
    void Dispatch(const std::function<void(FrameParams)>& commandsFunc);

    inline VkDevice& GetDevice() { return m_VKDeviceLogical; }
    inline VmaAllocator& GetAllocator() { return m_VKMemoryAllocator; }
    inline VkQueue& GetCommandQueue() { return m_VKCommandQueue; }
    inline uint32_t& GetCommandQueueIndex() { return m_VKCommandQueueIndex; }
    inline VkCommandPool& GetCommandPool() { return m_VKCommandPool; }
    inline GLFWwindow* GetWindow() { return m_Window; }
    inline Scene* GetScene() { return m_Scene.get(); }

private:
    VkInstance m_VKInstance             = VK_NULL_HANDLE;
    VkPhysicalDevice m_VKDevicePhysical = VK_NULL_HANDLE;
    VkDevice m_VKDeviceLogical          = VK_NULL_HANDLE;
    VkDescriptorPool m_VKDescriptorPool = VK_NULL_HANDLE;
    VmaAllocator m_VKMemoryAllocator    = VK_NULL_HANDLE;
    GLFWwindow* m_Window;

    // Command Primitives
    VkCommandPool m_VKCommandPool  = VK_NULL_HANDLE;
    VkQueue m_VKCommandQueue       = VK_NULL_HANDLE;
    uint32_t m_VKCommandQueueIndex = UINT_MAX;

    // Swapchain Primitives
    VkSwapchainKHR m_VKSwapchain = VK_NULL_HANDLE;
    VkSurfaceKHR m_VKSurface     = VK_NULL_HANDLE;
    std::vector<VkImage> m_VKSwapchainImages;
    std::vector<VkImageView> m_VKSwapchainImageViews;

    // Frame Primitives
    std::array<VkCommandBuffer, kMaxFramesInFlight> m_VKCommandBuffers {};
    std::array<VkSemaphore, kMaxFramesInFlight> m_VKImageAvailableSemaphores {};
    std::array<VkSemaphore, kMaxFramesInFlight> m_VKRenderCompleteSemaphores {};
    std::array<VkFence, kMaxFramesInFlight> m_VKInFlightFences {};

    // Scene (Constructed from Hydra)
    std::unique_ptr<Scene> m_Scene;
};

#endif