#ifndef MESH_H
#define MESH_H

class RenderDelegate;

class Mesh : public HdMesh
{
public:

    Mesh(const SdfPath& rprimId, RenderDelegate* pRenderDelegate) : HdMesh(rprimId), m_Owner(pRenderDelegate) {}
    ~Mesh() override = default;

    HdDirtyBits GetInitialDirtyBitsMask() const override;
    void        Sync(HdSceneDelegate* pSceneDelegate, HdRenderParam* pRenderParam, HdDirtyBits* pDirtyBits, const TfToken& reprToken) override;
    void        Finalize(HdRenderParam* renderParam) override;

    inline const GfMatrix4f& GetLocalToWorld() const { return m_LocalToWorld; }
    inline const size_t&     GetMaterialHash() const { return m_MaterialHash; }

protected:

    HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;
    void        _InitRepr(const TfToken& reprToken, HdDirtyBits* dirtyBits) override;

private:

    RenderDelegate* m_Owner;

    // Store the material id hash.
    // (The parent class does not seem to).
    size_t m_MaterialHash;

    GfMatrix4f m_LocalToWorld {};
};

#endif
