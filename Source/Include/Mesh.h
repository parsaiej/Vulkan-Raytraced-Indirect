#ifndef MESH_H
#define MESH_H

class RenderDelegate;

class Mesh : public HdMesh
{
public:
    Mesh(SdfPath const& rprimId, RenderDelegate* pRenderDelegate) : HdMesh(rprimId), m_Owner(pRenderDelegate) {}

    ~Mesh() override = default;

    HdDirtyBits GetInitialDirtyBitsMask() const override;

    void Sync(HdSceneDelegate* pSceneDelegate, HdRenderParam* pRenderParam, HdDirtyBits* pDirtyBits, TfToken const& reprToken) override;

    void Finalize(HdRenderParam* renderParam) override;

    inline const uint64_t& GetResourceHandle() const { return m_ResourceHandle; }

    inline const GfMatrix4f& GetLocalToWorld() const { return m_LocalToWorld; }

protected:
    HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;

    void _InitRepr(TfToken const& reprToken, HdDirtyBits* dirtyBits) override;

private:
    RenderDelegate* m_Owner;

    uint64_t m_ResourceHandle {};

    glm::vec3 m_DebugColor {};

    GfMatrix4f m_LocalToWorld {};
};

#endif