#ifndef FREE_CAMERA_H
#define FREE_CAMERA_H

class FreeCamera : public HdxFreeCameraSceneDelegate
{
public:

    struct State
    {
        glm::vec3 position;
        glm::vec3 target;
        glm::vec3 up;

        float speed;
        float sensitivity;

        float phi;
        float theta;

        float fov;
        float aspect;
        float planeN;
        float planeF;
    };

    explicit FreeCamera(HdRenderIndex* renderIndex, const SdfPath& delegateId, GLFWwindow* pWindow);

    static glm::vec3 SphericalToCartesian(float phi, float theta) { return { sinf(phi) * cosf(theta), cosf(phi), sinf(phi) * sinf(theta) }; }

    void Update(float deltaTime);

    inline const Keyboard* GetKeyboard() { return m_Keyboard.get(); }
    inline const Mouse*    GetMouse() { return m_Mouse.get(); }

private:

    State m_State;

    void SyncMatricesToState();

    std::unique_ptr<Keyboard> m_Keyboard;
    std::unique_ptr<Mouse>    m_Mouse;
};

#endif
