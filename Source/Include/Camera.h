#ifndef CAMERA_H
#define CAMERA_H

class RenderDelegate;

class Camera : public HdCamera
{
public:
    Camera(SdfPath const& sprimId, RenderDelegate* pRenderDelegate) : HdCamera(sprimId), m_Owner(pRenderDelegate) {};
    
    HdDirtyBits GetInitialDirtyBitsMask() const override;
    void Sync(HdSceneDelegate* pSceneDelegate, HdRenderParam* pRenderParam, HdDirtyBits* pDirtyBits) override;

    // const glm::mat4& GetViewProjectionMatrix() { return m_MatrixVP; }
    const GfMatrix4f& GetViewProjectionMatrix() { return m_MatrixVP; }

private:
    RenderDelegate* m_Owner;

    // glm::mat4 m_MatrixVP;
    GfMatrix4f m_MatrixVP;
};

#endif