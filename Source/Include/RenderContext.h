#ifndef RENDER_CONTEXT_H
#define RENDER_CONTEXT_H

// Context for OS-Window, Vulkan Initialization, Swapchain Management.
// ---------------------------------------------------------

const uint32_t kWindowWidth       = 1920u;
const uint32_t kWindowHeight      = 1080u;
const uint32_t kMaxFramesInFlight = 3u;

struct FrameParams;
class Scene;

class RenderContext
{
public:
    RenderContext(uint32_t windowWidth, uint32_t windowHeight);
    ~RenderContext();

    // Dispatch a render loop into the OS window, invoking a provided command recording callback
    // each frame.
    void Dispatch(std::function<void(FrameParams)> commandsFunc);

    inline VkDevice& GetDevice() { return m_VKDeviceLogical; }
    inline VmaAllocator& GetAllocator() { return m_VKMemoryAllocator; }
    inline VkQueue& GetCommandQueue() { return m_VKCommandQueue; }
    inline uint32_t& GetCommandQueueIndex() { return m_VKCommandQueueIndex; }
    inline VkCommandPool& GetCommandPool() { return m_VKCommandPool; }
    inline GLFWwindow* GetWindow() { return m_Window; }
    inline Scene* GetScene() { return m_Scene.get(); }

private:
    VkInstance m_VKInstance;
    VkPhysicalDevice m_VKDevicePhysical;
    VkDevice m_VKDeviceLogical;
    VkDescriptorPool m_VKDescriptorPool;
    VmaAllocator m_VKMemoryAllocator;
    GLFWwindow* m_Window;

    // Command Primitives
    VkCommandPool m_VKCommandPool;
    VkQueue m_VKCommandQueue;
    uint32_t m_VKCommandQueueIndex;

    // Swapchain Primitives
    VkSwapchainKHR m_VKSwapchain;
    VkSurfaceKHR m_VKSurface;
    std::vector<VkImage> m_VKSwapchainImages;
    std::vector<VkImageView> m_VKSwapchainImageViews;

    // Frame Primitives
    VkCommandBuffer m_VKCommandBuffers[kMaxFramesInFlight];
    VkSemaphore m_VKImageAvailableSemaphores[kMaxFramesInFlight];
    VkSemaphore m_VKRenderCompleteSemaphores[kMaxFramesInFlight];
    VkFence m_VKInFlightFences[kMaxFramesInFlight];

    // Scene (Constructed from Hydra)
    std::unique_ptr<Scene> m_Scene;
};

#endif