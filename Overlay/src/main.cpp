#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <tchar.h>
#include <dwmapi.h>
#include <cmath>
#include <string.h>
#include <chrono>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "network.h"
#include "ClickGUI.h"
#include "TextureManager.h"
#include "modules/ESP.h"
#include "modules/Nametags.h"
#include "modules/Disable.h"
#include "modules/BlockESP.h"
#include "modules/Friends.h"
#include "modules/PlayerESP.h"
#include "MathUtils.h"

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
ClickGUI clickGui;
ESP* espModule = nullptr;
Nametags* nametagsModule = nullptr;
Disable* disableModule = nullptr;
BlockESP* blockEspModule = nullptr;
Friends* friendsModule = nullptr;
PlayerESP* playerEspModule = nullptr;

// Forward declarations
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();

// Performance Debugging
long long totalRenderTime = 0;
long long totalNametagTime = 0;
int renderFrames = 0;
std::chrono::steady_clock::time_point lastRenderDebugTime = std::chrono::steady_clock::now();

#include "utils/IconLoader.h"

// Forward declarations of helper functions
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Math Helpers are now in MathUtils.h

// Config
bool showMenu = false;

// Window Enumeration Callback
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    char title[256];
    if (GetWindowTextA(hwnd, title, sizeof(title))) {
        if (strstr(title, "Minecraft") != NULL && strstr(title, "1.21.4") != NULL) {
            *(HWND*)lParam = hwnd;
            return FALSE; // Stop enumeration
        }
    }
    return TRUE; // Continue
}

