#define WIN32_LEAN_AND_MEAN
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <tchar.h>
#include <cstdio>
#include <optional>
#include <filesystem>
#include "http/httplib.h"
#include "http/ip_helper.h"
#include "ImGuiNotify.hpp"
using namespace lib_net;
using namespace std;
namespace fs = std::filesystem;
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
// Data
static ID3D11Device *g_pd3dDevice = nullptr;
static ID3D11DeviceContext *g_pd3dDeviceContext = nullptr;
static IDXGISwapChain *g_pSwapChain = nullptr;
static bool g_SwapChainOccluded = false;
static UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView *g_mainRenderTargetView = nullptr;
static bool show_window = true;
static constexpr unsigned char font_data[] = {
#embed "font/selek.ttf"
};
static std::optional<ImGuiToast> g_pendingToast; // 延迟通知
static std::string g_iniPath;   // 全局绝对路径
// 数据持久化
enum Operator
{
    telecom,
    cmcc,
    unicom
};
char *opera[3] = {
    "telecom",
    "cmcc",
    "unicom"};
static struct LoginData
{
    char user[64] = "";
    char pass[64] = "";
    bool remember = false;
    bool autoStart = false;
    enum Operator def = Operator::cmcc;
    char ip[16] = "";
} g_Data;

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

void Setup();
void layout_ui();
static void InitIniPath()
{
    wchar_t exePathW[MAX_PATH];
    GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
    fs::path exeDir = fs::path(exePathW).parent_path();
    g_iniPath = (exeDir / L"config.ini").string();
}
static void LoadConfig()
{
    FILE *f = fopen(g_iniPath.c_str(), "r");
    if (f)
    {
        fscanf(f, "user=%63s\n", g_Data.user);
        fscanf(f, "pass=%63s\n", g_Data.pass);
        int r = 0, a = 0;
        fscanf(f, "remember=%d\n", &r);
        fscanf(f, "autostart=%d\n", &a);
        g_Data.remember = r != 0;
        g_Data.autoStart = a != 0;
        fscanf(f, "default_operator=%d\n", (int *)&g_Data.def);
        fclose(f);
    }
}
static void SaveConfig()
{
    FILE *f = fopen(g_iniPath.c_str(), "w");
    if (f)
    {
        fprintf(f, "user=%s\n", g_Data.user);
        fprintf(f, "pass=%s\n", g_Data.pass);
        fprintf(f, "remember=%d\n", g_Data.remember ? 1 : 0);
        fprintf(f, "autostart=%d\n", g_Data.autoStart ? 1 : 0);
        fprintf(f, "default_operator=%d\n", g_Data.def);
        fclose(f);
    }
}
bool GetIPv4Address()
{
    net_ada_list nal = net_adapter_helper::get_instance().get_info_win();
    int list_size = nal.size();
#if defined(os_is_win)
    for (auto item : nal)
    {
        for (auto item_ip : item._ip)
        {
            if (item_ip._inet4 != "0.0.0.0")
            {
                scanf_s(item_ip._inet4.c_str(), "%15s", g_Data.ip, (unsigned)_countof(g_Data.ip));
                return true;
            }
        }
    }
#endif
    return false;
}

bool init_verify()
{
    httplib::Client cli("10.17.8.18", 801);
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "/eportal/portal/login?callback=dr1003"
                  "&login_method=1"
                  "&user_account=%s%%40%s" // %% 转义成 %
                  "&user_password=%s"
                  "&wlan_user_ip=%s"
                  "&jsVersion=4.1.3"
                  "&terminal_type=1&lang=zh-cn&v=4104&lang=zh",
                  g_Data.user, opera[(int)g_Data.def], g_Data.pass, g_Data.ip);
    auto res = cli.Get(buf);
    if (res->body.find("\"result\":1") != std::string::npos)
    {
        g_pendingToast.emplace(ImGuiToastType::Success, 3000, "认证成功！");
        return true;
    }
    else
    {
        g_pendingToast.emplace(ImGuiToastType::Error, 3000, "认证失败或者已登录，请检查账号或密码!");
        return false;
    }
}

static void SetAutoStart(bool enable)
{
    HKEY hKey;
    const wchar_t *keyPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t *valueName = L"ImGuiAuth"; // 任意名称

    if (RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS)
    {
        if (enable)
        {
            // 获取自身完整路径
            wchar_t exePath[MAX_PATH];
            GetModuleFileNameW(nullptr, exePath, MAX_PATH);
            RegSetValueExW(hKey, valueName, 0, REG_SZ,
                           reinterpret_cast<const BYTE *>(exePath),
                           (wcslen(exePath) + 1) * sizeof(wchar_t));
        }
        else
        {
            RegDeleteValueW(hKey, valueName);
        }
        RegCloseKey(hKey);
    }
}

