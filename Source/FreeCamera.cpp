#include <FreeCamera.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

// Original window procedure
WNDPROC g_WndProc;

// Custom window procedure
LRESULT CALLBACK HandleWin32Events(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    // Retrieve the class instance from the window's user data
    auto* pCamera = reinterpret_cast<FreeCamera*>(GetWindowLongPtr(hwnd, GWLP_USERDATA)); // NOLINT

    pCamera->GetKeyboard()->ProcessMessage(uMsg, wParam, lParam); // NOLINT
    pCamera->GetMouse()->ProcessMessage(uMsg, wParam, lParam);    // NOLINT

    // Call the original window procedure for default handling
    return CallWindowProc(g_WndProc, hwnd, uMsg, wParam, lParam);
}

FreeCamera::FreeCamera(HdRenderIndex* renderIndex, const SdfPath& delegateId, GLFWwindow* pWindow) : // NOLINT
    HdxFreeCameraSceneDelegate(renderIndex, delegateId)                                              // NOLINT
{
    m_Keyboard = std::make_unique<Keyboard>();
    m_Mouse    = std::make_unique<Mouse>();

    auto* pHwnd = glfwGetWin32Window(pWindow);

    // Need to configure callback to get native window events.
    g_WndProc = (WNDPROC)SetWindowLongPtr(pHwnd, GWLP_WNDPROC, (LONG_PTR)HandleWin32Events); // NOLINT

    // Store the pointer to this instance in the window's user data
    SetWindowLongPtr(pHwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    m_Mouse->SetWindow(pHwnd);

    State initialState {};
    {
        initialState.position = { 0, 0, 0 };
        initialState.target   = { 1, 0, 0 };
        initialState.up       = { 0, 1, 0 };

        initialState.speed       = 2.0F;
        initialState.sensitivity = 0.5F;

        initialState.fov    = XMConvertToRadians(60.0F);
        initialState.aspect = 16.0F / 9.0F;
        initialState.planeN = 0.01F;
        initialState.planeF = 1000.0F;

        initialState.phi   = XM_PIDIV2;
        initialState.theta = 0.0F;
    }
    m_State = initialState;
}

void FreeCamera::Update(float deltaTime)
{
    auto kb = m_Keyboard->GetState();
    auto m  = m_Mouse->GetState();

    auto move = glm::vec3(0, 0, 0);
    {
        if (kb.W)
            move += m_State.target;
        if (kb.S)
            move -= m_State.target;
        if (kb.A)
            move -= glm::cross(m_State.target, m_State.up);
        if (kb.D)
            move += glm::cross(m_State.target, m_State.up);
        if (kb.E)
            move += m_State.up;
        if (kb.Q)
            move -= m_State.up;
    }

    m_State.position += move * m_State.speed * deltaTime;

    if (m.rightButton || kb.LeftControl)
        m_Mouse->SetMode(Mouse::MODE_RELATIVE);
    else
        m_Mouse->SetMode(Mouse::MODE_ABSOLUTE);

    if (m.positionMode != Mouse::MODE_RELATIVE)
    {
        SyncMatricesToState();
        return;
    }

    float dx = static_cast<float>(m.x) * m_State.sensitivity * deltaTime;
    float dy = static_cast<float>(m.y) * m_State.sensitivity * deltaTime;

    m_State.phi += dy;
    m_State.theta += dx;

    float gimbalLockThreshold = 0.01F;

    m_State.phi = std::clamp(m_State.phi, gimbalLockThreshold, DirectX::XM_PI - gimbalLockThreshold);

    m_State.target = SphericalToCartesian(m_State.phi, m_State.theta);

    SyncMatricesToState();
}

void FreeCamera::SyncMatricesToState()
{
    // Create View Matrix.
    auto matrixV = glm::lookAt(m_State.position, m_State.position + m_State.target, m_State.up);

    // Create Projection Matrix.
    auto matrixP = glm::perspective(m_State.fov, m_State.aspect, m_State.planeN, m_State.planeF);

    auto WrapMatrix = [](glm::mat4 m)
    {
        return GfMatrix4f(m[0][0],
                          m[0][1],
                          m[0][2],
                          m[0][3],
                          m[1][0],
                          m[1][1],
                          m[1][2],
                          m[1][3],
                          m[2][0],
                          m[2][1],
                          m[2][2],
                          m[2][3],
                          m[3][0],
                          m[3][1],
                          m[3][2],
                          m[3][3]);
    };

    SetMatrices(GfMatrix4d(WrapMatrix(matrixV)), GfMatrix4d(WrapMatrix(matrixP)));
}
