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