static bool IsAutoStartEnabled()
{
    HKEY hKey;
    const wchar_t *keyPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t *valueName = L"ImGuiAuth";
    bool exist = false;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS)
    {
        exist = RegQueryValueExW(hKey, valueName, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
        RegCloseKey(hKey);
    }
    return exist;
}
int main()
{
    // Make process DPI aware and obtain main monitor scale
    ImGui_ImplWin32_EnableDpiAwareness();
    float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));

    // Create application window
    WNDCLASSEXW wc = {sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ImGui Example", nullptr};
    wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Dear ImGui DirectX11 Example", WS_OVERLAPPEDWINDOW, 100, 100, (int)(1280 * main_scale), (int)(800 * main_scale), nullptr, nullptr, wc.hInstance, nullptr);

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Show the window
    ::ShowWindow(hwnd, SW_HIDE);
    ::UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // !!! 启用 docking 功能的支持
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;   // !!! 启用 viewport 功能的支持
    io.ConfigViewportsNoAutoMerge = true;

    // 加载大字体
    ImFontConfig font_config;
    font_config.PixelSnapH = false;
    font_config.OversampleH = 5;
    font_config.OversampleV = 5;
    font_config.RasterizerMultiply = 1.2f;
    font_config.SizePixels = 30.0f;
    io.Fonts->AddFontFromMemoryTTF((void *)font_data, sizeof(font_data), 30.0f, &font_config, io.Fonts->GetGlyphRangesChineseFull());
    io.Fonts->AddFontDefault(&font_config);

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    // ImGui::StyleColorsLight();

    // Setup scaling
    ImGuiStyle &style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale); // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
    InitIniPath();
    Setup();
    LoadConfig();
    g_Data.autoStart = IsAutoStartEnabled(); // 同步注册表
    while (!GetIPv4Address())
    {
        Sleep(1000);
    }
    bool verify = false;
    if (init_verify())
    {
        verify = true;
    }
    // Main loop
    while (show_window)
    {
        // Poll and handle messages (inputs, window resize, etc.)
        // See the WndProc() function below for our to dispatch events to the Win32 backend.
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                show_window = false;
        }
        if (!show_window)
            break;

        // Handle window being minimized or screen locked
        if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
        {
            ::Sleep(10);
            continue;
        }
        g_SwapChainOccluded = false;

        // Handle window resize (we don't resize directly in the WM_SIZE handler)
        if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        // Start the Dear ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        if (g_pendingToast.has_value())
        {
            ImGui::InsertNotification(g_pendingToast.value());
            g_pendingToast.reset(); // 只插一次
        }
        if (!verify)
        {
            layout_ui();
        }
        if (verify && ImGui::GetTime() > 2.0) // 2 秒后自动退出
        {
            ::PostQuitMessage(0);
            continue;
        }

        /**
         * Notifications Rendering Start
         */

        // Notifications style setup
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);   // Disable round borders
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f); // Disable borders

        // Notifications color setup
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.10f, 0.10f, 1.00f)); // Background color

        // Main rendering function
        ImGui::RenderNotifications();

        // ——————————————————————————————— WARNING ———————————————————————————————
        //  Argument MUST match the amount of ImGui::PushStyleVar() calls
        ImGui::PopStyleVar(2);
        // Argument MUST match the amount of ImGui::PushStyleColor() calls
        ImGui::PopStyleColor(1);

        /**
         * Notifications Rendering End
         */
        ImGui::Render();
        float clear_color[4] = {0.0f, 0.0f, 0.0f, 0.0f}; // R, G, B, Alpha
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        // Present
        HRESULT hr = g_pSwapChain->Present(1, 0); // Present with vsync
        // HRESULT hr = g_pSwapChain->Present(0, 0); // Present without vsync
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
    }
    // Cleanup
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
}
void SetUpColors(ImGuiStyle &style, ImVec4 *colors)
{
    // Moonlight style by deathsu/madam-herta
    // https://github.com/Madam-Herta/Moonlight/

    style.Alpha = 1.0f;
    style.DisabledAlpha = 1.0f;
    style.WindowPadding = ImVec2(12.0f, 12.0f);
    style.WindowRounding = 11.5f;
    style.WindowBorderSize = 0.0f;
    style.WindowMinSize = ImVec2(20.0f, 20.0f);
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    style.WindowMenuButtonPosition = ImGuiDir_Right;
    style.ChildRounding = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupRounding = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.FramePadding = ImVec2(20.0f, 3.400000095367432f);
    style.FrameRounding = 11.89999961853027f;
    style.FrameBorderSize = 0.0f;
    style.ItemSpacing = ImVec2(4.300000190734863f, 5.5f);
    style.ItemInnerSpacing = ImVec2(7.099999904632568f, 1.799999952316284f);
    style.CellPadding = ImVec2(12.10000038146973f, 9.199999809265137f);
    style.IndentSpacing = 0.0f;
    style.ColumnsMinSpacing = 4.900000095367432f;
    style.ScrollbarSize = 11.60000038146973f;
    style.ScrollbarRounding = 15.89999961853027f;
    style.GrabMinSize = 3.700000047683716f;
    style.GrabRounding = 20.0f;
    style.TabRounding = 0.0f;
    style.TabBorderSize = 0.0f;
    style.ColorButtonPosition = ImGuiDir_Right;
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

    style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.2745098173618317f, 0.3176470696926117f, 0.4509803950786591f, 1.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.0784313753247261f, 0.08627451211214066f, 0.1019607856869698f, 1.0f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.09250493347644806f, 0.100297249853611f, 0.1158798336982727f, 1.0f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.0784313753247261f, 0.08627451211214066f, 0.1019607856869698f, 1.0f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.1568627506494522f, 0.168627455830574f, 0.1921568661928177f, 1.0f);
    style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0784313753247261f, 0.08627451211214066f, 0.1019607856869698f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.1120669096708298f, 0.1262156516313553f, 0.1545064449310303f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.1568627506494522f, 0.168627455830574f, 0.1921568661928177f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.1568627506494522f, 0.168627455830574f, 0.1921568661928177f, 1.0f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.0470588244497776f, 0.05490196123719215f, 0.07058823853731155f, 1.0f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.0470588244497776f, 0.05490196123719215f, 0.07058823853731155f, 1.0f);
    style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.0784313753247261f, 0.08627451211214066f, 0.1019607856869698f, 1.0f);
    style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.09803921729326248f, 0.105882354080677f, 0.1215686276555061f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.0470588244497776f, 0.05490196123719215f, 0.07058823853731155f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.1176470592617989f, 0.1333333402872086f, 0.1490196138620377f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.1568627506494522f, 0.168627455830574f, 0.1921568661928177f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.1176470592617989f, 0.1333333402872086f, 0.1490196138620377f, 1.0f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.9725490212440491f, 1.0f, 0.4980392158031464f, 1.0f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.971993625164032f, 1.0f, 0.4980392456054688f, 1.0f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.7953379154205322f, 0.4980392456054688f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.1176470592617989f, 0.1333333402872086f, 0.1490196138620377f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.1821731775999069f, 0.1897992044687271f, 0.1974248886108398f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.1545050293207169f, 0.1545048952102661f, 0.1545064449310303f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.1414651423692703f, 0.1629818230867386f, 0.2060086131095886f, 1.0f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.1072951927781105f, 0.107295036315918f, 0.1072961091995239f, 1.0f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.0784313753247261f, 0.08627451211214066f, 0.1019607856869698f, 1.0f);
    style.Colors[ImGuiCol_Separator] = ImVec4(0.1293079704046249f, 0.1479243338108063f, 0.1931330561637878f, 1.0f);
    style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.1568627506494522f, 0.1843137294054031f, 0.250980406999588f, 1.0f);
    style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.1568627506494522f, 0.1843137294054031f, 0.250980406999588f, 1.0f);
    style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.1459212601184845f, 0.1459220051765442f, 0.1459227204322815f, 1.0f);
    style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.9725490212440491f, 1.0f, 0.4980392158031464f, 1.0f);
    style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.999999463558197f, 1.0f, 0.9999899864196777f, 1.0f);
    style.Colors[ImGuiCol_Tab] = ImVec4(0.0784313753247261f, 0.08627451211214066f, 0.1019607856869698f, 1.0f);
    style.Colors[ImGuiCol_TabHovered] = ImVec4(0.1176470592617989f, 0.1333333402872086f, 0.1490196138620377f, 1.0f);
    style.Colors[ImGuiCol_TabActive] = ImVec4(0.1176470592617989f, 0.1333333402872086f, 0.1490196138620377f, 1.0f);
    style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.0784313753247261f, 0.08627451211214066f, 0.1019607856869698f, 1.0f);
    style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.1249424293637276f, 0.2735691666603088f, 0.5708154439926147f, 1.0f);
    style.Colors[ImGuiCol_PlotLines] = ImVec4(0.5215686559677124f, 0.6000000238418579f, 0.7019608020782471f, 1.0f);
    style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.03921568766236305f, 0.9803921580314636f, 0.9803921580314636f, 1.0f);
    style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.8841201663017273f, 0.7941429018974304f, 0.5615870356559753f, 1.0f);
    style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.9570815563201904f, 0.9570719599723816f, 0.9570761322975159f, 1.0f);
    style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.0470588244497776f, 0.05490196123719215f, 0.07058823853731155f, 1.0f);
    style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.0470588244497776f, 0.05490196123719215f, 0.07058823853731155f, 1.0f);
    style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.1176470592617989f, 0.1333333402872086f, 0.1490196138620377f, 1.0f);
    style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.09803921729326248f, 0.105882354080677f, 0.1215686276555061f, 1.0f);
    style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.9356134533882141f, 0.9356129765510559f, 0.9356223344802856f, 1.0f);
    style.Colors[ImGuiCol_DragDropTarget] = ImVec4(0.4980392158031464f, 0.5137255191802979f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.266094446182251f, 0.2890366911888123f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.4980392158031464f, 0.5137255191802979f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.196078434586525f, 0.1764705926179886f, 0.5450980663299561f, 0.501960813999176f);
    style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.196078434586525f, 0.1764705926179886f, 0.5450980663299561f, 0.501960813999176f);
}

