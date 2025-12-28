#include "guimain.hpp"
#include "drawutils.hpp"
#include "function/memory_utils.hpp"
#include "function/injector.hpp"
#include <d3d11.h>
#include <tchar.h>
#include <string>
#include <vector>
#include <random>
#include <thread>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <map>
#include <cmath>
#include <dwmapi.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <set>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "imgui_internal.h"

#include "poppins.h"
#include "fa_icons.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "json.hpp"
using json = nlohmann::json;

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

struct DllEntry {
    int id = 0;
    std::string name;
    std::string path;
    std::string description;
    std::string dateAdded;
    std::string customImageName;
    bool hasCustomImage = false;
    bool isFolder = false;
    bool recursive = true;
};

static std::set<std::string> g_FsIgnoreList;

static std::map<std::string, std::vector<std::string>> g_FolderSortOrders;

static bool showSortingOptions = false;
static float breadcrumbScrollTarget = 0.0f;

static bool currentViewRecursive = true;

static int g_NextDllId = 0;

struct ImageAsset {
    std::string name;
    ID3D11ShaderResourceView* texture = nullptr;
    int width = 0;
    int height = 0;
};

static std::map<std::string, ImageAsset> g_ImageCache;

static int g_SortTypeState = 0;
static int g_SortTimeState = 0;
static int g_SortAlphaState = 0;

std::string GetFileTimeString(const std::filesystem::directory_entry& entry) {
    try {
        auto ftime = std::filesystem::last_write_time(entry);
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
        );
        std::time_t cftime = std::chrono::system_clock::to_time_t(sctp);

        std::stringstream ss;
        struct tm tm_buf;

        if (localtime_s(&tm_buf, &cftime) == 0) {
            ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M");
        }
        else {
            return "Unknown";
        }
        return ss.str();
    }
    catch (...) { return "Unknown"; }
}

typedef enum _ACCENT_STATE {
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_GRADIENT = 1,
    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
    ACCENT_ENABLE_BLURBEHIND = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,
    ACCENT_INVALID_STATE = 5
} ACCENT_STATE;

typedef struct _ACCENT_POLICY {
    ACCENT_STATE AccentState;
    DWORD AccentFlags;
    DWORD GradientColor;
    DWORD AnimationId;
} ACCENT_POLICY;

typedef struct _WINDOWCOMPOSITIONATTRIBDATA {
    DWORD Attrib;
    PVOID pvData;
    SIZE_T cbData;
} WINDOWCOMPOSITIONATTRIBDATA;

typedef BOOL(WINAPI* PSetWindowCompositionAttribute)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);

void EnableBlur(HWND hwnd) {
    HMODULE hUser = GetModuleHandle(TEXT("user32.dll"));
    if (hUser) {
        PSetWindowCompositionAttribute SetWindowCompositionAttribute = (PSetWindowCompositionAttribute)GetProcAddress(hUser, "SetWindowCompositionAttribute");
        if (SetWindowCompositionAttribute) {

            ACCENT_POLICY policy = { ACCENT_ENABLE_BLURBEHIND, 0, 0, 0 };
            WINDOWCOMPOSITIONATTRIBDATA data = { 19, &policy, sizeof(ACCENT_POLICY) };
            SetWindowCompositionAttribute(hwnd, &data);
        }
    }

    MARGINS margins = { -1 };
    DwmExtendFrameIntoClientArea(hwnd, &margins);
}

static char targetNameBuffer[128] = "cs2.exe";
static char searchBuffer[128] = "";
static std::vector<DllEntry> dllList;

static int selectedDllId = -9999;

static std::string currentViewPath = "";
static std::vector<DllEntry> fsList;
static std::vector<std::string> navHistory = { "" };
static int navHistoryIndex = 0;

static bool showImageModal = false;
static int imageSelectionTargetIndex = -1;
static char imageSearchBuffer[128] = "";

static bool showFileBrowserModal = false;
static char fileBrowserSearchBuffer[128] = "";
static std::string fileBrowserCurrentPath = "";
static char* fileBrowserTargetPtr = nullptr;
static int fileBrowserReturnTarget = 0;
struct FileBrowserItem {
    std::string name;
    std::string path;
    bool isFolder;
};
static std::vector<FileBrowserItem> fileBrowserItems;

static int contextMenuTargetIndex = -1;

static bool bShowStealthSettings = false;
static int currentInjectionMethod = 0;
static int currentExecutionMethod = 0;
static bool bUseSyscalls = true;
static bool bEraseHeaders = true;
static bool bRandomJitter = false;
static bool bDriverAssistance = false;
static bool bRuntimeDecryption = false;

static bool showEditModal = false;
static int editIndex = -1;
static char editDescBuffer[512] = "";

static bool showEditPathModal = false;
static int editPathIndex = -1;
static char editPathBuffer[512] = "";

static bool showRenameModal = false;
static int renameIndex = -1;
static char renameBuffer[128] = "";

static bool showAddDllModal = false;
static char addDllPathBuffer[512] = "";

static bool showErrorModal = false;

static int errorReturnTarget = 0;

static bool showSortErrorModal = false;

static std::string logBuffer;
static void AddLog(const std::string& msg) {
    logBuffer += msg + "\n";
}

std::string GetCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    struct tm tm_buf;
    localtime_s(&tm_buf, &in_time_t);
    ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M");
    return ss.str();
}

std::string GetFileNameFromPath(const std::string& path) {

    std::string p = path;
    if (!p.empty() && (p.back() == '/' || p.back() == '\\')) {
        p.pop_back();
    }
    size_t lastSlash = p.find_last_of("/\\");
    if (lastSlash != std::string::npos) return p.substr(lastSlash + 1);
    return p;
}

std::string GetSmartDllName(const std::string& path) {

    std::string cleanPath = path;
    while (!cleanPath.empty() && (cleanPath.back() == '/' || cleanPath.back() == '\\')) {
        cleanPath.pop_back();
    }

    try {
        std::filesystem::path p(cleanPath);
        if (!p.has_filename()) return GetFileNameFromPath(cleanPath);

        std::string filename = p.filename().string();
        std::string stem = p.stem().string();
        std::filesystem::path parent = p.parent_path();

        std::filesystem::path potentialFolder = parent / stem;

        std::error_code ec;

        if (std::filesystem::exists(potentialFolder, ec) && std::filesystem::is_directory(potentialFolder, ec)) {

            return filename;
        }

        return stem;
    }
    catch (...) {
        return GetFileNameFromPath(cleanPath);
    }
}

bool IsValidPath(const std::string& path) {
    std::string pStr = path;

    while (!pStr.empty() && (pStr.back() == '/' || pStr.back() == '\\')) {
        pStr.pop_back();
    }

    std::error_code ec;

    if (!std::filesystem::exists(pStr, ec)) return false;

    if (std::filesystem::is_directory(pStr, ec)) return true;

    std::filesystem::path p(pStr);
    if (!p.has_extension()) return false;

    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".dll";
}

void RefreshFS(const std::string& path, bool keepState = false) {
    currentViewPath = path;

    if (path.empty()) {
        fsList.clear();
        return;
    }

    std::vector<DllEntry> diskItems;
    std::set<std::string> diskPaths;

    try {
        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            try {
                if (g_FsIgnoreList.count(entry.path().string())) continue;

                bool isDir = entry.is_directory();
                bool shouldAdd = false;

                if (isDir) {
                    shouldAdd = true;
                }
                else if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".dll") {
                        shouldAdd = true;
                    }
                }

                if (!shouldAdd) continue;

                DllEntry e;

                e.path = entry.path().string();
                e.name = GetSmartDllName(e.path);
                e.dateAdded = GetFileTimeString(entry);
                e.description = "";
                e.isFolder = isDir;
                e.recursive = true;

                diskItems.push_back(e);
                diskPaths.insert(e.path);
            }
            catch (...) { continue; }
        }
    }
    catch (...) {}

    int minId = -1;
    if (keepState) {
        for (const auto& i : fsList) if (i.id < minId) minId = i.id;
    }
    int tempIdCounter = minId - 1;

    if (keepState && !fsList.empty()) {

        auto it = fsList.begin();
        while (it != fsList.end()) {
            if (diskPaths.find(it->path) == diskPaths.end()) {
                it = fsList.erase(it);
            }
            else {
                ++it;
            }
        }

        for (auto& item : diskItems) {
            bool exists = false;
            for (const auto& existing : fsList) {
                if (existing.path == item.path) { exists = true; break; }
            }
            if (!exists) {
                item.id = tempIdCounter--;
                fsList.push_back(item);
            }
        }
    }
    else {

        fsList = diskItems;

        for (auto& e : fsList) e.id = tempIdCounter--;

        bool isCustom = (g_SortTypeState == 0 && g_SortTimeState == 0 && g_SortAlphaState == 0);
        if (isCustom) {
            auto it = g_FolderSortOrders.find(path);
            if (it != g_FolderSortOrders.end()) {
                const std::vector<std::string>& savedOrder = it->second;
                std::vector<DllEntry> sortedList;
                std::vector<DllEntry> remaining = fsList;

                for (const auto& name : savedOrder) {
                    auto found = std::find_if(remaining.begin(), remaining.end(),
                        [&](const DllEntry& e) { return e.name == name; });

                    if (found != remaining.end()) {
                        sortedList.push_back(*found);
                        remaining.erase(found);
                    }
                }

                sortedList.insert(sortedList.end(), remaining.begin(), remaining.end());
                fsList = sortedList;
            }
        }
    }

    if (g_SortTypeState != 0 || g_SortTimeState != 0 || g_SortAlphaState != 0) {
        std::sort(fsList.begin(), fsList.end(), [](const DllEntry& a, const DllEntry& b) {

            if (g_SortTypeState != 0) {
                bool aFolder = a.isFolder;
                bool bFolder = b.isFolder;
                if (aFolder != bFolder) {
                    return (g_SortTypeState == 2) ? (aFolder > bFolder) : (aFolder < bFolder);
                }
            }

            if (g_SortTimeState != 0) {
                if (g_SortTimeState == 1) return a.dateAdded < b.dateAdded;
                else return a.dateAdded > b.dateAdded;
            }
            if (g_SortAlphaState != 0) {
                std::string na = a.name; std::string nb = b.name;
                std::transform(na.begin(), na.end(), na.begin(), ::tolower);
                std::transform(nb.begin(), nb.end(), nb.begin(), ::tolower);
                if (g_SortAlphaState == 1) return na < nb;
                else return na > nb;
            }
            return a.id > b.id;
            });
    }
}

