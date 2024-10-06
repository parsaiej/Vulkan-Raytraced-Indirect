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

    // Brixelizer utility.
    inline const FfxBrixelizerAABB& GetAABB() const { return m_AABB; }
    inline const FfxFloat32x3x4&    GetLocalToWorld3x4() const { return m_LocalToWorld3x4; }

protected:

    HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;
    void        _InitRepr(const TfToken& reprToken, HdDirtyBits* dirtyBits) override;

private:

    RenderDelegate* m_Owner;

    // Store the material id hash.
    // (The parent class does not seem to).
    size_t m_MaterialHash;

    // For Brixelizer acceleration structure.
    FfxBrixelizerAABB m_AABB;

    GfMatrix4f     m_LocalToWorld {};
    FfxFloat32x3x4 m_LocalToWorld3x4 {};
};

#endif
