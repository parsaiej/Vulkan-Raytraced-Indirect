#include <Camera.h>
#include <RenderDelegate.h>
#include <RenderContext.h>
#include <Scene.h>

HdDirtyBits Camera::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::AllSceneDirtyBits;
}

void Camera::Sync(HdSceneDelegate* pSceneDelegate, HdRenderParam* pRenderParams, HdDirtyBits* pDirtyBits)
{
    if (!(*pDirtyBits & HdChangeTracker::AllSceneDirtyBits))
        return;

    HdCamera::Sync(pSceneDelegate, pRenderParams, pDirtyBits);

    // TODO: Might be able to remove glm from the project completely and just use the USD native matrix type.
    // glm::mat4 view = glm::transpose(glm::make_mat4(GetTransform().data()));
    // glm::mat4 proj = glm::transpose(glm::make_mat4(ComputeProjectionMatrix().data()));

    GfMatrix4f view = (GfMatrix4f)GetTransform();
    GfMatrix4f proj = (GfMatrix4f)ComputeProjectionMatrix();

    // Y-flip
    // proj[1][1] *= -1;

    // Compose the matrix.
    m_MatrixVP = view * proj;

    // spdlog::info("Camera View: {}", glm::to_string(view));
    // spdlog::info("Camera Proj: {}", glm::to_string(proj));
    
    m_Owner->GetRenderContext()->GetScene()->AddCamera(this);

    // Clear the dirty bits.
    *pDirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
}