void RefreshFileBrowser(std::string path) {
    fileBrowserCurrentPath = path;
    fileBrowserItems.clear();

    try {
        if (path.empty()) path = std::filesystem::current_path().string();
        std::filesystem::path p(path);

        if (p.has_parent_path() && p.parent_path() != p) {
            fileBrowserItems.push_back({ "..", p.parent_path().string(), true });
        }

        for (const auto& entry : std::filesystem::directory_iterator(p)) {
            FileBrowserItem item;
            item.path = entry.path().string();
            item.name = entry.path().filename().string();
            item.isFolder = entry.is_directory();

            bool shouldAdd = false;
            if (item.isFolder) shouldAdd = true;
            else {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".dll" || ext == ".exe") shouldAdd = true;
            }

            if (shouldAdd) fileBrowserItems.push_back(item);
        }
    }
    catch (...) {

    }

    std::sort(fileBrowserItems.begin(), fileBrowserItems.end(), [](const FileBrowserItem& a, const FileBrowserItem& b) {
        if (a.name == "..") return true;
        if (b.name == "..") return false;
        if (a.isFolder != b.isFolder) return a.isFolder > b.isFolder;
        return a.name < b.name;
        });
}

void NavigateTo(std::string path, bool recursiveSettings = true) {
    if (path == currentViewPath) return;

    if (navHistoryIndex < navHistory.size() - 1) {
        navHistory.resize(navHistoryIndex + 1);
    }
    navHistory.push_back(path);
    navHistoryIndex = (int)navHistory.size() - 1;

    currentViewRecursive = recursiveSettings;
    selectedDllId = -9999;

    RefreshFS(path, false);
}

void NavigateBack() {
    if (navHistoryIndex > 0) {
        navHistoryIndex--;
        selectedDllId = -9999;
        RefreshFS(navHistory[navHistoryIndex], false);
    }
}

void NavigateForward() {
    if (navHistoryIndex < navHistory.size() - 1) {
        navHistoryIndex++;
        selectedDllId = -9999;
        RefreshFS(navHistory[navHistoryIndex], false);
    }
}

bool LoadTextureFromFile(const char* filename, ID3D11ShaderResourceView** out_srv, int* out_width, int* out_height)
{

    int image_width = 0;
    int image_height = 0;
    unsigned char* image_data = stbi_load(filename, &image_width, &image_height, NULL, 4);
    if (image_data == NULL)
        return false;

    D3D11_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Width = image_width;
    desc.Height = image_height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;

    ID3D11Texture2D* pTexture = NULL;
    D3D11_SUBRESOURCE_DATA subResource;
    subResource.pSysMem = image_data;
    subResource.SysMemPitch = desc.Width * 4;
    subResource.SysMemSlicePitch = 0;
    g_pd3dDevice->CreateTexture2D(&desc, &subResource, &pTexture);

    if (!pTexture) {
        stbi_image_free(image_data);
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
    ZeroMemory(&srvDesc, sizeof(srvDesc));
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = desc.MipLevels;
    srvDesc.Texture2D.MostDetailedMip = 0;
    g_pd3dDevice->CreateShaderResourceView(pTexture, &srvDesc, out_srv);
    pTexture->Release();

    *out_width = image_width;
    *out_height = image_height;
    stbi_image_free(image_data);

    return true;
}

void RefreshImageCache() {
    if (!g_pd3dDevice) return;

    std::set<std::string> currentFiles;
    if (std::filesystem::exists("IMG")) {
        try {
            for (const auto& entry : std::filesystem::directory_iterator("IMG")) {
                if (entry.is_regular_file()) {
                    std::string path = entry.path().string();
                    std::string filename = entry.path().filename().string();
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp") {
                        currentFiles.insert(filename);

                        if (g_ImageCache.find(filename) == g_ImageCache.end()) {
                            ImageAsset newAsset;
                            newAsset.name = filename;
                            if (LoadTextureFromFile(path.c_str(), &newAsset.texture, &newAsset.width, &newAsset.height)) {
                                g_ImageCache[filename] = newAsset;
                            }
                        }
                    }
                }
            }
        }
        catch (...) {}
    }

    for (auto it = g_ImageCache.begin(); it != g_ImageCache.end(); ) {
        if (currentFiles.find(it->first) == currentFiles.end()) {
            if (it->second.texture) {
                it->second.texture->Release();
            }
            it = g_ImageCache.erase(it);
        }
        else {
            ++it;
        }
    }
}

void SaveConfig() {
    try {
        json j;
        j["targetProcess"] = targetNameBuffer;

        j["stealth"] = {
            {"injectionMethod", currentInjectionMethod},
            {"executionMethod", currentExecutionMethod},
            {"useSyscalls", bUseSyscalls},
            {"eraseHeaders", bEraseHeaders},
            {"randomJitter", bRandomJitter},
            {"driverAssistance", bDriverAssistance},
            {"runtimeDecryption", bRuntimeDecryption}
        };

        j["dlls"] = json::array();
        for (const auto& dll : dllList) {
            j["dlls"].push_back({
                {"id", dll.id},
                {"name", dll.name},
                {"path", dll.path},
                {"description", dll.description},
                {"dateAdded", dll.dateAdded},
                {"customImageName", dll.customImageName},
                {"hasCustomImage", dll.hasCustomImage},
                {"isFolder", dll.isFolder},
                {"recursive", dll.recursive}
                });
        }

        j["ignoreList"] = g_FsIgnoreList;

        j["folderSorts"] = g_FolderSortOrders;

        std::ofstream o("config.json");
        o << std::setw(4) << j << std::endl;
    }
    catch (const std::exception& e) {
        AddLog(std::string("Save failed: ") + e.what());
    }
}

void LoadConfig() {
    if (!std::filesystem::exists("config.json")) return;

    try {
        std::ifstream i("config.json");
        json j;
        i >> j;

        if (j.contains("targetProcess")) {
            std::string t = j["targetProcess"];
            if (t.length() < sizeof(targetNameBuffer))
                strcpy_s(targetNameBuffer, t.c_str());
        }

        if (j.contains("stealth")) {
            auto& s = j["stealth"];
            currentInjectionMethod = s.value("injectionMethod", 0);
            currentExecutionMethod = s.value("executionMethod", 0);
            bUseSyscalls = s.value("useSyscalls", true);
            bEraseHeaders = s.value("eraseHeaders", true);
            bRandomJitter = s.value("randomJitter", false);
            bDriverAssistance = s.value("driverAssistance", false);
            bRuntimeDecryption = s.value("runtimeDecryption", false);
        }

        if (j.contains("dlls") && j["dlls"].is_array()) {
            dllList.clear();
            int maxId = 0;
            for (auto& element : j["dlls"]) {
                DllEntry e;
                e.id = element.value("id", 0);
                e.name = element.value("name", "Unknown.dll");
                e.path = element.value("path", "");
                e.description = element.value("description", "");
                e.dateAdded = element.value("dateAdded", "");
                e.customImageName = element.value("customImageName", "");
                e.hasCustomImage = element.value("hasCustomImage", false);
                e.isFolder = element.value("isFolder", false);
                e.recursive = element.value("recursive", true);

                if (std::filesystem::exists(e.path) && std::filesystem::is_directory(e.path)) {
                    e.isFolder = true;
                }
                if (e.id > maxId) maxId = e.id;
                dllList.push_back(e);
            }
            g_NextDllId = maxId;
        }
        if (j.contains("ignoreList")) {
            g_FsIgnoreList = j["ignoreList"].get<std::set<std::string>>();
        }

        if (j.contains("folderSorts")) {
            g_FolderSortOrders = j["folderSorts"].get<std::map<std::string, std::vector<std::string>>>();
        }

        AddLog("Settings Loaded.");
    }
    catch (const std::exception& e) {
        AddLog(std::string("Load failed: ") + e.what());
    }
}

namespace GUI {

    void TextCentered(const char* text) {
        float winWidth = ImGui::GetWindowSize().x;
        float textWidth = ImGui::CalcTextSize(text).x;
        if (textWidth < winWidth) {
            ImGui::SetCursorPosX((winWidth - textWidth) * 0.5f);
        }
        ImGui::Text("%s", text);
    }