void Setup()
{
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    ImGuiStyle &style = ImGui::GetStyle();
    ImVec4 *colors = style.Colors;
    ImVec2 whole_content_size = io.DisplaySize;
    whole_content_size.x = whole_content_size.x * 0.3;
    whole_content_size.y = whole_content_size.y * 0.2;
    SetUpColors(style, colors);
}

void layout_ui()
{
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(550, 550));

    ImGui::Begin("Login", &show_window, ImGuiWindowFlags_NoCollapse);

    ImGui::Text("JXUST Web Auth");

    ImGui::Dummy(ImVec2(0, 12));

    ImGui::Text("一卡通账户");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##user", g_Data.user, sizeof(g_Data.user));

    ImGui::Text("密码");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##pass", g_Data.pass, sizeof(g_Data.pass), ImGuiInputTextFlags_Password);

    ImGui::Dummy(ImVec2(0, 8));

    if(ImGui::Checkbox("记住我", &g_Data.remember))
    {
        SaveConfig();
    }
    ImGui::SameLine(ImGui::GetWindowWidth() - 200);
    ImGui::Checkbox("开机自启动", &g_Data.autoStart);
    if (ImGui::IsItemDeactivatedAfterEdit()) // 松开鼠标时生效
    {
        SetAutoStart(g_Data.autoStart);
        SaveConfig();
    }
    ImGui::RadioButton("中国电信", (int *)&g_Data.def, 0);
    ImGui::SameLine();
    ImGui::RadioButton("中国移动", (int *)&g_Data.def, 1);
    ImGui::SameLine();
    ImGui::RadioButton("中国联通", (int *)&g_Data.def, 2);

    ImGui::Dummy(ImVec2(0, 18));

    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 120) * 0.5f);
    if (ImGui::Button("Login", ImVec2(120, 36)) || ImGui::IsKeyPressed(ImGuiKey_Enter))
    {
        init_verify();
    }

    ImGui::End();
}

bool CreateDeviceD3D(HWND hWnd)
{
    // Setup swap chain
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
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
    // createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0,
    };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED) // Try high-performance WARP software driver if hardware is not available.
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain)
    {
        g_pSwapChain->Release();
        g_pSwapChain = nullptr;
    }
    if (g_pd3dDeviceContext)
    {
        g_pd3dDeviceContext->Release();
        g_pd3dDeviceContext = nullptr;
    }
    if (g_pd3dDevice)
    {
        g_pd3dDevice->Release();
        g_pd3dDevice = nullptr;
    }
}

void CreateRenderTarget()
{
    ID3D11Texture2D *pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView)
    {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam); // Queue resize
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
