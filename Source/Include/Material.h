#ifndef MATERIAL_H
#define MATERIAL_H

class Material final : public HdMaterial
{
public:
    Material(SdfPath const& id) : HdMaterial(id) {}

    void Sync(HdSceneDelegate* pSceneDelegate, HdRenderParam* pRenderParam, HdDirtyBits* pDirtyBits) override;

    HdDirtyBits GetInitialDirtyBitsMask() const override;
};

#endif