#ifndef SCENE_H
#define SCENE_H

class Mesh;
class Camera;
class Material;

class Scene
{
public:

    inline void AddMesh(Mesh* pMesh) { m_MeshList.push_back(pMesh); };
    inline void AddCamera(Camera* pCamera) { m_CameraList.push_back(pCamera); }

    void AddMaterial(Material* pMaterial);

    inline const std::vector<Mesh*>&            GetMeshList() { return m_MeshList; }
    inline const std::vector<Camera*>&          GetCameraList() { return m_CameraList; }
    inline const std::map<uint32_t, Material*>& GetMaterialMap() { return m_MaterialMap; }

private:

    std::vector<Mesh*>   m_MeshList;
    std::vector<Camera*> m_CameraList;

    std::map<uint32_t, Material*> m_MaterialMap;
};

#endif
