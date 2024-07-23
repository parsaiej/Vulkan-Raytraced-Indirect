#ifndef SCENE_H
#define SCENE_H

class Mesh;

class Scene
{
public:
    inline void AddMesh(Mesh* pMesh) { m_MeshList.push_back(pMesh); };

    inline const std::vector<Mesh*>& GetMeshList() { return m_MeshList; }

private:
    std::vector<Mesh*> m_MeshList;
};

#endif