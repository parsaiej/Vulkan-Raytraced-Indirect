#include <Common.h>
#include <Mesh.h>
#include <RenderContext.h>
#include <RenderDelegate.h>
#include <ResourceRegistry.h>
#include <Scene.h>

HdDirtyBits Mesh::GetInitialDirtyBitsMask() const { return HdChangeTracker::AllSceneDirtyBits; }

void Mesh::Sync(HdSceneDelegate* pSceneDelegate, HdRenderParam* pRenderParams, HdDirtyBits* pDirtyBits, const TfToken& reprToken)
{
    if ((*pDirtyBits & HdChangeTracker::AllSceneDirtyBits) == 0U)
        return;

    PROFILE_START("Process Mesh");

    std::lock_guard<std::mutex> renderContextLock(m_Owner->GetRenderContextMutex());

    SdfPath id = GetId();

    // Extract pointers to data lists for the mesh.
    auto pPointList    = pSceneDelegate->Get(id, HdTokens->points).Get<VtVec3fArray>();
    auto pNormalList   = pSceneDelegate->Get(id, HdTokens->normals).Get<VtVec3fArray>();
    auto pTexcoordList = pSceneDelegate->Get(id, TfToken("primvars:st")).Get<VtVec2fArray>(); // Will be generated
    auto pIndexList    = VtVec3iArray();                                                      // Will be generated

    // Compute the triangulated indices from the mesh topology.
    HdMeshTopology topology = pSceneDelegate->GetMeshTopology(id);

    // Initialize the mesh util.
    HdMeshUtil meshUtil(&topology, id);

    // Reconstruct the indices / mesh topology.
    VtIntArray trianglePrimitiveParams;
    meshUtil.ComputeTriangleIndices(&pIndexList, &trianglePrimitiveParams);

    // Triangule the texture coordinate prim vars.
    // TODO(parsa): Support "Face Varying" primvars. Currently we promote every to "Vertex" in Houdini to fix problems.

    // {
    //     HdVtBufferSource pTexcoordSource(TfToken("TextureCoordinateSource"),
    //                                      VtValue(pSceneDelegate->Get(id, TfToken("primvars:st")).Get<VtVec2fArray>()));

    //     Check(meshUtil.ComputeTriangulatedFaceVaryingPrimvar(pTexcoordSource.GetData(),
    //                                                          static_cast<int>(pTexcoordSource.GetNumElements()),
    //                                                          pTexcoordSource.GetTupleType().type,
    //                                                          &pTexcoordList),
    //           "Failed to triangulate texture coordinate list.");
    // }

    // Obtain the resource registry.
    auto pResourceRegistry = std::static_pointer_cast<ResourceRegistry>(pSceneDelegate->GetRenderIndex().GetResourceRegistry());
    {
        m_ResourceHandle = pResourceRegistry->PushMeshRequest({ id, pPointList, pNormalList, pIndexList, pTexcoordList });
    }

    // Get the world matrix.
    m_LocalToWorld = GfMatrix4f(pSceneDelegate->GetTransform(id));

    // Store material binding (if any)
    m_MaterialHash = pSceneDelegate->GetMaterialId(id).GetHash();

    m_Owner->GetRenderContext()->GetScene()->AddMesh(this);

    // Clear the dirty bits.
    *pDirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;

    PROFILE_END;
}

HdDirtyBits Mesh::_PropagateDirtyBits(HdDirtyBits bits) const { return bits; }

void Mesh::_InitRepr(const TfToken& reprToken, HdDirtyBits* pDirtyBits)
{
    auto it = std::find_if(_reprs.begin(), _reprs.end(), _ReprComparator(reprToken));

    if (it == _reprs.end())
        _reprs.emplace_back(reprToken, HdReprSharedPtr());
}

void Mesh::Finalize(HdRenderParam* /* renderParam */) {}
