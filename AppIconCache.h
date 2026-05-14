// TimeTrack - 应用图标缓存模块
// 职责：通过 SHGetFileInfoW 提取 exe 小图标，维护 HIMAGELIST + 索引缓存
// 接口简洁，无外部依赖（仅 Windows API）

#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <unordered_map>

namespace AppIconCache {

// 初始化：创建 16×16 ImageList，加载默认图标作为索引 0
// 在 PanelUI::Initialize 中调用（ListView 创建后）
void Init();

// 获取或加载指定 exe 路径的图标索引
// path: 进程完整路径（如 C:\...\chrome.exe）
// 返回: ImageList 索引，失败返回 0（默认图标）
int  GetOrLoadIcon(const std::wstring& path);

// 获取 HIMAGELIST 句柄（供 ListView_SetImageList 绑定）
HIMAGELIST GetImageList();

// 清理：销毁 ImageList，释放所有资源，清空缓存
// 在 PanelUI::Cleanup 中调用（WM_DESTROY 时）
void Cleanup();

// 重建图标缓存：销毁旧的 ImageList 并重建
// 用于窗口重新显示时刷新图标，或修复图标显示错误
// 调用后需重新绑定 ListView 的 ImageList（由 PanelUI 处理）
void RebuildCache();

} // namespace AppIconCache
