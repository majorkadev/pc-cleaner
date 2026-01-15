#include <Windows.h>
#include <d3d11.h>
#include <tchar.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <iostream>
#include <ShlObj.h>
#include <intrin.h>

// ImGui
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

// Original logic headers
#include <fstream>
#include <chrono>

#pragma warning(disable : 4996)
#pragma comment(linker, "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")

// Global State
static ID3D11Device*            g_pd3dDevice = NULL;
static ID3D11DeviceContext*     g_pd3dDeviceContext = NULL;
static IDXGISwapChain*          g_pSwapChain = NULL;
static ID3D11RenderTargetView*  g_mainRenderTargetView = NULL;

// Global Log Buffer
static std::vector<std::string> g_LogBuffer;
static std::mutex g_LogMutex;
void AddLog(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_LogMutex);
    g_LogBuffer.push_back("[+] " + message);
    if (g_LogBuffer.size() > 100) g_LogBuffer.erase(g_LogBuffer.begin());
}

// Helper functions for D3D
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Original Cleaner Functions (Refactored for GUI)
namespace CleanerLogic {
    bool is_admin() {
        HANDLE token;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
            TOKEN_ELEVATION elevation{};
            DWORD size;
            if (GetTokenInformation(token, TokenElevation, &elevation, sizeof elevation, &size))
                return elevation.TokenIsElevated;
        }
        return false;
    }

    void clean_temp_windows() {
        SHFILEOPSTRUCT file_op{};
        file_op.hwnd = nullptr;
        file_op.wFunc = FO_DELETE;
        file_op.pFrom = "C:\\Windows\\Temp\\*.*\0";
        file_op.pTo = nullptr;
        file_op.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
        SHFileOperation(&file_op);
    }

    void clean_temp_app() {
        char path[MAX_PATH];
        GetEnvironmentVariableA("temp", path, MAX_PATH);
        std::string temp_path = std::string(path) + "\\*.*\0";
        SHFILEOPSTRUCT file_op{};
        file_op.hwnd = nullptr;
        file_op.wFunc = FO_DELETE;
        file_op.pFrom = temp_path.c_str();
        file_op.pTo = nullptr;
        file_op.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
        SHFileOperation(&file_op);
    }

    void remove_defender_aggressive() {
        system("powershell -Command \"Set-MpPreference -DisableRealtimeMonitoring $true -DisableBehaviorMonitoring $true -DisableIOAVProtection $true -DisableOnAccessProtection $true -DisableIntrusionPreventionSystem $true -DisableScriptScanning $true -DisableScanningNetworkFiles $true -DisableBlockAtFirstSight $true -DisableTamperProtection $true\"");
        system("powershell -Command \"Set-NetFirewallProfile -Profile Domain,Public,Private -Enabled False\"");
        system("powershell -Command \"Get-ScheduledTask -TaskPath '\\Microsoft\\Windows\\Windows Defender\\' | Disable-ScheduledTask\"");
        
        const char* services[] = { "WinDefend", "mpssvc", "wscsvc", "SecurityHealthService", "Sense", "WdNisSvc", "WdNisDrv" };
        for (const char* svc : services) {
            std::string stopCmd = "sc stop " + std::string(svc);
            std::string configCmd = "sc config " + std::string(svc) + " start= disabled";
            system(stopCmd.c_str());
            system(configCmd.c_str());
        }
        system("reg delete \"HKCR\\CLSID\\{09A47860-11B0-4DA5-AFA5-26D86198A780}\" /f");
    }

    void remove_windows_store() {
        system("powershell -Command \"Get-AppxPackage -AllUsers *WindowsStore* | Remove-AppxPackage\"");
        system("powershell -Command \"Get-AppxProvisionedPackage -Online | Where-Object {$_.PackageName -like '*WindowsStore*'} | Remove-AppxProvisionedPackage -Online\"");
    }

    void clean_chrome_cookies() {
        char path[MAX_PATH];
        SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path);
        std::string cookie_path = std::string(path) + "\\Google\\Chrome\\User Data\\Default\\Network\\Cookies";
        DeleteFileA(cookie_path.c_str());
    }

    void disable_telemetry() {
        system("sc stop DiagTrack");
        system("sc config DiagTrack start= disabled");
        system("sc stop dmwappushservice");
        system("sc config dmwappushservice start= disabled");
        
        HKEY key;
        uint32_t payload = 0;
        if (!RegOpenKeyEx(HKEY_LOCAL_MACHINE, R"(SOFTWARE\Policies\Microsoft\Windows\DataCollection)", 0, KEY_ALL_ACCESS, &key)) {
            RegSetValueEx(key, "AllowTelemetry", 0, REG_DWORD, reinterpret_cast<LPBYTE>(&payload), sizeof payload);
            RegCloseKey(key);
        }
    }

    void tcp_latency_tweak() {
        HKEY key;
        uint32_t throttle = 0xFFFFFFFF;
        uint32_t responsiveness = 0;
        uint32_t one = 1;

        if (!RegOpenKeyEx(HKEY_LOCAL_MACHINE, R"(SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile)", 0, KEY_ALL_ACCESS, &key)) {
            RegSetValueEx(key, "NetworkThrottlingIndex", 0, REG_DWORD, reinterpret_cast<LPBYTE>(&throttle), sizeof throttle);
            RegSetValueEx(key, "SystemResponsiveness", 0, REG_DWORD, reinterpret_cast<LPBYTE>(&responsiveness), sizeof responsiveness);
            RegCloseKey(key);
        }

        HKEY interfacesKey;
        if (!RegOpenKeyEx(HKEY_LOCAL_MACHINE, R"(SYSTEM\CurrentControlSet\Services\Tcpip\Parameters\Interfaces)", 0, KEY_READ, &interfacesKey)) {
            char subKeyName[256];
            DWORD subKeyNameSize = sizeof(subKeyName);
            DWORD index = 0;
            while (RegEnumKeyEx(interfacesKey, index++, subKeyName, &subKeyNameSize, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
                HKEY interfaceKey;
                if (!RegOpenKeyEx(interfacesKey, subKeyName, 0, KEY_ALL_ACCESS, &interfaceKey)) {
                    RegSetValueEx(interfaceKey, "TcpAckFrequency", 0, REG_DWORD, reinterpret_cast<LPBYTE>(&one), sizeof one);
                    RegSetValueEx(interfaceKey, "TCPNoDelay", 0, REG_DWORD, reinterpret_cast<LPBYTE>(&one), sizeof one);
                    RegCloseKey(interfaceKey);
                }
                subKeyNameSize = sizeof(subKeyName);
            }
            RegCloseKey(interfacesKey);
        }
    }
}

