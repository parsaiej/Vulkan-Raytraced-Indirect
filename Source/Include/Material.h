#ifndef MATERIAL_H
#define MATERIAL_H

class RenderDelegate;

class Material final : public HdMaterial
{
public:

    Material(const SdfPath& id, RenderDelegate* pOwner) : HdMaterial(id), m_Owner(pOwner) {}

    // MaterialX Standard Surface
    constexpr static const char* kMaterialInputBaseColor = "base_color";
    constexpr static const char* kMaterialInputNormal    = "normal";
    constexpr static const char* kMaterialInputRoughness = "specular_roughness";
    constexpr static const char* kMaterialInputMetallic  = "metalness";

    void Sync(HdSceneDelegate* pSceneDelegate, HdRenderParam* pRenderParam, HdDirtyBits* pDirtyBits) override;

    inline const uint64_t& GetResourceHandle() const { return m_ResourceHandle; }

    [[nodiscard]] HdDirtyBits GetInitialDirtyBitsMask() const override;

private:

    RenderDelegate* m_Owner;

    uint64_t m_ResourceHandle {};
};

#endif