// Main Entry Point
int main(int, char**)
{
    // Create Application Window
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, _T("XaiOverlay"), NULL };
    RegisterClassEx(&wc);
    
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    // Initial Style: Click-Through (Transparent) because showMenu is false
    HWND hWnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW, 
        _T("XaiOverlay"), _T("XaiOverlay"), 
        WS_POPUP, 0, 0, screenW, screenH, 
        NULL, NULL, wc.hInstance, NULL
    );

    // Initialize Transparency
    SetLayeredWindowAttributes(hWnd, 0, 255, LWA_ALPHA);
    MARGINS margins = { -1 };
    DwmExtendFrameIntoClientArea(hWnd, &margins);

    // Initialize Direct3D
    if (!CreateDeviceD3D(hWnd))
    {
        CleanupDeviceD3D();
        UnregisterClass(_T("XaiOverlay"), wc.hInstance);
        return 1;
    }

    // Initialize TextureManager
    TextureManager::Instance().Initialize(g_pd3dDevice);

    // Show the window
    ShowWindow(hWnd, SW_SHOWDEFAULT);
    UpdateWindow(hWnd);
    
    // Find Minecraft Window
    HWND mcHwnd = NULL;
    // We'll search for it in the loop or find it once here.
    // Finding it once is risky if MC restarts. Finding it every frame is expensive.
    // Let's find it periodically.

    // Setup ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hWnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // Initialize IconLoader
    IconLoader::Get().Initialize(g_pd3dDevice);

    // Initialize Modules
    friendsModule = new Friends();
    espModule = new ESP(&net);
    playerEspModule = new PlayerESP(friendsModule);
    nametagsModule = new Nametags(friendsModule);
    disableModule = new Disable();
    blockEspModule = new BlockESP(&net);

    // Pass Modules to Network for Pre-Calculation (Phase 1)
    net.SetModules(espModule, playerEspModule, nametagsModule);

    clickGui.RegisterModule(friendsModule);
    clickGui.RegisterModule(espModule);
    clickGui.RegisterModule(playerEspModule);
    clickGui.RegisterModule(nametagsModule);
    clickGui.RegisterModule(disableModule);
    clickGui.RegisterModule(blockEspModule);

    // Connect to Mod
    if (net.Connect()) {
        // Initial state sync
        // Load config first, but we need connection to send update
        // So we load config, then send update
    }

    // Load Config
    clickGui.LoadConfig("config.ini");
    
    // Helper to send hotkeys
    auto BroadcastHotkeys = [&]() {
        std::vector<int> keys;
        for (auto mod : clickGui.modules) {
            if (mod->keybind > 0) keys.push_back(mod->keybind);
        }
        net.SendHotkeys(keys);
    };

    // Now that config is loaded (and BlockESP has its list), send the update if connected
    if (net.IsConnected()) {
        net.SendState(clickGui.modules);
        blockEspModule->SendUpdate(); // This triggers the block list packet
        espModule->SendUpdate(); // Send Specific Mobs
        BroadcastHotkeys();
    }

    // Persistent Data
    GameData data;

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
                // Save Config when menu is closed
                clickGui.SaveConfig("config.ini");
            }
            SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }

        // Module Keybinds (Network Driven)
        for (int pressedKey : data.hotkeysPressed) {
            for (auto mod : clickGui.modules) {
                if (mod->keybind > 0 && !mod->isBinding) {
                    // Convert Module VK to GLFW to match Packet
                    int glfwKey = net.VKToGLFW(mod->keybind);
                    if (glfwKey == pressedKey) {
                        mod->Toggle();
                    }
                }
            }
        }
        data.hotkeysPressed.clear(); // Consume keys

        // Find Minecraft Window
        static HWND mcHwnd = NULL;
        static int frameCount = 0;
        if (frameCount++ % 60 == 0 || mcHwnd == NULL) { // Check every ~1 second
            mcHwnd = NULL;
            EnumWindows(EnumWindowsProc, (LPARAM)&mcHwnd);
            
            // Try to reconnect if disconnected
            if (!net.IsConnected()) {
                if (net.Connect()) {
                    // Re-sync state on reconnect
                    net.SendState(clickGui.modules);
                    blockEspModule->SendUpdate();
                    espModule->SendUpdate();
                }
            }
        }

        // Determine Focus State
        bool isFocused = true;
        if (mcHwnd) {
            HWND foreground = GetForegroundWindow();
            if (foreground != mcHwnd && foreground != hWnd) { // hWnd is our overlay
                isFocused = false;
            }
        }
        
        // Start Frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Read Data (Update if available)
        net.ReadPacket(data);
        friendsModule->Update(&data);

        ImDrawList* bgDrawList = ImGui::GetBackgroundDrawList();

        // Render Blocks
        if (isFocused && !data.isScreenOpen && blockEspModule->enabled) {
            blockEspModule->Render(data, (float)screenW, (float)screenH, bgDrawList);
        }

        // Clear processed block updates to prevent accumulation
        data.blockUpdates.clear();
        data.blocksToDelete.clear();
        data.chunksToUnload.clear();
        data.shouldClearBlocks = false;

        // Render Entities (Hide if not focused OR screen is open)
        if (isFocused && !data.isScreenOpen && (espModule->enabled || nametagsModule->enabled || playerEspModule->enabled)) {
            auto tRenderStart = std::chrono::high_resolution_clock::now();
            long long frameNametagTime = 0;

            ImDrawList* draw = bgDrawList;
            for (const auto& e : data.entities) {
                // Optimization Check
                if (!e.shouldRender && !e.shouldRenderNametag) continue;

                // Frustum Culling Check (Phase 3) - DISABLED for stability
                // The simple center-point check causes entities to disappear when close or large.
                // We will rely on WorldToScreen's clipping for now.
                /*
                float x = e.x; float y = e.y; float z = e.z;
                float yawRad = data.camYaw * (3.14159f / 180.0f);
                float cY = cos(yawRad);
                float sY = sin(yawRad);
                float x1 = x * cY - z * sY;
                float z1 = x * sY + z * cY;
                float pitchRad = data.camPitch * (3.14159f / 180.0f);
                float cP = cos(pitchRad);
                float sP = sin(pitchRad);
                float z2 = y * sP + z1 * cP;

                // If z2 <= 0, it's behind the camera. CULL IT!
                if (z2 <= 0.5f) continue; // 0.5f buffer
                */

                // Calculate 3D Bounding Box Corners
                float w2 = e.w / 2.0f;
                float points[8][3] = {
                    { e.x - w2, e.y,       e.z - w2 }, // 0: Bottom-Left-North
                    { e.x + w2, e.y,       e.z - w2 }, // 1: Bottom-Right-North
                    { e.x - w2, e.y,       e.z + w2 }, // 2: Bottom-Left-South
                    { e.x + w2, e.y,       e.z + w2 }, // 3: Bottom-Right-South
                    { e.x - w2, e.y + e.h, e.z - w2 }, // 4: Top-Left-North
                    { e.x + w2, e.y + e.h, e.z - w2 }, // 5: Top-Right-North
                    { e.x - w2, e.y + e.h, e.z + w2 }, // 6: Top-Left-South
                    { e.x + w2, e.y + e.h, e.z + w2 }  // 7: Top-Right-South
                };

                // Define Faces (Vertex Indices)
                // Order: Bottom, Top, North, South, West, East
                int faces[6][4] = {
                    {0, 1, 3, 2}, // Bottom
                    {4, 5, 7, 6}, // Top
                    {0, 4, 5, 1}, // North (Z-)
                    {2, 6, 7, 3}, // South (Z+)
                    {0, 2, 6, 4}, // West (X-)
                    {1, 5, 7, 3}  // East (X+)
                };

                // Face Normals (X, Y, Z)
                float normals[6][3] = {
                    {0, -1, 0}, // Bottom
                    {0, 1, 0},  // Top
                    {0, 0, -1}, // North
                    {0, 0, 1},  // South
                    {-1, 0, 0}, // West
                    {1, 0, 0}   // East
                };

                // Face Centers relative to camera (e.x/y/z are already relative)
                float centers[6][3] = {
                    {e.x, e.y, e.z},             // Bottom
                    {e.x, e.y + e.h, e.z},       // Top
                    {e.x, e.y + e.h/2.0f, e.z - w2}, // North
                    {e.x, e.y + e.h/2.0f, e.z + w2}, // South
                    {e.x - w2, e.y + e.h/2.0f, e.z}, // West
                    {e.x + w2, e.y + e.h/2.0f, e.z}  // East
                };

                // Draw Visible Faces
                bool anyVisible = false;
                float minX = 100000, maxX = -100000;
                float minY = 100000, maxY = -100000;
                bool hasValidPoints = false; // Track if ANY point is valid for nametag culling check

                for (int i = 0; i < 6; i++) {
                    // Dot Product: ViewVector (Center - Camera) . Normal
                    // Camera is at (0,0,0), so ViewVector is just Center
                    float dot = centers[i][0] * normals[i][0] + 
                                centers[i][1] * normals[i][1] + 
                                centers[i][2] * normals[i][2];

                    if (dot < 0) { // Facing Camera
                        // Project vertices
                        Vec2 screenPoints[4];
                        bool allValid = true;
                        for (int j = 0; j < 4; j++) {
                            int idx = faces[i][j];
                            screenPoints[j] = WorldToScreen(points[idx][0], points[idx][1], points[idx][2], data.camYaw, data.camPitch, data.fov, (float)screenW, (float)screenH);
                            if (screenPoints[j].x <= -10000) allValid = false;
                            
                            // Update BBox for nametags (use any valid point we find)
                            if (screenPoints[j].x > -10000) {
                                hasValidPoints = true;
                                if (screenPoints[j].x < minX) minX = screenPoints[j].x;
                                if (screenPoints[j].x > maxX) maxX = screenPoints[j].x;
                                if (screenPoints[j].y < minY) minY = screenPoints[j].y;
                                if (screenPoints[j].y > maxY) maxY = screenPoints[j].y;
                            }
                        }

                        if (allValid) {
                            if (e.shouldRender) {
                                // Draw Quad Edges
                                draw->AddLine(ImVec2(screenPoints[0].x, screenPoints[0].y), ImVec2(screenPoints[1].x, screenPoints[1].y), e.cachedColor);
                                draw->AddLine(ImVec2(screenPoints[1].x, screenPoints[1].y), ImVec2(screenPoints[2].x, screenPoints[2].y), e.cachedColor);
                                draw->AddLine(ImVec2(screenPoints[2].x, screenPoints[2].y), ImVec2(screenPoints[3].x, screenPoints[3].y), e.cachedColor);
                                draw->AddLine(ImVec2(screenPoints[3].x, screenPoints[3].y), ImVec2(screenPoints[0].x, screenPoints[0].y), e.cachedColor);
                                anyVisible = true;
                            }
                        }
                    }
                }

                // Draw Nametags (Centered above box) - Check hasValidPoints instead of anyVisible to fix close-up culling
                if (hasValidPoints && nametagsModule->enabled && e.shouldRenderNametag) {
                    auto tNametagStart = std::chrono::high_resolution_clock::now();
                    float centerX = (minX + maxX) / 2.0f;
                    nametagsModule->Render(e, centerX, minY, data.fov);
                    auto tNametagEnd = std::chrono::high_resolution_clock::now();
                    frameNametagTime += std::chrono::duration_cast<std::chrono::microseconds>(tNametagEnd - tNametagStart).count();
                }
            }
            
            auto tRenderEnd = std::chrono::high_resolution_clock::now();
            totalRenderTime += std::chrono::duration_cast<std::chrono::microseconds>(tRenderEnd - tRenderStart).count();
            totalNametagTime += frameNametagTime;
            renderFrames++;

            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - lastRenderDebugTime).count() >= 1) {
                double avgRender = (totalRenderTime / (double)renderFrames) / 1000.0;
                double avgNametag = (totalNametagTime / (double)renderFrames) / 1000.0;
                printf("[Perf] C++: Render=%.2fms (Nametags=%.2fms)\n", avgRender, avgNametag);
                lastRenderDebugTime = now;
                totalRenderTime = 0;
                totalNametagTime = 0;
                renderFrames = 0;
            }
        }

        // Draw Menu (Hide if not focused)
        if (showMenu && isFocused) {
            if (clickGui.Render()) {
                if (disableModule->enabled) {
                    // Send Disable Packet
                    net.SendDisable(disableModule->fully);
                    done = true; // Exit loop
                } else {
                    net.SendState(clickGui.modules);
                    BroadcastHotkeys();
                }
            }
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
    clickGui.SaveConfig("config.ini");
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
