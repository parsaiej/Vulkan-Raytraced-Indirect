#ifndef MESH_H
#define MESH_H

class RenderDelegate;

class Mesh : public HdMesh
{
public:
    Mesh(SdfPath const& rprimId, RenderDelegate* renderDelegate);
    ~Mesh() override;

    HdDirtyBits GetInitialDirtyBitsMask() const override;
    void Sync(HdSceneDelegate* pSceneDelegate, HdRenderParam* pRenderParam, HdDirtyBits* pDirtyBits, TfToken const& reprToken) override;
    void Finalize(HdRenderParam* renderParam) override;

    inline uint64_t GetResourceHandle() { return m_ResourceHandle; }

protected:
    HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;
    void _InitRepr(TfToken const& reprToken, HdDirtyBits* dirtyBits) override;

private:
    RenderDelegate* m_Owner;

    uint64_t m_ResourceHandle;

    std::pair<VkBuffer, VmaAllocation> m_VertexBuffer;
    std::pair<VkBuffer, VmaAllocation> m_IndexBuffer;
};

#endif