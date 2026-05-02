# ImGui Setup Guide for CQL Database Engine

## Quick Start

### 1. Download ImGui
```powershell
cd D:\quantum\CQL_Database__Engine\
git clone https://github.com/ocornut/imgui.git external/imgui
```

### 2. Add Files to Visual Studio Project

**Source Files to Add:**
- `external/imgui/imgui.cpp`
- `external/imgui/imgui_demo.cpp`
- `external/imgui/imgui_draw.cpp`
- `external/imgui/imgui_tables.cpp`
- `external/imgui/imgui_widgets.cpp`
- `external/imgui/backends/imgui_impl_win32.cpp`
- `external/imgui/backends/imgui_impl_dx11.cpp`

**Header Files to Add:**
- `external/imgui/imgui.h`
- `external/imgui/imgui_internal.h`
- `external/imgui/backends/imgui_impl_win32.h`
- `external/imgui/backends/imgui_impl_dx11.h`

### 3. Project Configuration

**Include Directories:**
```
D:\quantum\CQL_Database__Engine\external\imgui
D:\quantum\CQL_Database__Engine\external\imgui\backends
```

**Linker Input (Additional Dependencies):**
```
d3d11.lib
d3dcompiler.lib
dxgi.lib
```

### 4. Basic Application Structure

```cpp
// main.cpp
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <tchar.h>

// DirectX 11 data
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

// Forward declarations
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Main code
int main(int, char**)
{
    // Create application window
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, 
                       GetModuleHandle(nullptr), nullptr, nullptr, 
                       nullptr, nullptr, L"CQL Database Engine", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"CQL Database Engine", 
                                 WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, 
                                 nullptr, nullptr, wc.hInstance, nullptr);

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Show the window
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // Main loop
    bool done = false;
    while (!done)
    {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        // Start the Dear ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Enable docking
        ImGui::DockSpaceOverViewport(ImGui::GetMainViewport());

        // Your CQL Database UI goes here
        {
            ImGui::Begin("CQL Database Engine");
            ImGui::Text("Database: Not Connected");
            if (ImGui::Button("Open Database"))
            {
                // TODO: Open file dialog and load database
            }
            ImGui::End();
        }

        // Rendering
        ImGui::Render();
        const float clear_color_with_alpha[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        // Update and Render additional Platform Windows
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        g_pSwapChain->Present(1, 0);
    }

    // Cleanup
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

// Helper functions for DirectX setup
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
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 
                                                 createDeviceFlags, featureLevelArray, 2, 
                                                 D3D11_SDK_VERSION, &sd, &g_pSwapChain, 
                                                 &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        if (g_pd3dDevice != nullptr)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
```

## Recommended UI Layout

```
┌─────────────────────────────────────────────────────────────┐
│ File  Database  Query  View  Tools  Help                    │
├─────────────┬───────────────────────────────────────────────┤
│             │                                               │
│  Schema     │         Table Data Viewer                     │
│  Browser    │  ┌──────────┬────────┬─────────┬──────────┐  │
│             │  │ ID (PK)  │ Name   │ Value   │ Created  │  │
│  ├─ Themes  │  ├──────────┼────────┼─────────┼──────────┤  │
│  ├─ Tokens  │  │ 1        │ Dark   │ #000    │ 2024...  │  │
│  └─ Caps    │  │ 2        │ Light  │ #FFF    │ 2024...  │  │
│             │  └──────────┴────────┴─────────┴──────────┘  │
│             │                                               │
├─────────────┼───────────────────────────────────────────────┤
│             │                                               │
│  Query      │         Log / Output                          │
│  Editor     │  [INFO] Database opened: citadel.cql          │
│             │  [INFO] Loaded 3 tables                       │
│  SELECT *   │  [INFO] Query executed in 0.5ms               │
│  FROM...    │                                               │
│             │                                               │
└─────────────┴───────────────────────────────────────────────┘
│ Connected: citadel.cql | Tables: 3 | Pages: 42              │
└─────────────────────────────────────────────────────────────┘
```

## Useful ImGui Patterns for Database UI

### Table Grid with Column Sorting
```cpp
if (ImGui::BeginTable("table_data", columnCount, 
    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | 
    ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable))
{
    // Setup columns
    for (const auto& col : schema.columns)
    {
        ImGui::TableSetupColumn(col.name.c_str());
    }
    ImGui::TableHeadersRow();

    // Display rows
    for (const auto& row : queryResult.rows)
    {
        ImGui::TableNextRow();
        for (int i = 0; i < row.cells.size(); i++)
        {
            ImGui::TableSetColumnIndex(i);
            ImGui::Text("%s", CellToString(row.cells[i]).c_str());
        }
    }
    ImGui::EndTable();
}
```

### Hex Viewer for Pages
```cpp
void DrawHexViewer(const uint8_t* data, size_t size)
{
    ImGuiListClipper clipper;
    clipper.Begin(size / 16);
    while (clipper.Step())
    {
        for (int line = clipper.DisplayStart; line < clipper.DisplayEnd; line++)
        {
            ImGui::Text("%08X: ", line * 16);
            ImGui::SameLine();
            
            // Hex values
            for (int i = 0; i < 16 && (line * 16 + i) < size; i++)
            {
                ImGui::Text("%02X ", data[line * 16 + i]);
                ImGui::SameLine();
            }
            
            // ASCII representation
            ImGui::Text("  ");
            ImGui::SameLine();
            for (int i = 0; i < 16 && (line * 16 + i) < size; i++)
            {
                uint8_t c = data[line * 16 + i];
                ImGui::Text("%c", (c >= 32 && c < 127) ? c : '.');
                ImGui::SameLine();
            }
            ImGui::NewLine();
        }
    }
}
```

### Performance Monitor
```cpp
void DrawPerformanceMonitor()
{
    ImGui::Begin("Performance");
    
    ImGui::Text("Query Execution Time: %.2f ms", lastQueryTime);
    ImGui::Text("Page Cache Hit Rate: %.1f%%", cacheHitRate * 100);
    ImGui::Text("Dirty Pages: %d", dirtyPageCount);
    
    ImGui::Separator();
    
    // Plot query times
    ImGui::PlotLines("Query Times", queryTimes, queryTimeCount, 0, 
                     nullptr, 0.0f, 100.0f, ImVec2(0, 80));
    
    ImGui::End();
}
```

## Next Steps

1. Get the basic ImGui window running
2. Implement the menu bar and status bar
3. Add the Schema Browser window
4. Build the Table Data Viewer
5. Create the Query Editor
6. Add specialized windows as you progress through milestones

Good luck with your CQL Database Engine! 🚀
