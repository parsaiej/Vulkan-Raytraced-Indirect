#ifndef MESH_H
#define MESH_H

class RenderDelegate;

class Mesh : public HdMesh
{
public:

    Mesh(const SdfPath& rprimId, RenderDelegate* pRenderDelegate) : HdMesh(rprimId), m_Owner(pRenderDelegate), m_MaterialHash(UINT64_MAX) {}
    ~Mesh() override = default;

    HdDirtyBits GetInitialDirtyBitsMask() const override;
    void        Sync(HdSceneDelegate* pSceneDelegate, HdRenderParam* pRenderParam, HdDirtyBits* pDirtyBits, const TfToken& reprToken) override;
    void        Finalize(HdRenderParam* renderParam) override;

    inline const uint64_t&   GetResourceHandle() const { return m_ResourceHandle; }
    inline const GfMatrix4f& GetLocalToWorld() const { return m_LocalToWorld; }
    inline const uint64_t&   GetMaterialHash() const { return m_MaterialHash; }
    inline const uint32_t&   GetIndexCount() const { return m_IndexCount; }

protected:

    HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;
    void        _InitRepr(const TfToken& reprToken, HdDirtyBits* dirtyBits) override;

private:

    RenderDelegate* m_Owner;

    uint64_t m_ResourceHandle {};

    glm::vec3 m_DebugColor {};

    GfMatrix4f m_LocalToWorld {};

    uint64_t m_MaterialHash;

    uint32_t m_IndexCount;
};

#endif