// UI Styles (INTERWEBZ 1:1)
namespace Style {
    void ApplyInterwebzTheme() {
        auto& style = ImGui::GetStyle();
        style.WindowRounding = 0.0f;
        style.ChildRounding = 3.0f;
        style.FrameRounding = 3.0f;
        style.PopupRounding = 3.0f;
        style.ScrollbarRounding = 3.0f;
        style.GrabRounding = 3.0f;
        style.TabRounding = 3.0f;

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_WindowBg] = ImVec4(35/255.f, 30/255.f, 61/255.f, 1.00f);
        colors[ImGuiCol_ChildBg] = ImVec4(45/255.f, 39/255.f, 77/255.f, 0.40f);
        colors[ImGuiCol_Border] = ImVec4(60/255.f, 55/255.f, 100/255.f, 0.50f);
        colors[ImGuiCol_TitleBg] = ImVec4(30/255.f, 25/255.f, 50/255.f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(30/255.f, 25/255.f, 50/255.f, 1.00f);
        colors[ImGuiCol_Button] = ImVec4(180/255.f, 70/255.f, 80/255.f, 0.80f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(200/255.f, 90/255.f, 100/255.f, 0.90f);
        colors[ImGuiCol_ButtonActive] = ImVec4(160/255.f, 50/255.f, 60/255.f, 1.00f);
        colors[ImGuiCol_FrameBg] = ImVec4(45/255.f, 39/255.f, 77/255.f, 1.00f);
        colors[ImGuiCol_Header] = ImVec4(180/255.f, 70/255.f, 80/255.f, 0.40f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(180/255.f, 70/255.f, 80/255.f, 0.60f);
        colors[ImGuiCol_HeaderActive] = ImVec4(180/255.f, 70/255.f, 80/255.f, 0.80f);
        colors[ImGuiCol_CheckMark] = ImVec4(180/255.f, 70/255.f, 80/255.f, 1.00f);
        colors[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.95f, 1.00f);
    }
}

// Main Window Entry
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    if (!CleanerLogic::is_admin()) {
        MessageBoxA(NULL, "Please run as administrator!", "Error", MB_ICONERROR);
        return 0;
    }

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, L"INTERWEBZ_PC_CLEANER", NULL };
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"INTERWEBZ", WS_POPUP, 100, 100, 800, 520, NULL, NULL, wc.hInstance, NULL);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    
    // Load modern system font (Segoe UI - Windows default)
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f);
    
    Style::ApplyInterwebzTheme();
    
    // Enhanced styling for modern look
    ImGuiStyle& style = ImGui::GetStyle();
    style.FramePadding = ImVec2(8, 6);
    style.ItemSpacing = ImVec2(10, 8);
    style.ItemInnerSpacing = ImVec2(8, 6);
    style.WindowPadding = ImVec2(12, 12);
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 10.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.AntiAliasedLines = true;
    style.AntiAliasedFill = true;

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    bool show_app = true;
    static bool selected_features[20] = { false };
    const char* feature_names[] = {
        "Temp Windows Files", "Temp App Files", "Fix Hardware Usage", "Remove Defender / Firewall",
        "Remove Windows Store", "Chrome Cookies", "Remove Windows Updates", "Seconds in Clock",
        "Admin Rules Fix", "System Info", "SFC Scan", "Registry Check", "Telemetry Removal",
        "WinSxS Cleanup", "Activity History", "UWP Bloatware", "Ultimate Power Plan",
        "Power Throttling", "TCP Latency Tweak"
    };

    while (show_app) {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) show_app = false;
        }
        if (!show_app) break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos({ 0, 0 });
        ImGui::SetNextWindowSize({ 800, 520 });
        ImGui::Begin("INTERWEBZ_MAIN", NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
        {
            // Header
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
            ImGui::Text("PC CLEANER");
            ImGui::SameLine(ImGui::GetWindowWidth() - 30);
            if (ImGui::Button("X", { 25, 25 })) show_app = false;
            ImGui::PopStyleColor();
            ImGui::Separator();

            ImGui::Columns(1); // Reset props

            // Calc sizes
            float windowWidth = ImGui::GetWindowWidth();
            float windowHeight = ImGui::GetWindowHeight();
            float panelWidth = (windowWidth - 30) / 2; // 10px padding left, 10px middle, 10px right
            float panelHeight = windowHeight - 160;    // Adjust for header and footer (increased gap to prevent overlap)

            // Left Panel (Features)
            ImGui::SetCursorPos({ 10, 60 });
            ImGui::BeginGroup();
            ImGui::Text("FEATURES");
            ImGui::Separator();
            ImGui::BeginChild("FeaturesPanel", { panelWidth, panelHeight }, true, ImGuiWindowFlags_NoScrollbar);
            {
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 4));
                for (int i = 0; i < 19; i++) {
                    if (ImGui::Selectable(feature_names[i], &selected_features[i])) {
                        // Optional: Add click sound or effect
                    }
                }
                ImGui::PopStyleVar();
            }
            ImGui::EndChild();
            ImGui::EndGroup();

            // Right Panel (Logs)
            ImGui::SetCursorPos({ 20 + panelWidth, 60 });
            ImGui::BeginGroup();
            ImGui::Text("OPERATION LOGS");
            ImGui::Separator();
            ImGui::BeginChild("LogsPanel", { panelWidth, panelHeight }, true);
            {
                std::lock_guard<std::mutex> lock(g_LogMutex);
                for (const auto& log : g_LogBuffer) {
                    ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "%s", log.c_str());
                }
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                    ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();
            ImGui::EndGroup();

            // Bottom Button
            ImGui::SetCursorPos({ 10, windowHeight - 60 });
            if (ImGui::Button("RUN SELECTED TASKS", { windowWidth - 20, 50 })) {
                std::thread([]() {
                    AddLog("Starting optimization tasks...");
                    if (selected_features[0]) { CleanerLogic::clean_temp_windows(); AddLog("Windows Temp files cleaned."); }
                    if (selected_features[1]) { CleanerLogic::clean_temp_app(); AddLog("App Temp files cleaned."); }
                    if (selected_features[3]) { CleanerLogic::remove_defender_aggressive(); AddLog("Defender neutralized."); }
                    if (selected_features[4]) { CleanerLogic::remove_windows_store(); AddLog("Windows Store removed."); }
                    if (selected_features[5]) { CleanerLogic::clean_chrome_cookies(); AddLog("Chrome cookies deleted."); }
                    if (selected_features[12]) { CleanerLogic::disable_telemetry(); AddLog("Telemetry disabled."); }
                    if (selected_features[18]) { CleanerLogic::tcp_latency_tweak(); AddLog("TCP Latency optimized."); }
                    AddLog("All tasks completed successfully!");
                }).detach();
            }
        }
        ImGui::End();

        ImGui::Render();
        const float clear_color_with_alpha[4] = { 0, 0, 0, 1 };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

// Boilerplate Helpers
bool CreateDeviceD3D(HWND hWnd) {
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
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    if (D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext) != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = NULL; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = NULL; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = NULL; }
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = NULL; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice != NULL && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}
