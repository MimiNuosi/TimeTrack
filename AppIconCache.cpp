// TimeTrack - 应用图标缓存实现
// 使用 SHGetFileInfoW 从进程 exe 提取 16×16 小图标
// 内部维护 HIMAGELIST + unordered_map 缓存，Icon 生命周期由 ImageList 托管
// 诊断日志：首次遇到每个 exePath 时输出提取结果
// PathFileExistsW 前置检查：文件不存在直接返回默认图标

#include "AppIconCache.h"
#include <shellapi.h>  // SHGetFileInfoW, SHFILEINFOW, SHGFI_*
#include <shlwapi.h>   // PathFileExistsW
#include <set>

#pragma comment(lib, "shlwapi.lib")

namespace AppIconCache {

// ---- 内部状态 ----
static HIMAGELIST g_hImageList = nullptr;
static std::unordered_map<std::wstring, int> g_iconCache;

// ---- 诊断去重集合（仅用于首次日志输出） ----
static std::set<std::wstring> g_diagIconSeenPaths;

// 图标尺寸（与 LVSIL_SMALL 匹配）
static const int ICON_CX = 16;
static const int ICON_CY = 16;

// ============================================================================
// ExtractIcon — 从 exe 路径提取 HICON
// 返回: 成功返回 HICON，失败返回 nullptr
// ============================================================================
static HICON ExtractIconFromPath(const std::wstring& path) {
    SHFILEINFOW sfi = {};
    DWORD_PTR result = SHGetFileInfoW(
        path.c_str(),
        0,
        &sfi,
        sizeof(SHFILEINFOW),
        SHGFI_SMALLICON | SHGFI_ICON
    );
    if (result == 0) {
        return nullptr;
    }
    return sfi.hIcon;
}

// ============================================================================
// Init — 创建 ImageList + 默认图标
// ============================================================================
void Init() {
    if (g_hImageList) return;

    g_hImageList = ImageList_Create(ICON_CX, ICON_CY, ILC_COLOR32, 0, 64);
    if (!g_hImageList) return;

    // 加载默认图标（索引 0）：系统标准应用程序图标 IDI_APPLICATION
    HICON hDefaultIcon = LoadIconW(nullptr, IDI_APPLICATION);
    if (hDefaultIcon) {
        int idx = ImageList_AddIcon(g_hImageList, hDefaultIcon);
        if (idx >= 0) {
            g_iconCache[L"__default__"] = idx;
            return;
        }
    }

    // 极端回退：创建一个空白图标
    HICON hBlank = CreateIcon(nullptr, ICON_CX, ICON_CY, 1, 1, nullptr, nullptr);
    if (hBlank) {
        int idx = ImageList_AddIcon(g_hImageList, hBlank);
        DestroyIcon(hBlank);
        if (idx >= 0) {
            g_iconCache[L"__default__"] = idx;
        }
    }
}

// ============================================================================
// GetOrLoadIcon — 获取缓存图标索引（含路径有效性检查 + 诊断）
// ============================================================================
int GetOrLoadIcon(const std::wstring& path) {
    if (path.empty() || !g_hImageList) {
        return 0;
    }

    // 1. 查缓存
    auto it = g_iconCache.find(path);
    if (it != g_iconCache.end()) {
        return it->second;
    }

    // ---- 诊断：首次遇到此路径 ----
    bool isFirstTime = g_diagIconSeenPaths.insert(path).second;
    if (isFirstTime) {
        std::wstring dbg = L"[AppIcon] GetOrLoadIcon path='";
        dbg += path;
        dbg += L"'\n";
        OutputDebugStringW(dbg.c_str());
    }

    // 2. 前置检查：文件是否真实存在
    //    对于系统进程（无路径）、UWP已卸载应用、"pid:"伪路径等，
    //    跳过 SHGetFileInfoW 直接返回默认图标，避免未定义行为
    if (!PathFileExistsW(path.c_str())) {
        g_iconCache[path] = 0;
        {
            std::wstring warn = L"[ICON FAIL] File not found: '";
            warn += path;
            warn += L"'\n";
            OutputDebugStringW(warn.c_str());
        }
        return 0;
    }

    // 3. 从 exe 提取图标
    HICON hIcon = ExtractIconFromPath(path);
    if (!hIcon) {
        g_iconCache[path] = 0;
        {
            std::wstring warn = L"[ICON FAIL] SHGetFileInfoW failed for '";
            warn += path;
            warn += L"'\n";
            OutputDebugStringW(warn.c_str());
        }
        return 0;
    }

    // 4. 添加到 ImageList
    int idx = ImageList_AddIcon(g_hImageList, hIcon);
    DestroyIcon(hIcon); // 图标已拷贝到 ImageList，释放原始 HICON

    if (idx < 0) {
        g_iconCache[path] = 0;
        {
            std::wstring warn = L"[ICON FAIL] ImageList_AddIcon failed for '";
            warn += path;
            warn += L"'\n";
            OutputDebugStringW(warn.c_str());
        }
        return 0;
    }

    // 5. 缓存成功索引
    g_iconCache[path] = idx;
    if (isFirstTime) {
        std::wstring dbg = L"[AppIcon] Icon OK idx=";
        dbg += std::to_wstring(idx);
        dbg += L" for '";
        dbg += path;
        dbg += L"'\n";
        OutputDebugStringW(dbg.c_str());
    }
    return idx;
}

// ============================================================================
// GetImageList — 返回 ImageList 句柄
// ============================================================================
HIMAGELIST GetImageList() {
    return g_hImageList;
}

// ============================================================================
// RebuildCache — 重建整个图标缓存（Cleanup + Init）
// 用于窗口重新显示时刷新图标，或修复图标错误
// ============================================================================
void RebuildCache() {
    Cleanup();   // 销毁旧 ImageList + 清空 g_iconCache
    Init();      // 重建 ImageList，索引 0 = IDI_APPLICATION
}

// ============================================================================
// Cleanup — 销毁 ImageList，清空缓存
// ============================================================================
void Cleanup() {
    if (g_hImageList) {
        ImageList_Destroy(g_hImageList);
        g_hImageList = nullptr;
    }
    g_iconCache.clear();
}

} // namespace AppIconCache
