#include <Common.h>
#include <Mesh.h>
#include <RenderContext.h>
#include <RenderDelegate.h>
#include <ResourceRegistry.h>

HdDirtyBits Mesh::GetInitialDirtyBitsMask() const { return HdChangeTracker::AllSceneDirtyBits; }

void Mesh::Sync(HdSceneDelegate* pSceneDelegate, HdRenderParam* pRenderParams, HdDirtyBits* pDirtyBits, const TfToken& reprToken)
{
    if ((*pDirtyBits & HdChangeTracker::AllSceneDirtyBits) == 0U)
        return;

    PROFILE_START("Sync Mesh");

    std::lock_guard<std::mutex> renderContextLock(m_Owner->GetRenderContextMutex());

    SdfPath id = GetId();

    auto SafeGet = [&]<typename T>(const TfToken& token, T& data)
    {
        VtValue pValue = pSceneDelegate->Get(id, token);

        if (!pValue.IsHolding<T>())
        {
            data = T();
            return;
        }

        data = pValue.Get<T>();
    };

    ResourceRegistry::MeshRequest meshRequest = { id };

    // Extract pointers to data lists for the mesh. Some might not exist so we check.
    SafeGet(HdTokens->points, meshRequest.pPoints);

    if (meshRequest.pPoints.empty())
    {
        // For some reason we can be provided empty meshes that fuck everything up.. skip...
        *pDirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
        return;
    }

    // Compute the triangulated indices from the mesh topology.
    HdMeshTopology topology = pSceneDelegate->GetMeshTopology(id);

    // Initialize the mesh util.
    HdMeshUtil meshUtil(&topology, id);

    // Reconstruct the indices / mesh topology.
    VtIntArray trianglePrimitiveParams;
    meshUtil.ComputeTriangleIndices(&meshRequest.pTriangles, &trianglePrimitiveParams);

    m_IndexCount = static_cast<uint32_t>(meshRequest.pTriangles.size()) * 3U;

    // TODO(parsa): Check the primvar meta data to determine face-varying or vertex interpolation.
    // For vertex interpolation, we can but the buffer directly in the vertex binding and don't need to triangulate
    // or handle per-face values. For face-varying, need to trianglulate and bind the resource to the fragment stage
    // in order to be manually sampled based on SV_PrimitiveID and interpolatated with SV_Barycentric. This requires a more
    // robust system that createst vertex bindings / descriptors accordingly but can be worked around for known asset formatting.

    /*
    if (pSceneDelegate->Get(id, TfToken("primvars:st")).IsHolding<VtVec2fArray>())
    {
        // https://graphics.pixar.com/opensubdiv/docs/subdivision_surfaces.html#face-varying-interpolation-rules
        HdVtBufferSource pTexcoordSource(TfToken("TextureCoordinateSource"),
                                         VtValue(pSceneDelegate->Get(id, TfToken("primvars:st")).Get<VtVec2fArray>()));

        // Triangule the texture coordinate prim vars.
        VtValue pTriangulatedTexcoord;
        Check(meshUtil.ComputeTriangulatedFaceVaryingPrimvar(pTexcoordSource.GetData(),
                                                             static_cast<int>(pTexcoordSource.GetNumElements()),
                                                             pTexcoordSource.GetTupleType().type,
                                                             &pTriangulatedTexcoord),
              "Failed to triangulate texture coordinate list.");

        meshRequest.pTexCoords = pTriangulatedTexcoord.UncheckedGet<VtVec2fArray>();
    }
    */

    // Store material binding (if any)
    meshRequest.materialId = pSceneDelegate->GetMaterialId(id);

    // Push request.
    m_ResourceHandle =
        std::static_pointer_cast<ResourceRegistry>(pSceneDelegate->GetRenderIndex().GetResourceRegistry())->PushMeshRequest(meshRequest);

    // Get the world matrix.
    m_LocalToWorld = GfMatrix4f(pSceneDelegate->GetTransform(id));

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
