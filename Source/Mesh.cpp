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
    SafeGet(HdTokens->normals, meshRequest.pNormals);
    SafeGet(TfToken("primvars:st"), meshRequest.pTexCoords);

    auto pIndexList = VtVec3iArray();

    // Compute the triangulated indices from the mesh topology.
    HdMeshTopology topology = pSceneDelegate->GetMeshTopology(id);

    // Initialize the mesh util.
    HdMeshUtil meshUtil(&topology, id);

    // Reconstruct the indices / mesh topology.
    VtIntArray trianglePrimitiveParams;
    meshUtil.ComputeTriangleIndices(&pIndexList, &trianglePrimitiveParams);

    meshRequest.pIndices = pIndexList;

    // VtVec2fArray resampledST(meshRequest.pPoints.size());
    {
        // https://graphics.pixar.com/opensubdiv/docs/subdivision_surfaces.html#face-varying-interpolation-rules
        //  HdVtBufferSource pTexcoordSource(TfToken("TextureCoordinateSource"),
        //                                   VtValue(pSceneDelegate->Get(id, TfToken("primvars:st")).Get<VtVec2fArray>()));
        //
        //  // Triangule the texture coordinate prim vars.
        //  VtValue pTriangulatedTexcoord;
        //  Check(meshUtil.ComputeTriangulatedFaceVaryingPrimvar(pTexcoordSource.GetData(),
        //                                                       static_cast<int>(pTexcoordSource.GetNumElements()),
        //                                                       pTexcoordSource.GetTupleType().type,
        //                                                       &pTriangulatedTexcoord),
        //        "Failed to triangulate texture coordinate list.");

        // for (uint32_t triangleIndex = 0U; triangleIndex < pIndexList.size(); triangleIndex++)
        // {
        //     auto& triangle = pIndexList[triangleIndex];
        //
        //     for (uint32_t triangleVertexIndex = 0U; triangleVertexIndex < 1U; triangleVertexIndex++)
        //     {
        //         resampledST[triangle[triangleVertexIndex]] = meshRequest.pTexCoords[triangleIndex * 3U + triangleVertexIndex];
        //     }
        // }
    }

    // Push request.
    m_ResourceHandle =
        std::static_pointer_cast<ResourceRegistry>(pSceneDelegate->GetRenderIndex().GetResourceRegistry())->PushMeshRequest(meshRequest);

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
