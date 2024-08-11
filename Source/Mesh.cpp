#include <Common.h>
#include <Mesh.h>
#include <RenderContext.h>
#include <RenderDelegate.h>
#include <ResourceRegistry.h>
#include <Scene.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

HdDirtyBits Mesh::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::AllSceneDirtyBits;
}

void Mesh::Sync(HdSceneDelegate* pSceneDelegate, HdRenderParam* pRenderParams, HdDirtyBits* pDirtyBits, TfToken const& reprToken)
{
    if ((*pDirtyBits & HdChangeTracker::AllSceneDirtyBits) == 0U)
        return;

    std::lock_guard<std::mutex> renderContextLock(m_Owner->GetRenderContextMutex());

    SdfPath id = GetId();

    // Set debug color
    {
        m_DebugColor = { 1.0F, 0.0F, 0.0F };
    }

    auto pPointList  = pSceneDelegate->Get(id, HdTokens->points).Get<VtVec3fArray>();
    auto pNormalList = pSceneDelegate->Get(id, HdTokens->normals).Get<VtVec3fArray>();
    auto pIndexList  = VtVec3iArray();

    // Compute the triangulated indices from the mesh topology.
    HdMeshTopology topology = pSceneDelegate->GetMeshTopology(id);

    // Initialize the mesh util.
    HdMeshUtil meshUtil(&topology, id);

    VtIntArray trianglePrimitiveParams;
    meshUtil.ComputeTriangleIndices(&pIndexList, &trianglePrimitiveParams);

    // Obtain the resource registry.
    auto pResourceRegistry = std::static_pointer_cast<ResourceRegistry>(pSceneDelegate->GetRenderIndex().GetResourceRegistry());
    {
        m_ResourceHandle = pResourceRegistry->PushMeshRequest({ id, pPointList, pNormalList, pIndexList });
    }

    // Get the world matrix.
    // m_LocalToWorld = glm::transpose(glm::make_mat4(pSceneDelegate->GetTransform(id).data()));
    m_LocalToWorld = GfMatrix4f(pSceneDelegate->GetTransform(id));

    m_Owner->GetRenderContext()->GetScene()->AddMesh(this);

    // Clear the dirty bits.
    *pDirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
}

HdDirtyBits Mesh::_PropagateDirtyBits(HdDirtyBits bits) const
{
    return bits;
}

void Mesh::_InitRepr(TfToken const& reprToken, HdDirtyBits* pDirtyBits)
{
    auto it = std::find_if(_reprs.begin(), _reprs.end(), _ReprComparator(reprToken));

    if (it == _reprs.end())
        _reprs.emplace_back(reprToken, HdReprSharedPtr());
}

void Mesh::Finalize(HdRenderParam* /* renderParam */) {}