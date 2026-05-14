// TimeTrack - 系统托盘管理实现
// 使用 Shell_NotifyIconW 管理图标
// TrackPopupMenu 实现右键菜单

#include "TrayManager.h"
#include "Resource.h"
#include <string>

// 托盘图标 UID（唯一标识）
static constexpr UINT TRAY_UID = 1;

// ============================================================================
// 析构 — 确保托盘图标被移除
// ============================================================================

TrayManager::~TrayManager() {
    Remove();
}

// ============================================================================
// Initialize — 创建托盘图标
// ============================================================================

bool TrayManager::Initialize(HWND hwndOwner, HINSTANCE hInst, TrayCommandCallback onCommand) {
    m_hwndOwner = hwndOwner;
    m_hInst     = hInst;
    m_onCommand = std::move(onCommand);

    // 加载自定义图标
    HICON hIcon = LoadIconW(m_hInst, MAKEINTRESOURCEW(IDI_TIMETRACK));
    if (!hIcon) {
        // 自定义图标加载失败（资源不存在或损坏），回退到系统默认
        hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }

    // 填充 NOTIFYICONDATAW 结构
    // NIF_ICON:   显示图标
    // NIF_MESSAGE: 图标区域事件发送到 hWnd 的 uCallbackMessage
    // NIF_TIP:    鼠标悬停时显示提示文本
    ZeroMemory(&m_nid, sizeof(m_nid));
    m_nid.cbSize = sizeof(NOTIFYICONDATAW);
    m_nid.hWnd   = hwndOwner;
    m_nid.uID    = TRAY_UID;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_APP_TRAYICON;
    m_nid.hIcon  = hIcon;
    wcscpy_s(m_nid.szTip, L"TimeTrack");

    BOOL result = Shell_NotifyIconW(NIM_ADD, &m_nid);
    if (!result) {
        // 添加第二版（某些系统需要更大的结构）
        m_nid.cbSize = sizeof(NOTIFYICONDATAW) - sizeof(m_nid.guidItem);
        result = Shell_NotifyIconW(NIM_ADD, &m_nid);
        if (!result) {
            // 回退：使用老版本结构（兼容 Windows XP，虽然我们目标 Win10+）
            m_nid.cbSize = NOTIFYICONDATAW_V2_SIZE;
            result = Shell_NotifyIconW(NIM_ADD, &m_nid);
        }
    }

    return result != FALSE;
}

// ============================================================================
// UpdateTooltip — 更新悬停提示文本
// ============================================================================

void TrayManager::UpdateTooltip(const std::wstring& text) {
    m_nid.uFlags = NIF_TIP;
    wcsncpy_s(m_nid.szTip, _countof(m_nid.szTip), text.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}

// ============================================================================
// ShowContextMenu — 构建并显示右键弹出菜单
// ============================================================================

void TrayManager::ShowContextMenu(HWND hwnd) {
    // 获取鼠标位置
    POINT pt;
    GetCursorPos(&pt);

    // 创建弹出菜单
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    // 菜单项（根据暂停状态动态切换文本）
    AppendMenuW(hMenu, MF_STRING | (m_paused ? 0 : MF_DEFAULT),
        IDM_OPEN_PANEL, L"Open Statistics Panel\tClick");

    AppendMenuW(hMenu, MF_STRING,
        IDM_TOGGLE_PAUSE,
        m_paused ? L"Resume Statistics" : L"Pause Statistics");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(hMenu, MF_STRING,
        IDM_SETTINGS, L"Settings...");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(hMenu, MF_STRING,
        IDM_EXIT, L"Exit");

    // 设置默认项加粗显示
    SetMenuDefaultItem(hMenu, m_paused ? IDM_TOGGLE_PAUSE : IDM_OPEN_PANEL, FALSE);

    // 显示菜单
    // TPM_RIGHTALIGN:  右键弹出时右对齐（更自然）
    // TPM_BOTTOMALIGN: 底部对齐
    //
    // 注：必须先调用 SetForegroundWindow 确保菜单能正确关闭
    // 然后发送 WM_NULL 消息使菜单能够响应点击外部关闭
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu,
        TPM_RIGHTALIGN | TPM_BOTTOMALIGN,
        pt.x, pt.y,
        0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0); // 使菜单可被点击外部关闭

    DestroyMenu(hMenu);
}

// ============================================================================
// Remove — 从任务栏移除托盘图标
// ============================================================================

void TrayManager::Remove() {
    if (m_nid.hWnd) {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        m_nid.hWnd = nullptr;
    }
}
