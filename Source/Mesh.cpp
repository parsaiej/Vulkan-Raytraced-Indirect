#include <Common.h>
#include <Mesh.h>
#include <RenderContext.h>
#include <RenderDelegate.h>
#include <ResourceRegistry.h>

#include <cstddef>

HdDirtyBits Mesh::GetInitialDirtyBitsMask() const { return HdChangeTracker::AllSceneDirtyBits; }

void Mesh::Sync(HdSceneDelegate* pSceneDelegate, HdRenderParam* pRenderParams, HdDirtyBits* pDirtyBits, const TfToken& reprToken)
{
    if ((*pDirtyBits & HdChangeTracker::AllSceneDirtyBits) == 0U)
        return;

    std::lock_guard<std::mutex> renderContextLock(m_Owner->GetRenderContextMutex());

    PROFILE_START("Sync Mesh");

    auto SafeGet = [&]<typename T>(const TfToken& token, T& data)
    {
        VtValue pValue = pSceneDelegate->Get(GetId(), token);

        if (!pValue.IsHolding<T>())
        {
            data = T();
            return;
        }

        data = pValue.Get<T>();
    };

    // Extract pointers to data lists for the mesh. Some might not exist so we check.
    VtVec3fArray pPoints;
    SafeGet(HdTokens->points, pPoints);

    if (pPoints.empty())
    {
        // Early exit on mesh prims with invalid topology.
        *pDirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;

        return;
    }

    // Extract topology information (mainly to get face count).
    HdMeshTopology topology = pSceneDelegate->GetMeshTopology(GetId());

    // Initialize the mesh util.
    HdMeshUtil meshUtil(&topology, GetId());

    // Reconstruct the indices / mesh topology.
    VtIntArray   trianglePrimitiveParams;
    VtVec3iArray triangles;
    meshUtil.ComputeTriangleIndices(&triangles, &trianglePrimitiveParams);

    auto* pResourceRegistry = std::static_pointer_cast<ResourceRegistry>(m_Owner->GetResourceRegistry()).get();

    uint64_t sizeBytesI = sizeof(GfVec3i) * triangles.size();
    uint64_t sizeBytesV = sizeof(GfVec3f) * pPoints.size();

    // Fetch the allocation needed.
    DrawItemRequest request { this };
    {
        request.indexBufferSize  = sizeBytesI;
        request.vertexBufferSize = sizeBytesV;
    }
    pResourceRegistry->PushDrawItemRequest(request);

    // Copy into the pool.
    memcpy(request.pVertexBufferHost, pPoints.data(), sizeBytesV);
    memcpy(request.pIndexBufferHost, triangles.data(), sizeBytesI);

    spdlog::info("Pre-processed Mesh: {}", GetId().GetText());

    // TODO(parsa): Can serialize the post-processed mesh to disk to speed up future executions of the application.

    // Get the world matrix.
    m_LocalToWorld = GfMatrix4f(pSceneDelegate->GetTransform(GetId()));

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
