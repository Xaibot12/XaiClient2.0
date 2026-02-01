#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <tchar.h>
#include <dwmapi.h>
#include <cmath>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "network.h"

// Link DirectX
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dwmapi.lib")

// Global Variables
ID3D11Device* g_pd3dDevice = NULL;
ID3D11DeviceContext* g_pd3dDeviceContext = NULL;
IDXGISwapChain* g_pSwapChain = NULL;
ID3D11RenderTargetView* g_mainRenderTargetView = NULL;
NetworkClient net;

// Forward declarations
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Math Helpers
struct Vec2 { float x, y; };

Vec2 WorldToScreen(float x, float y, float z, float yaw, float pitch, float fov, float screenW, float screenH) {
    // Convert degrees to radians
    // Negate yaw/pitch to correct Minecraft vs Math coordinate differences
    float yawRad = -yaw * (3.14159f / 180.0f);
    float pitchRad = -pitch * (3.14159f / 180.0f);
    float fovRad = fov * (3.14159f / 180.0f);

    // Minecraft Coordinate System to Camera Space
    // X: Right, Y: Up, Z: Back (OpenGL standard view space)
    
    // Rotate around Y (Yaw)
    // We need to transform World(Relative) -> View
    
    // Standard rotation formulas
    // Removed + PI offset to fix 180 degree inversion
    float cosYaw = cos(yawRad); 
    float sinYaw = sin(yawRad);
    float cosPitch = cos(pitchRad);
    float sinPitch = sin(pitchRad);

    // Apply Yaw Rotation (around Y axis)
    // x' = x*cos - z*sin
    // z' = x*sin + z*cos
    // We negate x1 to fix Left-Right inversion
    float x1 = -(x * cosYaw - z * sinYaw);
    float z1 = x * sinYaw + z * cosYaw;
    float y1 = y;

    // Apply Pitch Rotation (around X axis)
    // y' = y*cos - z*sin
    // z' = y*sin + z*cos
    float y2 = y1 * cosPitch - z1 * sinPitch;
    float z2 = y1 * sinPitch + z1 * cosPitch;
    float x2 = x1;

    // Now (x2, y2, z2) is in View Space where -Z is forward? 
    // Wait, let's stick to a simpler projection.
    // If z2 > 0, point is behind camera (OpenGL convention: -Z is forward)
    // Let's assume standard perspective projection:
    
    // In our relative system, if z2 is POSITIVE, it's BEHIND us (since we rotated world to align with camera).
    // Actually, let's just use the direct projection logic that works for Minecraft.
    
    // Project to Screen
    // ScreenX = (x / z) * scale + center
    // ScreenY = (y / z) * scale + center
    
    // Z-Check (Clipping)
    if (z2 <= 0.1f) return { -10000, -10000 }; // Behind camera

    float aspectRatio = screenW / screenH;
    float tanHalfFov = tan(fovRad / 2.0f);
    
    float x_screen = (x2 / (z2 * tanHalfFov * aspectRatio)) * 0.5f + 0.5f;
    float y_screen = 0.5f - (y2 / (z2 * tanHalfFov)) * 0.5f;

    return { x_screen * screenW, y_screen * screenH };
}

// Config
bool showMenu = true;
bool enableEsp = true;
float espColor[3] = { 1.0f, 0.0f, 0.0f };

// Main Entry Point
int main(int, char**)
{
    // Create Application Window
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, _T("XaiOverlay"), NULL };
    RegisterClassEx(&wc);
    
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    HWND hWnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW, 
        _T("XaiOverlay"), _T("XaiOverlay"), 
        WS_POPUP, 0, 0, screenW, screenH, 
        NULL, NULL, wc.hInstance, NULL
    );

    // Initialize Transparency
    SetLayeredWindowAttributes(hWnd, RGB(0,0,0), 0, LWA_COLORKEY);
    MARGINS margins = { -1 };
    DwmExtendFrameIntoClientArea(hWnd, &margins);

    // Initialize Direct3D
    if (!CreateDeviceD3D(hWnd))
    {
        CleanupDeviceD3D();
        UnregisterClass(_T("XaiOverlay"), wc.hInstance);
        return 1;
    }

    ShowWindow(hWnd, SW_SHOWDEFAULT);
    UpdateWindow(hWnd);

    // Setup ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hWnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // Connect to Mod
    net.Connect();

    // Main Loop
    bool done = false;
    while (!done)
    {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        // Toggle Menu Key (Right Shift)
        if (GetAsyncKeyState(VK_RSHIFT) & 1) {
            showMenu = !showMenu;
            if (showMenu) {
                // Make Clickable
                SetWindowLong(hWnd, GWL_EXSTYLE, WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW);
            } else {
                // Click Through
                SetWindowLong(hWnd, GWL_EXSTYLE, WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW);
            }
        }

        // Start Frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Read Data
        GameData data;
        if (net.ReadPacket(data)) {
            // Draw ESP
            if (enableEsp) {
                ImDrawList* draw = ImGui::GetBackgroundDrawList();
                for (const auto& e : data.entities) {
                    Vec2 feet = WorldToScreen(e.x, e.y, e.z, data.camYaw, data.camPitch, 70.0f, (float)screenW, (float)screenH);
                    Vec2 head = WorldToScreen(e.x, e.y + e.h, e.z, data.camYaw, data.camPitch, 70.0f, (float)screenW, (float)screenH);

                    if (feet.x > 0 && feet.x < screenW) {
                        float h = feet.y - head.y;
                        float w = h * 0.5f;
                        draw->AddRect(
                            ImVec2(head.x - w/2, head.y), 
                            ImVec2(head.x + w/2, feet.y), 
                            IM_COL32(espColor[0]*255, espColor[1]*255, espColor[2]*255, 255)
                        );
                    }
                }
            }
        }

        // Draw Menu
        if (showMenu) {
            ImGui::Begin("XaiClient");
            ImGui::Text("Connected: %s", net.Connect() ? "Yes" : "No");
            ImGui::Checkbox("Enable ESP", &enableEsp);
            ImGui::ColorEdit3("ESP Color", espColor);
            ImGui::End();
        }

        // Rendering
        ImGui::Render();
        const float clear_color_with_alpha[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; // Transparent Background
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0); // VSync On
    }

    // Cleanup
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hWnd);
    UnregisterClass(_T("XaiOverlay"), wc.hInstance);

    return 0;
}

// Helpers
bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    if (D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext) != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = NULL; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = NULL; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = NULL; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = NULL; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (g_pd3dDevice != NULL && wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}
