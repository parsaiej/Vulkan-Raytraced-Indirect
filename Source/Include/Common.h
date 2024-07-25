#ifndef COMMON_H
#define COMMON_H

// Logging + crash utility when an assertion fails.
// ---------------------------------------------------------

#ifdef _DEBUG
    inline void Check(VkResult a, const char* b) { if (a != VK_SUCCESS) { spdlog::critical(b); __debugbreak(); exit(a); } }
    inline void Check(bool     a, const char* b) { if (a != true)       { spdlog::critical(b); __debugbreak(); exit(1); } }
#else
    inline void Check(VkResult a, const char* b) { if (a != VK_SUCCESS) { spdlog::critical(b); exit(a); } }
    inline void Check(bool     a, const char* b) { if (a != true)       { spdlog::critical(b); exit(1); } }
#endif

// Common parameters pushed to all shaders.
// ---------------------------------------------------------

struct PushConstants
{
    GfMatrix4f _MatrixVP; // glm::mat4 _MatrixVP;
    GfMatrix4f _MatrixM; // glm::mat4 _MatrixM;
    float     _Time;
    glm::vec3 _Color;
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

// Collection of vulkan primitives to hold an image.
// ---------------------------------------------------------

struct Image
{
    VkImage       image;
    VkImageView   imageView;
    VmaAllocation imageAllocation;  
};

// Utility Functions.
// ---------------------------------------------------------

class RenderContext;

bool CreatePhysicallyBasedMaterialDescriptorLayout  (const VkDevice& vkLogicalDevice, VkDescriptorSetLayout& vkDescriptorSetLayout);
bool SelectVulkanPhysicalDevice                     (const VkInstance& vkInstance, const std::vector<const char*> requiredExtensions, VkPhysicalDevice& vkPhysicalDevice);
bool CreateVulkanLogicalDevice                      (const VkPhysicalDevice& vkPhysicalDevice, const std::vector<const char*>& requiredExtensions, uint32_t vkGraphicsQueueIndex, VkDevice& vkLogicalDevice);
bool LoadByteCode                                   (const char* filePath, std::vector<char>& byteCode);
void SetDefaultRenderState                          (VkCommandBuffer commandBuffer);
bool GetVulkanQueueIndices                          (const VkInstance& vkInstance, const VkPhysicalDevice& vkPhysicalDevice, uint32_t& vkQueueIndexGraphics);
void GetVertexInputLayout                           (std::vector<VkVertexInputBindingDescription2EXT>& bindings, std::vector<VkVertexInputAttributeDescription2EXT>& attributes);
bool CreateRenderingAttachments                     (RenderContext* pRenderContext, Image& colorAttachment, Image& depthAttachment);
void NameVulkanObject                               (VkDevice vkLogicalDevice, VkObjectType vkObjectType, uint64_t vkObject, std::string vkObjectName);

#endif