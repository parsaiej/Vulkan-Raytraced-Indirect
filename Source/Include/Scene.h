#ifndef SCENE_H
#define SCENE_H

class Mesh;
class Camera;

class Scene
{
public:

    inline void AddMesh(Mesh* pMesh) { m_MeshList.push_back(pMesh); };
    inline void AddCamera(Camera* pCamera) { m_CameraList.push_back(pCamera); }

    inline const std::vector<Mesh*>&   GetMeshList() { return m_MeshList; }
    inline const std::vector<Camera*>& GetCameraList() { return m_CameraList; }

private:

    std::vector<Mesh*>   m_MeshList;
    std::vector<Camera*> m_CameraList;
};

#endif
