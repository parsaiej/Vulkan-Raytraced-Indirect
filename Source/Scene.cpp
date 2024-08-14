#include <Scene.h>
#include <Material.h>

void Scene::AddMaterial(Material* pMaterial)
{
    auto materialHash = static_cast<uint32_t>(pMaterial->GetId().GetHash());

    if (m_MaterialMap.contains(materialHash))
        return;

    m_MaterialMap[materialHash] = pMaterial;
}