    void ApplyTheme() {
        ImGuiStyle& style = ImGui::GetStyle();

        style.WindowRounding = 10.0f;
        style.ChildRounding = 8.0f;
        style.FrameRounding = 6.0f;
        style.PopupRounding = 8.0f;
        style.ScrollbarRounding = 12.0f;
        style.GrabRounding = 6.0f;

        style.WindowPadding = ImVec2(10, 10);
        style.ItemSpacing = ImVec2(6, 6);
        style.ScrollbarSize = 6.0f;

        ImVec4* colors = style.Colors;

        colors[ImGuiCol_Text] = ImVec4(0.92f, 0.94f, 0.98f, 1.00f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.55f, 0.65f, 1.00f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.07f, 0.11f, 0.45f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.06f, 0.07f, 0.11f, 0.25f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.06f, 0.07f, 0.11f, 0.90f);
        colors[ImGuiCol_Border] = ImVec4(0.20f, 0.25f, 0.35f, 0.40f);
        colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.12f, 0.18f, 0.30f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.15f, 0.18f, 0.25f, 0.50f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.22f, 0.30f, 0.60f);
        colors[ImGuiCol_Header] = ImVec4(0.15f, 0.20f, 0.35f, 0.50f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.20f, 0.25f, 0.45f, 0.70f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.25f, 0.30f, 0.55f, 0.80f);
        colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.04f, 0.08f, 0.15f);
        colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.15f, 0.35f, 0.65f, 0.4f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.20f, 0.40f, 0.75f, 0.60f);
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.25f, 0.50f, 0.90f, 0.80f);
        colors[ImGuiCol_ResizeGrip] = ImVec4(0.15f, 0.35f, 0.65f, 0.30f);
        colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.20f, 0.40f, 0.75f, 0.60f);
        colors[ImGuiCol_ResizeGripActive] = ImVec4(0.25f, 0.50f, 0.90f, 0.80f);
        colors[ImGuiCol_Button] = ImVec4(0.10f, 0.30f, 0.55f, 0.30f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.15f, 0.35f, 0.65f, 0.60f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.08f, 0.25f, 0.45f, 0.70f);
        colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.25f, 0.35f, 0.50f);
    }

    struct DragState {
        int draggedId = -9999;
        int dragStartIndex = -9999;
        ImVec2 dragStartMousePos;
        bool isDragging = false;
        std::vector<int> visualOrder;
        std::map<int, ImVec2> currentPositions;
    };
    static DragState ds;

    void RenderLoop() {
        static bool bInit = false;
        if (!bInit) {
            Utils::SetLogCallback(AddLog);
            bInit = true;
            LoadConfig();
            if (!std::filesystem::exists("IMG")) {
                std::filesystem::create_directory("IMG");
            }
        }

        WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ModernUI", nullptr };
        ::RegisterClassExW(&wc);
        int w = 1000, h = 650;
        HWND hWnd = ::CreateWindowW(wc.lpszClassName, L"Modern Injector", WS_POPUP, 100, 100, w, h, nullptr, nullptr, wc.hInstance, nullptr);

        DWM_WINDOW_CORNER_PREFERENCE preference = (DWM_WINDOW_CORNER_PREFERENCE)DWMWCP_ROUND;
        DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
        EnableBlur(hWnd);

        if (!CreateDeviceD3D(hWnd)) {
            CleanupDeviceD3D();
            ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
            return;
        }

        ::ShowWindow(hWnd, SW_SHOWDEFAULT);
        ::UpdateWindow(hWnd);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;

        ImFontConfig font_config;
        font_config.PixelSnapH = true;
        font_config.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromMemoryTTF((void*)poppins_font_data, poppins_font_size, 18.0f, &font_config);

        ImFontConfig icons_config;
        icons_config.MergeMode = true;
        icons_config.PixelSnapH = true;
        icons_config.FontDataOwnedByAtlas = false;
        icons_config.GlyphMinAdvanceX = 13.0f;
        icons_config.GlyphOffset = ImVec2(0, 0);
        io.Fonts->AddFontFromMemoryTTF((void*)fa_icons_data, fa_icons_size, 16.0f, &icons_config, icons_fa);

        ApplyTheme();

        ImGui_ImplWin32_Init(hWnd);
        ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

        static bool isWindowDragging = false;
        static POINT dragStartPos;
        static RECT winStartRect;
        static float scanTimer = 0.0f;
        static float stealthState = 0.0f;

        auto HandleWindowDrag = [](HWND h) {
            bool isHoveringItem = ImGui::IsAnyItemHovered() || ImGui::IsAnyItemActive();
            if (ImGui::IsWindowHovered() && !isHoveringItem && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                isWindowDragging = true;
                GetCursorPos(&dragStartPos);
                GetWindowRect(h, &winStartRect);
            }
            };

        bool done = false;
        while (!done) {
            MSG msg;
            while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
                ::TranslateMessage(&msg);
                ::DispatchMessage(&msg);
                if (msg.message == WM_QUIT) done = true;
            }
            if (done) break;

            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            scanTimer += ImGui::GetIO().DeltaTime;
            if (scanTimer > 1.0f) {
                RefreshImageCache();

                if (!currentViewPath.empty() && !ds.isDragging) {
                    RefreshFS(currentViewPath, true);
                }

                scanTimer = 0.0f;
            }

            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);

            ImGui::Begin("Main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

            HandleWindowDrag(hWnd);

            if (isWindowDragging) {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    POINT curPos;
                    GetCursorPos(&curPos);
                    SetWindowPos(hWnd, nullptr, winStartRect.left + (curPos.x - dragStartPos.x), winStartRect.top + (curPos.y - dragStartPos.y), 0, 0, SWP_NOSIZE | SWP_NOZORDER);
                }
                else isWindowDragging = false;
            }

            float totalHeight = ImGui::GetContentRegionAvail().y;
            float leftPanelWidth = 300.0f;

            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
            ImGui::BeginChild("LeftPanel", ImVec2(leftPanelWidth, totalHeight), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            HandleWindowDrag(hWnd);

            ImGui::Spacing();
            ImGui::TextDisabled(" TARGET PROCESS");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 10);

            if (ImGui::InputText("##Target", targetNameBuffer, sizeof(targetNameBuffer))) {
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                SaveConfig();
            }

            ImGui::Spacing();
            ImGui::Separator();

            static float calculatedContentHeight = 180.0f;

            float minHeight = 180.0f;

            static float activeHeightTarget = 180.0f;

            if (bShowStealthSettings) {

                float realTarget = (calculatedContentHeight > 10.0f) ? calculatedContentHeight : minHeight;

                activeHeightTarget = Lerp(activeHeightTarget, realTarget, ImGui::GetIO().DeltaTime * 15.0f);
            }

            float targetState = bShowStealthSettings ? 1.0f : 0.0f;
            stealthState = Lerp(stealthState, targetState, ImGui::GetIO().DeltaTime * 8.0f);

            float currentMidHeight = Lerp(minHeight, activeHeightTarget, stealthState);

            float injectBtnHeight = 60.0f;
            float topBuffer = 80.0f;
            float payloadAreaHeight = totalHeight - (topBuffer + currentMidHeight + injectBtnHeight) - 34.0f;
            if (payloadAreaHeight < 50.0f) payloadAreaHeight = 50.0f;

            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.07f, 0.11f, 0.25f));

            ImGui::BeginChild("PayloadArea", ImVec2(0, payloadAreaHeight), true);
            UpdateSmoothScroll("PayloadAreaSmooth");

            bool viewingHome = currentViewPath.empty();
            std::vector<DllEntry>* activeList = viewingHome ? &dllList : &fsList;

            DllEntry* selectedEntry = nullptr;
            for (auto& entry : *activeList) {
                if (entry.id == selectedDllId) {
                    selectedEntry = &entry;
                    break;
                }
            }

            if (selectedEntry) {
                DllEntry& sEntry = *selectedEntry;
                ImGui::Spacing();

                float winWidth = ImGui::GetWindowSize().x;
                float winPos = ImGui::GetWindowPos().x;
                float layoutBoxHeight = 130.0f;

                ImVec2 containerStartPos = ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton("ImgContainer", ImVec2(winWidth, layoutBoxHeight));

                ImVec2 centerPos = ImVec2(
                    winPos + winWidth * 0.5f,
                    containerStartPos.y + layoutBoxHeight * 0.5f
                );

                bool imageDisplayed = false;
                if (sEntry.hasCustomImage && !sEntry.customImageName.empty()) {
                    auto it = g_ImageCache.find(sEntry.customImageName);
                    if (it != g_ImageCache.end() && it->second.texture) {
                        imageDisplayed = true;
                        float imgAspect = (float)it->second.width / (float)it->second.height;
                        float boxAspect = winWidth / layoutBoxHeight;
                        float drawWidth, drawHeight;
                        if (imgAspect > boxAspect) {
                            drawHeight = layoutBoxHeight;
                            drawWidth = drawHeight * imgAspect;
                            if (drawWidth > winWidth * 0.9f) {
                                drawWidth = winWidth * 0.9f;
                                drawHeight = drawWidth / imgAspect;
                            }
                        }
                        else {
                            drawHeight = layoutBoxHeight;
                            drawWidth = drawHeight * imgAspect;
                        }
                        ImGui::SetCursorScreenPos(ImVec2(centerPos.x - drawWidth * 0.5f, centerPos.y - drawHeight * 0.5f));
                        ImGui::Image((void*)it->second.texture, ImVec2(drawWidth, drawHeight));
                    }
                }

                if (!imageDisplayed) {
                    float aspectRatio = 1.77f;
                    std::string n = sEntry.customImageName;
                    std::transform(n.begin(), n.end(), n.begin(), ::tolower);

                    if (n.find("tall") != std::string::npos || n.find("portrait") != std::string::npos) {
                        aspectRatio = 0.66f;
                    }
                    else if (n.find("sq") != std::string::npos || n.find("box") != std::string::npos) {
                        aspectRatio = 1.0f;
                    }
                    else if (n.find("wide") != std::string::npos || n.find("banner") != std::string::npos) {
                        aspectRatio = 1.77f;
                    }
                    else if (!sEntry.hasCustomImage) {
                        aspectRatio = 0.75f;
                    }

                    float targetH = layoutBoxHeight;
                    float targetW = targetH * aspectRatio;
                    if (targetW > winWidth * 0.85f) {
                        targetW = winWidth * 0.85f;
                        targetH = targetW / aspectRatio;
                    }

                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    float placeholderShiftX = -15.0f;

                    if (sEntry.hasCustomImage) {
                        ImGui::PushFont(io.Fonts->Fonts[0]);
                        float oldScale = ImGui::GetFont()->Scale;
                        float iconSize = targetH * 0.4f;
                        ImGui::GetFont()->Scale = iconSize / 16.0f;
                        ImVec2 textSize = ImGui::CalcTextSize(ICON_FA_IMG);
                        ImVec2 textPos = ImVec2(centerPos.x - textSize.x * 0.5f + placeholderShiftX, centerPos.y - textSize.y * 0.5f - 5.0f);
                        drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize() * ImGui::GetFont()->Scale, textPos, IM_COL32(255, 255, 255, 255), ICON_FA_IMG);
                        ImGui::GetFont()->Scale = oldScale;
                        ImGui::PopFont();
                    }
                    else {
                        ImGui::PushFont(io.Fonts->Fonts[0]);
                        float oldScale = ImGui::GetFont()->Scale;
                        float iconSize = targetH * 0.5f;
                        ImGui::GetFont()->Scale = iconSize / 16.0f;
                        const char* displayIcon = sEntry.isFolder ? ICON_FA_FOLDER : ICON_FA_FILE_CODE;
                        ImVec2 textSize = ImGui::CalcTextSize(displayIcon);
                        ImVec2 textPos = ImVec2(centerPos.x - textSize.x * 0.5f + placeholderShiftX + -5.0f, centerPos.y - textSize.y * 0.5f - 5.0f);
                        drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize() * ImGui::GetFont()->Scale, textPos, IM_COL32(200, 200, 200, 255), displayIcon);
                        ImGui::GetFont()->Scale = oldScale;
                        ImGui::PopFont();
                    }
                }

                ImGui::Spacing();
                ImGui::Spacing();
                TextCentered(sEntry.name.c_str());
                ImGui::Spacing();

                if (!sEntry.dateAdded.empty()) {
                    std::string dateStr = "Added: " + sEntry.dateAdded;
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.7f, 1.0f));
                    TextCentered(dateStr.c_str());
                    ImGui::PopStyleColor();
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                if (!sEntry.description.empty()) {
                    ImGui::TextWrapped("%s", sEntry.description.c_str());
                }
                else if (sEntry.isFolder) {
                    ImGui::TextWrapped("Double-click or Select to browse folder contents.");
                }
                else {
                    ImGui::TextWrapped("No description provided.");
                }
            }
            else {
                float availY = ImGui::GetContentRegionAvail().y;
                ImGui::SetCursorPosY(availY * 0.45f);
                TextCentered("Select a Payload");
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();

            ImVec2 defaultPadding = ImGui::GetStyle().WindowPadding;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::BeginChild("MidSection", ImVec2(0, currentMidHeight), true);

            float consoleAlpha = ImClamp(1.0f - (stealthState * 2.5f), 0.0f, 1.0f);
            float settingsAlpha = ImClamp((stealthState - 0.4f) * 2.0f, 0.0f, 1.0f);

            if (consoleAlpha > 0.01f)
            {
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, consoleAlpha);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.05f, 0.05f, 0.6f * consoleAlpha));
                ImGui::BeginChild("ConsoleLog", ImVec2(0, 0), false);
                ImGui::SetCursorPos(ImVec2(5, 4));
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f);
                ImGui::TextDisabled("ERROR CONSOLE");
                ImGui::Separator();
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2));
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 2.0f);
                ImGui::InputTextMultiline("##LogOut", (char*)logBuffer.c_str(), logBuffer.size() + 1, ImVec2(-1, -1), ImGuiInputTextFlags_ReadOnly);
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();
            }

            if (settingsAlpha > 0.01f)
            {
                ImGui::SetCursorPos(ImVec2(defaultPadding.x, defaultPadding.y));
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, settingsAlpha);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, defaultPadding);

                ImGui::BeginChild("SettingsInner", ImVec2(-defaultPadding.x + -10.0f, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize);
                ImGui::SetWindowFontScale(0.9f);
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 3));

                ImGui::Text("Injection Technique:");
                const char* injectionMethods[] = { "Manual Map (Reloc)", "Thread Hijacking" };
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                if (ImGui::Combo("##x", &currentInjectionMethod, injectionMethods, IM_ARRAYSIZE(injectionMethods))) SaveConfig();
                ImGui::Text("Execution Method:");
                const char* executionMethods[] = { "Syscalls", "Window Api" };
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                if (ImGui::Combo("##xx", &currentExecutionMethod, executionMethods, IM_ARRAYSIZE(executionMethods))) SaveConfig();
                ImGui::Dummy(ImVec2(0, 5));
                ImGui::Text("Advanced Options:");
                ImGui::Columns(2, "StealthCols", false);
                if (SmoothCheckbox("Direct Syscalls", &bUseSyscalls)) SaveConfig();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bypasses user-mode hooks (e.g. MinHook) by calling syscall stub directly.");
                if (SmoothCheckbox("Erase Headers", &bEraseHeaders)) SaveConfig();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Removes DOS/NT headers from injected memory to hinder analysis.");
                ImGui::NextColumn();
                if (SmoothCheckbox("Timing Jitter", &bRandomJitter)) SaveConfig();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Adds random delays to confuse heuristic timing checks.");
                ImGui::Columns(1);
                ImGui::PopStyleVar();
                ImGui::SetWindowFontScale(1.0f);

                float contentBottom = ImGui::GetCursorPosY() + defaultPadding.y + 5.0f;

                if (contentBottom < 100.0f) contentBottom = 100.0f;

                calculatedContentHeight = ImGui::GetCursorPosY() + 10.0f;

                ImGui::EndChild();
                ImGui::PopStyleVar();
                ImGui::PopStyleVar();
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();

            ImGui::SetCursorPosX(ImGui::GetStyle().WindowPadding.x + 5);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.30f, 0.55f, 0.30f));
            if (GlowButton(bShowStealthSettings ? "Stealth Settings [-]" : "Stealth Settings [+]", ImVec2(ImGui::GetContentRegionAvail().x - 15.0f, 0))) {
                bShowStealthSettings = !bShowStealthSettings;
            }
            ImGui::PopStyleColor();

            ImGui::SetCursorPosY(totalHeight - 55);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.45f, 0.85f, 0.30f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.1f, 0.55f, 0.95f, 0.60f));
            ImGui::SetCursorPosX(15.0f);
            if (GlowButton("INJECT", ImVec2(ImGui::GetContentRegionAvail().x - 15.0f, 50))) {
                AddLog("Injection Sequence Started...");
                AddLog("[INFO] Target: " + std::string(targetNameBuffer));
                if (selectedEntry) {
                    AddLog("[INFO] Payload: " + selectedEntry->name);
                }
            }
            ImGui::PopStyleColor(2);

            ImGui::EndChild();
            ImGui::PopStyleColor();

            ImGui::SameLine();
            {
                ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y), ImVec2(p.x, p.y + totalHeight), ImGui::GetColorU32(ImVec4(0.2f, 0.3f, 0.5f, 0.5f)));
                ImGui::Dummy(ImVec2(1, totalHeight));
            }
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
            ImGui::BeginChild("RightPanel", ImVec2(0, totalHeight), false, ImGuiWindowFlags_NoScrollWithMouse);
            HandleWindowDrag(hWnd);

            static float folderViewAnim = 0.0f;
            float targetAnim = viewingHome ? 0.0f : 1.0f;
            folderViewAnim = Lerp(folderViewAnim, targetAnim, ImGui::GetIO().DeltaTime * 10.0f);

            static bool showResetHyperspaceModal = false;

            ImGui::BeginGroup();

            float totalAvailWidth = ImGui::GetContentRegionAvail().x;
            float addButtonMaxWidth = 120.0f;
            float spacing = ImGui::GetStyle().ItemSpacing.x;

            float currentAddBtnWidth = addButtonMaxWidth * (1.0f - folderViewAnim);

            float searchBarWidth = totalAvailWidth - currentAddBtnWidth - (currentAddBtnWidth > 1.0f ? spacing : 0.0f);

            ImGui::SetNextItemWidth(searchBarWidth);
            ImGui::InputTextWithHint("##SearchRight", ICON_FA_SEARCH " Search...", searchBuffer, sizeof(searchBuffer));

            if (currentAddBtnWidth > 1.0f) {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.30f, 0.55f, 0.30f));

                ImGui::PushClipRect(ImGui::GetCursorScreenPos(), ImVec2(ImGui::GetCursorScreenPos().x + currentAddBtnWidth, ImGui::GetCursorScreenPos().y + 30), true);

                if (currentAddBtnWidth < 100.0f) {
                    if (GlowButton(ICON_FA_PLUS, ImVec2(currentAddBtnWidth, 0))) {
                        memset(addDllPathBuffer, 0, sizeof(addDllPathBuffer));
                        showAddDllModal = true;
                    }
                }
                else {
                    if (GlowButton(ICON_FA_PLUS " Add Item", ImVec2(currentAddBtnWidth, 0))) {
                        memset(addDllPathBuffer, 0, sizeof(addDllPathBuffer));
                        showAddDllModal = true;
                    }
                }
                ImGui::PopClipRect();
                ImGui::PopStyleColor();
            }
            ImGui::EndGroup();

            ImGui::Dummy(ImVec2(0, 5));
            ImGui::BeginGroup();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            if (navHistoryIndex > 0) {
                if (ImGui::Button(ICON_FA_ARROW_LEFT "##BackBtn")) NavigateBack();
            }
            else {
                ImGui::BeginDisabled();
                ImGui::Button(ICON_FA_ARROW_LEFT "##BackBtn");
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            if (navHistoryIndex < navHistory.size() - 1) {
                if (ImGui::Button(ICON_FA_ARROW_RIGHT "##FwdBtn")) NavigateForward();
            }
            else {
                ImGui::BeginDisabled();
                ImGui::Button(ICON_FA_ARROW_RIGHT "##FwdBtn");
                ImGui::EndDisabled();
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();

            float availableWidth = ImGui::GetContentRegionAvail().x - 40.0f;
            float arrowBtnWidth = 20.0f;

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            if (ImGui::Button(ICON_FA_ANGLE_LEFT "##ScrollLeft", ImVec2(arrowBtnWidth, 0))) {
                breadcrumbScrollTarget -= 100.0f;
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.02f, 0.02f, 0.04f, 0.3f));
            ImGui::BeginChild("Breadcrumbs", ImVec2(availableWidth - arrowBtnWidth * 2 - 10, 26), false, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollbar);

            float currentScroll = ImGui::GetScrollX();
            float maxScroll = ImGui::GetScrollMaxX();
            if (breadcrumbScrollTarget < 0) breadcrumbScrollTarget = 0;
            if (breadcrumbScrollTarget > maxScroll) breadcrumbScrollTarget = maxScroll;
            float newScroll = Lerp(currentScroll, breadcrumbScrollTarget, ImGui::GetIO().DeltaTime * 10.0f);
            if (std::abs(newScroll - currentScroll) > 0.5f) ImGui::SetScrollX(newScroll);
            else ImGui::SetScrollX(breadcrumbScrollTarget);

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));

            ImGui::AlignTextToFramePadding();
            if (ImGui::SmallButton(ICON_FA_HOME " Home")) NavigateTo("");

            if (!currentViewPath.empty()) {
                ImGui::SameLine(); ImGui::Text("/"); ImGui::SameLine();

                std::filesystem::path p(currentViewPath);
                std::vector<std::filesystem::path> parts;
                for (auto it = p.begin(); it != p.end(); ++it) parts.push_back(*it);

                std::filesystem::path buildPath;

                std::map<std::string, int> nameCounts;

                for (size_t i = 0; i < parts.size(); ++i) {
                    if (i > 0) buildPath /= parts[i];
                    else buildPath = parts[i];

                    std::string part = parts[i].string();
                    if (part != "\\" && part != "/" && !part.empty()) {
                        std::string navPath = buildPath.string();

                        if (part.size() == 2 && part[1] == ':' && navPath.back() != '\\' && navPath.back() != '/') {
                            navPath += "\\";
                        }

                        int count = nameCounts[part]++;

                        std::string uniqueLabel;
                        if (count < 10) uniqueLabel = part + "##" + part + "_0" + std::to_string(count);
                        else uniqueLabel = part + "##" + part + "_" + std::to_string(count);

                        if (ImGui::SmallButton(uniqueLabel.c_str())) {
                            NavigateTo(navPath);
                        }

                        if (i < parts.size() - 1) {
                            ImGui::SameLine(); ImGui::Text("/"); ImGui::SameLine();
                        }
                    }
                }
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            ImGui::PopStyleColor();
            ImGui::EndChild();

            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            if (ImGui::Button(ICON_FA_ANGLE_RIGHT "##ScrollRight", ImVec2(arrowBtnWidth, 0))) {
                breadcrumbScrollTarget += 100.0f;
            }
            ImGui::PopStyleColor();

            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.30f, 0.55f, 0.30f));

            if (GlowButton("##SortToggle", ImVec2(30, 0))) {
                showSortingOptions = !showSortingOptions;
            }

            if (ImGui::IsItemVisible()) {
                ImVec2 rectMin = ImGui::GetItemRectMin();
                ImVec2 rectMax = ImGui::GetItemRectMax();
                ImVec2 center = ImVec2((rectMin.x + rectMax.x) * 0.5f, (rectMin.y + rectMax.y) * 0.5f);

                const char* icon = showSortingOptions ? ICON_FA_XMARK : ICON_FA_ANGLE_DOWN;

                float iconOffsetX = showSortingOptions ? 1.0f : 0.0f;

                ImVec2 textSize = ImGui::CalcTextSize(icon);

                ImVec2 textPos = ImVec2(center.x - textSize.x * 0.5f + iconOffsetX, center.y - textSize.y * 0.5f);

                ImGui::GetWindowDrawList()->AddText(textPos, ImGui::GetColorU32(ImGuiCol_Text), icon);
            }
            ImGui::PopStyleColor();

            ImGui::EndGroup();

            if (showSortingOptions) {
                ImGui::Dummy(ImVec2(0, 2));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0.2f));
                ImGui::BeginChild("SortPanel", ImVec2(0, 35), false, ImGuiWindowFlags_NoScrollbar);

                ImGui::SetCursorPos(ImVec2(10, 5));
                ImGui::TextDisabled("Sort by: ");
                ImGui::SameLine();

                auto GetSortLabel = [](const char* base, int state) {
                    std::string s = base;
                    if (state == 1) s += " " ICON_FA_ANGLE_UP;
                    else if (state == 2) s += " " ICON_FA_ANGLE_DOWN;
                    else s += " -";
                    return s;
                    };

                bool isCustom = (g_SortTimeState == 0 && g_SortAlphaState == 0 && g_SortTypeState == 0);
                ImGui::PushStyleColor(ImGuiCol_Text, isCustom ? ImVec4(0.5f, 1.0f, 0.5f, 1.0f) : ImGui::GetStyle().Colors[ImGuiCol_Text]);
                if (GlowButton("Custom")) {
                    g_SortTimeState = 0;
                    g_SortAlphaState = 0;
                    g_SortTypeState = 0;
                    RefreshFS(currentViewPath, false);
                }
                ImGui::PopStyleColor();

                ImGui::SameLine();
                std::string typeLabel = GetSortLabel("Type", g_SortTypeState);
                if (GlowButton(typeLabel.c_str())) {
                    g_SortTypeState = (g_SortTypeState + 1) % 3;
                    RefreshFS(currentViewPath, true);
                }

                ImGui::SameLine();
                std::string alphaLabel = GetSortLabel("Alpha", g_SortAlphaState);
                if (GlowButton(alphaLabel.c_str())) {
                    g_SortAlphaState = (g_SortAlphaState + 1) % 3;
                    if (g_SortAlphaState != 0) g_SortTimeState = 0;
                    RefreshFS(currentViewPath, true);
                }

                ImGui::SameLine();
                std::string timeLabel = GetSortLabel("Time", g_SortTimeState);
                if (GlowButton(timeLabel.c_str())) {
                    g_SortTimeState = (g_SortTimeState + 1) % 3;
                    if (g_SortTimeState != 0) g_SortAlphaState = 0;
                    RefreshFS(currentViewPath, true);
                }

                if (folderViewAnim > 0.01f) {
                    ImGui::SameLine();

                    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, folderViewAnim);

                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                    if (GlowButton(ICON_FA_ARROWS_ROTATE " Reset Hyperspace")) {
                        showResetHyperspaceModal = true;
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset hidden files and custom ordering for this folder.");
                    ImGui::PopStyleColor();
                    ImGui::PopStyleVar();
                }

                ImGui::EndChild();
                ImGui::PopStyleColor();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            bool isCustomSort = (g_SortTimeState == 0 && g_SortAlphaState == 0 && g_SortTypeState == 0);
            bool allowDragReorder = isCustomSort;

            std::vector<int> currentDisplayIDs;

            if (viewingHome) {
                if (!isCustomSort) {
                    std::vector<DllEntry> sorted = dllList;

                    std::sort(sorted.begin(), sorted.end(), [](const DllEntry& a, const DllEntry& b) {
                        if (g_SortTypeState != 0) {
                            bool aFolder = a.isFolder; bool bFolder = b.isFolder;
                            if (aFolder != bFolder) return (g_SortTypeState == 2) ? (aFolder > bFolder) : (aFolder < bFolder);
                        }
                        if (g_SortTimeState != 0) {
                            return (g_SortTimeState == 1) ? a.dateAdded < b.dateAdded : a.dateAdded > b.dateAdded;
                        }
                        if (g_SortAlphaState != 0) {
                            std::string na = a.name; std::string nb = b.name;
                            std::transform(na.begin(), na.end(), na.begin(), ::tolower);
                            std::transform(nb.begin(), nb.end(), nb.begin(), ::tolower);
                            return (g_SortAlphaState == 1) ? na < nb : na > nb;
                        }
                        return a.id < b.id;
                        });

                    currentDisplayIDs.clear();
                    for (const auto& e : sorted) currentDisplayIDs.push_back(e.id);
                }
                else {
                    if (ds.visualOrder.size() != dllList.size()) {
                        ds.visualOrder.clear();
                        for (const auto& entry : dllList) ds.visualOrder.push_back(entry.id);
                    }
                    for (auto it = ds.visualOrder.begin(); it != ds.visualOrder.end(); ) {
                        bool exists = false;
                        for (const auto& d : dllList) if (d.id == *it) { exists = true; break; }
                        if (!exists) it = ds.visualOrder.erase(it);
                        else ++it;
                    }
                    for (const auto& d : dllList) {
                        bool found = false;
                        for (int vid : ds.visualOrder) if (vid == d.id) { found = true; break; }
                        if (!found) ds.visualOrder.push_back(d.id);
                    }
                    currentDisplayIDs = ds.visualOrder;
                }
            }
            else {
                currentDisplayIDs.clear();
                for (const auto& entry : fsList) currentDisplayIDs.push_back(entry.id);
            }

            std::vector<int> filteredVisualOrder;
            std::string searchQ = searchBuffer;
            std::transform(searchQ.begin(), searchQ.end(), searchQ.begin(),
                [](unsigned char c) { return std::tolower(c); });

            for (int id : currentDisplayIDs) {
                const DllEntry* entry = nullptr;
                for (const auto& d : *activeList) if (d.id == id) { entry = &d; break; }

                if (entry) {
                    if (searchQ.empty()) {
                        filteredVisualOrder.push_back(id);
                    }
                    else {
                        std::string n = entry->name;
                        std::string d = entry->description;
                        std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return std::tolower(c); });
                        std::transform(d.begin(), d.end(), d.begin(), [](unsigned char c) { return std::tolower(c); });

                        if (n.find(searchQ) != std::string::npos || d.find(searchQ) != std::string::npos) {
                            filteredVisualOrder.push_back(id);
                        }
                    }
                }
            }

            int cols = 4;
            float panelWidth = ImGui::GetContentRegionAvail().x;

            float cardWidth = (panelWidth - (cols * spacing)) / cols;
            if (cardWidth < 100.0f) { cardWidth = 100.0f; cols = (int)(panelWidth / 110.0f); }
            if (cols < 1) cols = 1;
            float cardHeight = 160.0f;

            ImVec2 gridStartPos = ImGui::GetCursorScreenPos();

            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered() && !ds.isDragging) {
                selectedDllId = -9999;
            }

            int targetSlotIndex = -1;
            if (ds.isDragging) {
                if (allowDragReorder) {
                    ImVec2 mousePos = ImGui::GetMousePos();
                    for (int i = 0; i < filteredVisualOrder.size(); i++) {
                        int r = i / cols;
                        int c = i % cols;
                        float tx = gridStartPos.x + c * (cardWidth + spacing);
                        float ty = gridStartPos.y + r * (cardHeight + spacing) - ImGui::GetScrollY();

                        if (mousePos.x >= tx && mousePos.x <= tx + cardWidth + spacing &&
                            mousePos.y >= ty && mousePos.y <= ty + cardHeight + spacing) {
                            targetSlotIndex = i;
                            break;
                        }
                    }

                    if (targetSlotIndex != -1 && targetSlotIndex < filteredVisualOrder.size() && searchQ.empty()) {

                        if (viewingHome) {
                            int currentIdx = -1;
                            for (int i = 0; i < ds.visualOrder.size(); i++)
                                if (ds.visualOrder[i] == ds.draggedId) { currentIdx = i; break; }

                            if (currentIdx != -1 && currentIdx != targetSlotIndex) {
                                int id = ds.visualOrder[currentIdx];
                                ds.visualOrder.erase(ds.visualOrder.begin() + currentIdx);
                                ds.visualOrder.insert(ds.visualOrder.begin() + targetSlotIndex, id);
                                filteredVisualOrder = ds.visualOrder;
                                if (viewingHome) {
                                    std::vector<DllEntry> newList;
                                    for (int vid : ds.visualOrder) {
                                        for (const auto& d : dllList) {
                                            if (d.id == vid) { newList.push_back(d); break; }
                                        }
                                    }
                                    dllList = newList;
                                    SaveConfig();
                                }
                            }
                        }
                        else {
                            int currentIdx = -1;
                            for (int i = 0; i < fsList.size(); i++) {
                                if (fsList[i].id == ds.draggedId) { currentIdx = i; break; }
                            }

                            if (currentIdx != -1 && currentIdx != targetSlotIndex) {
                                DllEntry item = fsList[currentIdx];
                                if (currentIdx < targetSlotIndex) {
                                    fsList.insert(fsList.begin() + targetSlotIndex + 1, item);
                                    fsList.erase(fsList.begin() + currentIdx);
                                }
                                else {
                                    fsList.insert(fsList.begin() + targetSlotIndex, item);
                                    fsList.erase(fsList.begin() + currentIdx + 1);
                                }

                                std::vector<std::string> newOrder;
                                for (const auto& f : fsList) newOrder.push_back(f.name);
                                g_FolderSortOrders[currentViewPath] = newOrder;
                                SaveConfig();
                            }
                        }
                    }
                }
            }

            int idToOpenPopup = -1;
            bool anyModalOpen = showEditModal || showAddDllModal || showImageModal || showEditPathModal || showErrorModal || showRenameModal || showSortErrorModal || showFileBrowserModal || showResetHyperspaceModal;

            for (int i = 0; i < filteredVisualOrder.size(); i++) {
                int id = filteredVisualOrder[i];

                DllEntry* entry = nullptr;
                for (auto& d : *activeList) if (d.id == id) { entry = &d; break; }
                if (!entry) continue;

                int r = i / cols;
                int c = i % cols;
                ImVec2 targetPos = ImVec2(
                    gridStartPos.x + c * (cardWidth + spacing),
                    gridStartPos.y + r * (cardHeight + spacing)
                );

                if (ds.currentPositions.find(id) == ds.currentPositions.end()) {
                    ds.currentPositions[id] = targetPos;
                }

                float dt = ImGui::GetIO().DeltaTime;
                ImVec2& currentPos = ds.currentPositions[id];
                currentPos.x = Lerp(currentPos.x, targetPos.x, dt * 10.0f);
                currentPos.y = Lerp(currentPos.y, targetPos.y, dt * 10.0f);

                bool isBeingDragged = (ds.isDragging && ds.draggedId == id);

                float btnSize = 24.0f;
                ImVec2 ctxBtnRelPos = ImVec2(cardWidth - btnSize - 5, cardHeight - btnSize - 5);
                ImVec2 ctxScreenPos = ImVec2(currentPos.x + ctxBtnRelPos.x, currentPos.y + ctxBtnRelPos.y);
                ImVec2 mousePos = ImGui::GetMousePos();
                bool hoveringCtx = (mousePos.x >= ctxScreenPos.x && mousePos.x <= ctxScreenPos.x + btnSize &&
                    mousePos.y >= ctxScreenPos.y && mousePos.y <= ctxScreenPos.y + btnSize);

                if (anyModalOpen) hoveringCtx = false;

                float alphaMult = isBeingDragged ? 0.3f : 1.0f;
                ImGui::SetCursorScreenPos(currentPos);

                ImGui::PushID(id);
                ImVec4 cardBg = ImVec4(0.08f, 0.09f, 0.13f, 0.40f * alphaMult);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, cardBg);

                ImGui::BeginChild("Card", ImVec2(cardWidth, cardHeight), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

                ImGui::SetCursorPos(ImVec2(10, 10));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0.3f * alphaMult));
                ImGui::BeginChild("IconBox", ImVec2(cardWidth - 20, 80), true, ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_NoScrollbar);

                bool cardImgDisplayed = false;
                if (entry->hasCustomImage && !entry->customImageName.empty()) {
                    auto it = g_ImageCache.find(entry->customImageName);
                    if (it != g_ImageCache.end() && it->second.texture) {
                        cardImgDisplayed = true;
                        float availW = cardWidth - 20;
                        float availH = 80.0f;
                        float imgAspect = (float)it->second.width / (float)it->second.height;
                        float drawH = availH;
                        float drawW = drawH * imgAspect;
                        if (drawW > availW) {
                            drawW = availW;
                            drawH = drawW / imgAspect;
                        }
                        ImGui::SetCursorPos(ImVec2((availW - drawW) * 0.5f, (availH - drawH) * 0.5f));
                        ImGui::Image((void*)it->second.texture, ImVec2(drawW, drawH));
                    }
                }

                if (!cardImgDisplayed) {
                    ImGui::SetCursorPos(ImVec2((cardWidth - 20) * 0.5f - 10, 30));

                    if (entry->isFolder) ImGui::Text(ICON_FA_FOLDER);
                    else if (entry->hasCustomImage) ImGui::Text(ICON_FA_IMG);
                    else ImGui::Text(ICON_FA_FILE_CODE);
                }

                ImGui::EndChild();
                ImGui::PopStyleColor();

                ImGui::SetCursorPos(ImVec2(5, 100));
                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + cardWidth - 10);
                ImGui::TextDisabled("%s", entry->name.c_str());
                ImGui::PopTextWrapPos();

                ImGui::SetCursorPos(ImVec2(0, 0));
                ImGui::InvisibleButton("CardInteract", ImVec2(cardWidth, cardHeight));

                bool isHovered = ImGui::IsItemHovered();
                bool isActive = ImGui::IsItemActive();

                if (isActive && !ds.isDragging && !isBeingDragged && !hoveringCtx) {
                    ImVec2 mouse = ImGui::GetMousePos();
                    if (ds.dragStartIndex == -9999) {
                        ds.dragStartIndex = i;
                        ds.dragStartMousePos = mouse;
                        ds.draggedId = id;
                    }
                    else {
                        float dX = mouse.x - ds.dragStartMousePos.x;
                        float dY = mouse.y - ds.dragStartMousePos.y;
                        if ((dX * dX + dY * dY) > 25.0f) {
                            if (!isCustomSort) {
                                showSortErrorModal = true;
                                ds.dragStartIndex = -9999;
                                ds.draggedId = -9999;
                            }
                            else {
                                ds.isDragging = true;
                            }
                        }
                    }
                }

                if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                    ds.dragStartIndex = i;
                    ds.dragStartMousePos = ImGui::GetMousePos();
                    ds.draggedId = id;
                }
                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                    if (!ds.isDragging && ds.draggedId == id && !hoveringCtx) {
                        if (entry->isFolder) {
                            NavigateTo(entry->path, entry->recursive);
                        }
                        else {
                            selectedDllId = id;
                        }
                    }
                    if (ds.draggedId == id) {
                        ds.isDragging = false;
                        ds.dragStartIndex = -9999;
                        ds.draggedId = -9999;
                    }
                }

                if (!ds.isDragging) {
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    if (hoveringCtx) {
                        drawList->AddCircleFilled(ImVec2(ctxScreenPos.x + btnSize / 2, ctxScreenPos.y + btnSize / 2), btnSize / 2, IM_COL32(255, 255, 255, 30));
                    }
                    ImU32 dotColor = IM_COL32(200, 200, 200, 255);
                    drawList->AddCircleFilled(ImVec2(ctxScreenPos.x + btnSize / 2, ctxScreenPos.y + btnSize / 2 - 5), 2.0f, dotColor);
                    drawList->AddCircleFilled(ImVec2(ctxScreenPos.x + btnSize / 2, ctxScreenPos.y + btnSize / 2), 2.0f, dotColor);
                    drawList->AddCircleFilled(ImVec2(ctxScreenPos.x + btnSize / 2, ctxScreenPos.y + btnSize / 2 + 5), 2.0f, dotColor);

                    if (hoveringCtx && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !anyModalOpen) {
                        idToOpenPopup = id;
                    }
                }

                ImGui::EndChild();
                ImGui::PopStyleColor();

                if (selectedDllId == id) {
                    ImGui::GetWindowDrawList()->AddRect(currentPos, ImVec2(currentPos.x + cardWidth, currentPos.y + cardHeight), IM_COL32(50, 150, 250, 150), 10.0f, 0, 2.0f);
                }

                ImGui::PopID();
            }

            int totalRows = (int)((filteredVisualOrder.size() + cols - 1) / cols);
            ImGui::SetCursorPosY(gridStartPos.y - ImGui::GetWindowPos().y + totalRows * (cardHeight + spacing));
            ImGui::Dummy(ImVec2(1, 1));

            if (ds.isDragging && ds.draggedId > -9000) {
                DllEntry* entry = nullptr;
                for (auto& d : *activeList) if (d.id == ds.draggedId) { entry = &d; break; }

                if (entry) {
                    ImVec2 mouse = ImGui::GetMousePos();
                    ImVec2 dragPos = ImVec2(mouse.x - cardWidth * 0.5f, mouse.y - cardHeight * 0.5f);
                    ImDrawList* fg = ImGui::GetForegroundDrawList();
                    fg->AddRectFilled(dragPos, ImVec2(dragPos.x + cardWidth, dragPos.y + cardHeight), IM_COL32(20, 17, 33, 230), 10.0f);
                    fg->AddRect(dragPos, ImVec2(dragPos.x + cardWidth, dragPos.y + cardHeight), IM_COL32(50, 150, 250, 200), 10.0f, 0, 2.0f);
                    ImVec2 iconBoxMin = ImVec2(dragPos.x + 10, dragPos.y + 10);
                    ImVec2 iconBoxMax = ImVec2(dragPos.x + cardWidth - 10, dragPos.y + 90);
                    fg->AddRectFilled(iconBoxMin, iconBoxMax, IM_COL32(0, 0, 0, 100), 8.0f);
                }
            }

            UpdateSmoothScroll("RightPanelSmooth");

            ImGui::EndChild();
            ImGui::PopStyleColor();

            if (idToOpenPopup != -1) {
                for (int k = 0; k < activeList->size(); k++) {
                    if ((*activeList)[k].id == idToOpenPopup) {
                        contextMenuTargetIndex = k;
                        break;
                    }
                }
                ImGui::OpenPopup("CardCtx");
            }

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
            ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.06f, 0.07f, 0.11f, 0.90f));

            if (ImGui::BeginPopup("CardCtx")) {
                if (contextMenuTargetIndex >= 0 && contextMenuTargetIndex < activeList->size()) {
                    DllEntry& target = (*activeList)[contextMenuTargetIndex];
                    ImGui::TextDisabled("%s", target.name.c_str());
                    ImGui::Separator();

                    if (ImGui::Selectable(ICON_FA_EDIT " Rename")) {
                        renameIndex = contextMenuTargetIndex;
                        strcpy_s(renameBuffer, target.name.c_str());
                        showRenameModal = true;
                        ImGui::CloseCurrentPopup();
                    }

                    if (ImGui::Selectable(ICON_FA_EDIT " Edit Desc")) {
                        editIndex = contextMenuTargetIndex;
                        strcpy_s(editDescBuffer, target.description.c_str());
                        showEditModal = true;
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::Selectable(ICON_FA_IMG " Change Image")) {
                        imageSelectionTargetIndex = contextMenuTargetIndex;
                        showImageModal = true;
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::Selectable(ICON_FA_CODE " Edit Path")) {
                        editPathIndex = contextMenuTargetIndex;
                        strcpy_s(editPathBuffer, target.path.c_str());
                        showEditPathModal = true;
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::Separator();

                    if (viewingHome) {
                        if (ImGui::Selectable(ICON_FA_TRASH " Remove")) {
                            if (selectedDllId == target.id) selectedDllId = -9999;

                            dllList.erase(dllList.begin() + contextMenuTargetIndex);
                            SaveConfig();
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    else {
                        if (ImGui::Selectable(ICON_FA_EYE_SLASH " Hide")) {
                            std::string pathToRemove = fsList[contextMenuTargetIndex].path;
                            g_FsIgnoreList.insert(pathToRemove);
                            SaveConfig();
                            RefreshFS(currentViewPath, true);
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }
                ImGui::EndPopup();
            }
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();

            if (showEditModal) {
                ImGui::OpenPopup("Edit Description");
                ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0, 0, 0, 0.0f));
            }

            ImGui::SetNextWindowBgAlpha(0.85f);
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

            if (ImGui::BeginPopupModal("Edit Description", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
                if (showEditModal) ImGui::PopStyleColor();
                ImGui::Text("Edit Description");
                ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.2f, 0.25f, 0.35f, 0.50f));
                ImGui::Separator();
                ImGui::PopStyleColor();
                ImGui::Spacing();
                ImGui::Text("Enter new description:");
                CustomMarqueeEditor("##DescEdit", editDescBuffer, sizeof(editDescBuffer), ImVec2(400, 100), false);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.3f, 0.55f, 0.40f));
                if (GlowButton("Save", ImVec2(120, 0))) {
                    if (editIndex >= 0 && editIndex < activeList->size()) {
                        if (viewingHome) {
                            (*activeList)[editIndex].description = std::string(editDescBuffer);
                            SaveConfig();
                        }
                    }
                    showEditModal = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (GlowButton("Cancel", ImVec2(120, 0))) {
                    showEditModal = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopStyleColor();
                ImGui::EndPopup();
            }
            else if (showEditModal) ImGui::PopStyleColor();

            if (showRenameModal) {
                ImGui::OpenPopup("Rename Item");
                ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0, 0, 0, 0.0f));
            }
            ImGui::SetNextWindowBgAlpha(0.85f);
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

            if (ImGui::BeginPopupModal("Rename Item", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
                if (showRenameModal) ImGui::PopStyleColor();

                ImGui::Text("Rename");
                ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.2f, 0.25f, 0.35f, 0.50f));
                ImGui::Separator();
                ImGui::PopStyleColor();
                ImGui::Spacing();

                if (!viewingHome) {
                    ImGui::TextColored(ImVec4(1, 1, 0, 1), "[!] Renaming a file here will add it to your Library.");
                }

                ImGui::Text("Enter new display name:");

                CustomMarqueeEditor("##RenameEdit", renameBuffer, sizeof(renameBuffer), ImVec2(400, 50), false);
                ImGui::Dummy(ImVec2(0, 10));

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.3f, 0.55f, 0.40f));
                if (GlowButton("Save", ImVec2(120, 0))) {
                    if (renameIndex >= 0 && renameIndex < activeList->size()) {
                        if (viewingHome) {
                            (*activeList)[renameIndex].name = std::string(renameBuffer);
                            SaveConfig();
                        }
                        else {
                            DllEntry& src = (*activeList)[renameIndex];
                            DllEntry newEntry = src;
                            newEntry.id = ++g_NextDllId;
                            newEntry.name = std::string(renameBuffer);

                            dllList.push_back(newEntry);
                            SaveConfig();
                            AddLog("Imported '" + src.name + "' as '" + newEntry.name + "' to Library.");
                        }
                    }
                    showRenameModal = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (GlowButton("Cancel", ImVec2(120, 0))) {
                    showRenameModal = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopStyleColor();
                ImGui::EndPopup();
            }
            else if (showRenameModal) ImGui::PopStyleColor();

            if (showAddDllModal) {
                ImGui::OpenPopup("Add Item");
                ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0, 0, 0, 0.0f));
            }
            ImGui::SetNextWindowBgAlpha(0.85f);
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

            if (ImGui::BeginPopupModal("Add Item", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
                if (showAddDllModal) ImGui::PopStyleColor();

                ImGui::Text("Add New Item");
                ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.2f, 0.25f, 0.35f, 0.50f));
                ImGui::Separator();
                ImGui::PopStyleColor();
                ImGui::Spacing();

                ImGui::Text("File or Folder Path:");
                CustomMarqueeEditor("##PathAdd", addDllPathBuffer, sizeof(addDllPathBuffer), ImVec2(400, 80), true);

                static bool bAddRecursive = true;
                ImGui::Spacing();
                SmoothCheckbox("Add recursive subfolders", &bAddRecursive);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("If unchecked, only DLLs in the top-level folder will be shown.");

                ImGui::Dummy(ImVec2(0, 10));

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.3f, 0.55f, 0.40f));
                if (GlowButton("Add", ImVec2(120, 0))) {
                    std::string newPath = addDllPathBuffer;
                    if (!newPath.empty()) {
                        if (IsValidPath(newPath)) {
                            std::string newName = GetSmartDllName(newPath);
                            bool isDir = std::filesystem::is_directory(newPath);

                            dllList.push_back({ ++g_NextDllId, newName, newPath, "No description provided.", GetCurrentTimestamp(), "", false, isDir, bAddRecursive });
                            SaveConfig();
                            showAddDllModal = false;
                            ImGui::CloseCurrentPopup();
                        }
                        else {
                            showAddDllModal = false;
                            showErrorModal = true;
                            errorReturnTarget = 1;
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }
                ImGui::SameLine();
                if (GlowButton("Cancel", ImVec2(120, 0))) {
                    showAddDllModal = false;
                    ImGui::CloseCurrentPopup();
                }

                ImGui::SameLine();
                if (GlowButton("Browse", ImVec2(120, 0))) {
                    fileBrowserTargetPtr = addDllPathBuffer;

                    std::string startPath = addDllPathBuffer;
                    if (startPath.empty() || !std::filesystem::exists(startPath)) {
                        startPath = std::filesystem::current_path().string();
                    }
                    else if (!std::filesystem::is_directory(startPath)) {
                        startPath = std::filesystem::path(startPath).parent_path().string();
                    }
                    RefreshFileBrowser(startPath);

                    fileBrowserReturnTarget = 1;
                    showAddDllModal = false;
                    ImGui::CloseCurrentPopup();

                    showFileBrowserModal = true;
                }

                ImGui::PopStyleColor();
                ImGui::EndPopup();
            }
            else if (showAddDllModal) ImGui::PopStyleColor();

            if (showEditPathModal) {
                ImGui::OpenPopup("Edit Path");
                ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0, 0, 0, 0.0f));
            }
            ImGui::SetNextWindowBgAlpha(0.85f);
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

            if (ImGui::BeginPopupModal("Edit Path", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
                if (showEditPathModal) ImGui::PopStyleColor();

                ImGui::Text("Edit Path");
                ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.2f, 0.25f, 0.35f, 0.50f));
                ImGui::Separator();
                ImGui::PopStyleColor();
                ImGui::Spacing();

                ImGui::Text("Update File Path:");
                CustomMarqueeEditor("##PathEdit", editPathBuffer, sizeof(editPathBuffer), ImVec2(400, 80), true);

                static bool bEditRecursive = true;
                static int lastEditIndex = -1;

                if (editPathIndex != lastEditIndex && editPathIndex >= 0 && editPathIndex < activeList->size()) {
                    bEditRecursive = (*activeList)[editPathIndex].recursive;
                    lastEditIndex = editPathIndex;
                }

                ImGui::Spacing();
                SmoothCheckbox("Add recursive subfolders", &bEditRecursive);

                ImGui::Dummy(ImVec2(0, 10));

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.3f, 0.55f, 0.40f));
                if (GlowButton("Save", ImVec2(120, 0))) {
                    if (editPathIndex >= 0 && editPathIndex < activeList->size()) {
                        if (viewingHome) {
                            std::string newPath = editPathBuffer;
                            if (IsValidPath(newPath)) {
                                (*activeList)[editPathIndex].path = newPath;
                                (*activeList)[editPathIndex].name = GetSmartDllName(newPath);
                                (*activeList)[editPathIndex].isFolder = std::filesystem::is_directory(newPath);
                                (*activeList)[editPathIndex].recursive = bEditRecursive;
                                SaveConfig();
                                showEditPathModal = false;
                                ImGui::CloseCurrentPopup();
                            }
                            else {
                                showEditPathModal = false;
                                showErrorModal = true;
                                errorReturnTarget = 2;
                                ImGui::CloseCurrentPopup();
                            }
                        }
                    }
                    else {
                        showEditPathModal = false;
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::SameLine();
                if (GlowButton("Cancel", ImVec2(120, 0))) {
                    showEditPathModal = false;
                    ImGui::CloseCurrentPopup();
                }

                ImGui::SameLine();
                if (GlowButton("Browse", ImVec2(120, 0))) {
                    fileBrowserTargetPtr = editPathBuffer;
                    std::string startPath = editPathBuffer;
                    if (startPath.empty() || !std::filesystem::exists(startPath)) {
                        startPath = std::filesystem::current_path().string();
                    }
                    else if (!std::filesystem::is_directory(startPath)) {
                        startPath = std::filesystem::path(startPath).parent_path().string();
                    }
                    RefreshFileBrowser(startPath);

                    fileBrowserReturnTarget = 2;
                    showEditPathModal = false;
                    ImGui::CloseCurrentPopup();

                    showFileBrowserModal = true;
                }

                ImGui::PopStyleColor();
                ImGui::EndPopup();
            }
            else if (showEditPathModal) ImGui::PopStyleColor();

            if (showFileBrowserModal) {
                if (!ImGui::IsPopupOpen("Browse Files")) ImGui::OpenPopup("Browse Files");
                ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0, 0, 0, 0.0f));
            }

            ImGui::SetNextWindowSize(ImVec2(500, 400));
            ImGui::SetNextWindowBgAlpha(0.85f);
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

            if (ImGui::BeginPopupModal("Browse Files", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar)) {
                if (showFileBrowserModal) ImGui::PopStyleColor();

                ImGui::Text("Browse Files");
                ImGui::SameLine();
                ImGui::TextDisabled("(%s)", fileBrowserCurrentPath.c_str());

                ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.2f, 0.25f, 0.35f, 0.50f));
                ImGui::Separator();
                ImGui::PopStyleColor();
                ImGui::Spacing();

                ImGui::InputTextWithHint("##FileSearch", "Search Files...", fileBrowserSearchBuffer, sizeof(fileBrowserSearchBuffer));
                ImGui::Spacing();

                ImGui::BeginChild("FileGrid", ImVec2(0, -40), true);
                int cols = (int)(ImGui::GetContentRegionAvail().x / 90.0f);
                if (cols < 1) cols = 1;
                ImGui::Columns(cols, "FileCols", false);

                std::string browserSearchQ = fileBrowserSearchBuffer;
                std::transform(browserSearchQ.begin(), browserSearchQ.end(), browserSearchQ.begin(), ::tolower);

                for (const auto& item : fileBrowserItems) {
                    if (!browserSearchQ.empty() && item.name != "..") {
                        std::string itemNameLower = item.name;
                        std::transform(itemNameLower.begin(), itemNameLower.end(), itemNameLower.begin(), ::tolower);
                        if (itemNameLower.find(browserSearchQ) == std::string::npos) continue;
                    }

                    ImGui::BeginGroup();
                    const char* icon = item.isFolder ? ICON_FA_FOLDER : ICON_FA_FILE_CODE;
                    if (item.name == "..") icon = ICON_FA_ARROW_UP;

                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.1f, 0.1f, 0.30f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.2f, 0.2f, 0.5f));
                    std::string btnId = "##fbtn_" + item.name;

                    if (ImGui::Button(btnId.c_str(), ImVec2(80, 80))) {
                        if (item.name == "..") {
                            std::filesystem::path p(fileBrowserCurrentPath);
                            if (p.has_parent_path()) RefreshFileBrowser(p.parent_path().string());
                        }
                        else if (item.isFolder) {
                            RefreshFileBrowser(item.path);
                        }
                        else {
                            if (fileBrowserTargetPtr) strcpy_s(fileBrowserTargetPtr, 512, item.path.c_str());
                            showFileBrowserModal = false;
                            ImGui::CloseCurrentPopup();
                            if (fileBrowserReturnTarget == 1) showAddDllModal = true;
                            if (fileBrowserReturnTarget == 2) showEditPathModal = true;
                        }
                    }

                    if (ImGui::IsItemVisible()) {
                        ImVec2 rectMin = ImGui::GetItemRectMin();
                        ImVec2 rectMax = ImGui::GetItemRectMax();
                        ImVec2 center = ImVec2((rectMin.x + rectMax.x) * 0.5f, (rectMin.y + rectMax.y) * 0.5f);

                        float iconOffsetX = -2.0f;
                        if (item.isFolder && item.name != "..") {
                            iconOffsetX = -4.0f;
                        }
                        ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), 30.0f, ImVec2(center.x - 10 + iconOffsetX, center.y - 15), IM_COL32(200, 200, 200, 255), icon);
                    }

                    ImGui::PopStyleColor(2);
                    ImGui::TextWrapped("%s", item.name.c_str());
                    ImGui::EndGroup();
                    ImGui::NextColumn();
                }
                ImGui::Columns(1);
                ImGui::EndChild();

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.3f, 0.55f, 0.40f));
                float btn1Width = 160.0f;
                float btn2Width = 100.0f;
                float spacing = ImGui::GetStyle().ItemSpacing.x;
                ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - (btn1Width + btn2Width + spacing));

                if (GlowButton("Select Current Folder", ImVec2(btn1Width, 0))) {
                    if (fileBrowserTargetPtr) strcpy_s(fileBrowserTargetPtr, 512, fileBrowserCurrentPath.c_str());
                    showFileBrowserModal = false;
                    ImGui::CloseCurrentPopup();
                    if (fileBrowserReturnTarget == 1) showAddDllModal = true;
                    if (fileBrowserReturnTarget == 2) showEditPathModal = true;
                }
                ImGui::SameLine();
                if (GlowButton("Cancel", ImVec2(btn2Width, 0))) {
                    showFileBrowserModal = false;
                    ImGui::CloseCurrentPopup();
                    if (fileBrowserReturnTarget == 1) showAddDllModal = true;
                    if (fileBrowserReturnTarget == 2) showEditPathModal = true;
                }
                ImGui::PopStyleColor();
                ImGui::EndPopup();
            }
            else if (showFileBrowserModal) ImGui::PopStyleColor();

            if (showImageModal) {
                ImGui::OpenPopup("Select Image");
                ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0, 0, 0, 0.0f));
            }
            ImGui::SetNextWindowSize(ImVec2(500, 400));
            ImGui::SetNextWindowBgAlpha(0.85f);
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

            if (ImGui::BeginPopupModal("Select Image", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar)) {
                if (showImageModal) ImGui::PopStyleColor();

                ImGui::Text("Select Image");
                ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.2f, 0.25f, 0.35f, 0.50f));
                ImGui::Separator();
                ImGui::PopStyleColor();
                ImGui::Spacing();

                ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "Tip: Put all images in IMG folder.");
                ImGui::Separator();
                ImGui::InputTextWithHint("##ImgSearch", "Search Images...", imageSearchBuffer, sizeof(imageSearchBuffer));
                ImGui::Spacing();

                ImGui::BeginChild("ImgGrid", ImVec2(0, -40), true);
                int imgCols = (int)(ImGui::GetContentRegionAvail().x / 90.0f);
                if (imgCols < 1) imgCols = 1;
                ImGui::Columns(imgCols, "ImgCols", false);

                std::string imgSearchQ = imageSearchBuffer;
                std::transform(imgSearchQ.begin(), imgSearchQ.end(), imgSearchQ.begin(), ::tolower);

                for (const auto& pair : g_ImageCache) {
                    const ImageAsset& img = pair.second;
                    if (!imgSearchQ.empty()) {
                        std::string imgNameLower = img.name;
                        std::transform(imgNameLower.begin(), imgNameLower.end(), imgNameLower.begin(), ::tolower);
                        if (imgNameLower.find(imgSearchQ) == std::string::npos) continue;
                    }

                    ImGui::BeginGroup();
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.1f, 0.1f, 0.30f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));

                    std::string btnId = "##imgBtn_" + img.name;
                    if (ImGui::ImageButton(btnId.c_str(), (void*)img.texture, ImVec2(80, 80))) {
                        if (imageSelectionTargetIndex >= 0 && imageSelectionTargetIndex < activeList->size()) {
                            if (viewingHome) {
                                (*activeList)[imageSelectionTargetIndex].customImageName = img.name;
                                (*activeList)[imageSelectionTargetIndex].hasCustomImage = true;
                                SaveConfig();
                            }
                        }
                        showImageModal = false;
                        ImGui::CloseCurrentPopup();
                    }

                    if (ImGui::IsItemHovered()) {
                        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(50, 150, 250, 150), 6.0f, 0, 2.0f);
                    }

                    ImGui::PopStyleColor(2);
                    ImGui::TextWrapped("%s", img.name.c_str());
                    ImGui::EndGroup();
                    ImGui::NextColumn();
                }
                ImGui::Columns(1);
                ImGui::EndChild();

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.3f, 0.55f, 0.40f));
                if (GlowButton("Cancel", ImVec2(120, 0))) {
                    showImageModal = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopStyleColor();
                ImGui::EndPopup();
            }
            else if (showImageModal) ImGui::PopStyleColor();

            if (showResetHyperspaceModal) {
                ImGui::OpenPopup("Reset Hyperspace");
                ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0, 0, 0, 0.0f));
            }
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowBgAlpha(0.95f);

            if (ImGui::BeginPopupModal("Reset Hyperspace", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
                if (showResetHyperspaceModal) ImGui::PopStyleColor();

                ImGui::Text(ICON_FA_ARROWS_ROTATE " Reset Hyperspace");
                ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.2f, 0.25f, 0.35f, 0.50f));
                ImGui::Separator();
                ImGui::PopStyleColor();
                ImGui::Spacing();

                ImGui::PushTextWrapPos(400.0f);
                ImGui::Text("Confirming this action will reset the linkage of this folder.");
                ImGui::TextDisabled("Any changes you've made, such as hiding folders and custom sorting, will be reset to default.");
                ImGui::PopTextWrapPos();

                ImGui::Dummy(ImVec2(0, 15));

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.15f, 0.15f, 0.60f));
                if (GlowButton("Confirm Reset", ImVec2(180, 0))) {

                    for (auto it = g_FsIgnoreList.begin(); it != g_FsIgnoreList.end(); ) {
                        if (it->find(currentViewPath) == 0) {
                            it = g_FsIgnoreList.erase(it);
                        }
                        else {
                            ++it;
                        }
                    }

                    g_FolderSortOrders.erase(currentViewPath);
                    SaveConfig();

                    RefreshFS(currentViewPath, false);

                    showResetHyperspaceModal = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopStyleColor();

                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.3f, 0.55f, 0.40f));
                if (GlowButton("Cancel", ImVec2(120, 0))) {
                    showResetHyperspaceModal = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopStyleColor();

                ImGui::EndPopup();
            }
            else if (showResetHyperspaceModal) ImGui::PopStyleColor();

            if (showErrorModal) {
                ImGui::OpenPopup("Invalid Path Error");
            }
            if (showSortErrorModal) {
                ImGui::OpenPopup("Sort Error");
            }

            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowBgAlpha(0.95f);
            ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

            if (ImGui::BeginPopupModal("Invalid Path Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
                showErrorModal = false;

                ImGui::Spacing();
                ImGui::Text("Path entered was not found or is invalid");
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                float winWidth = ImGui::GetWindowSize().x;
                ImGui::SetCursorPosX((winWidth - 60) * 0.5f);

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.3f, 0.55f, 0.40f));
                if (GlowButton("ok", ImVec2(60, 0))) {
                    ImGui::CloseCurrentPopup();

                    if (errorReturnTarget == 1) {
                        showAddDllModal = true;
                    }
                    else if (errorReturnTarget == 2) {
                        showEditPathModal = true;
                    }
                    errorReturnTarget = 0;
                }
                ImGui::PopStyleColor();

                ImGui::EndPopup();
            }

            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowBgAlpha(0.95f);
            if (ImGui::BeginPopupModal("Sort Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
                showSortErrorModal = false;

                ImGui::Spacing();
                ImGui::Text("Please switch to Custom sorting.");
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                float winWidth = ImGui::GetWindowSize().x;
                ImGui::SetCursorPosX((winWidth - 60) * 0.5f);

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.3f, 0.55f, 0.40f));
                if (GlowButton("ok", ImVec2(60, 0))) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopStyleColor();

                ImGui::EndPopup();
            }

            ImGui::PopStyleColor();

            ImGui::End();

            ImGui::Render();
            const float clear_color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
            g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            g_pSwapChain->Present(1, 0);
        }

        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        CleanupDeviceD3D();
        ::DestroyWindow(hWnd);
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    }
}

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
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext) != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    for (auto& pair : g_ImageCache) {
        if (pair.second.texture) pair.second.texture->Release();
    }
    g_ImageCache.clear();

    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    if (FAILED(g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer)))) return;
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
