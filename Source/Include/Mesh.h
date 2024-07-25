#ifndef MESH_H
#define MESH_H

class RenderDelegate;

class Mesh : public HdMesh
{
public:
    Mesh(SdfPath const& rprimId, RenderDelegate* pRenderDelegate) : HdMesh(rprimId), m_Owner(pRenderDelegate) {}
    ~Mesh() override {};

    HdDirtyBits GetInitialDirtyBitsMask() const override;
    void Sync(HdSceneDelegate* pSceneDelegate, HdRenderParam* pRenderParam, HdDirtyBits* pDirtyBits, TfToken const& reprToken) override;
    void Finalize(HdRenderParam* renderParam) override;

    inline const uint64_t&  GetResourceHandle() const { return m_ResourceHandle; }
    inline const glm::vec3& GetDebugColor()     const { return m_DebugColor; } 
    // inline const glm::mat4& GetLocalToWorld()   const { return m_LocalToWorld; }
    inline const GfMatrix4f& GetLocalToWorld()   const { return m_LocalToWorld; }

protected:
    HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;
    void _InitRepr(TfToken const& reprToken, HdDirtyBits* dirtyBits) override;

private:

    RenderDelegate* m_Owner;

    uint64_t m_ResourceHandle;

    glm::vec3 m_DebugColor;

    // glm::mat4 m_LocalToWorld;
    GfMatrix4f m_LocalToWorld;

    std::pair<VkBuffer, VmaAllocation> m_VertexBuffer;
    std::pair<VkBuffer, VmaAllocation> m_IndexBuffer;
};

#endif