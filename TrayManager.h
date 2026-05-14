// TimeTrack - 系统托盘管理模块
// 职责：托盘图标创建/删除、右键菜单构建与弹出、提示文本更新
// 使用 Shell_NotifyIcon API

#pragma once
#include <windows.h>
#include <string>
#include <functional>

// 托盘命令枚举（用于回调通知主窗口）
enum class TrayCommand {
    OpenPanel,
    TogglePause,
    Settings,
    Exit
};

using TrayCommandCallback = std::function<void(TrayCommand)>;

class TrayManager {
public:
    TrayManager() = default;
    ~TrayManager();

    TrayManager(const TrayManager&) = delete;
    TrayManager& operator=(const TrayManager&) = delete;

    // 初始化托盘图标
    // hwndOwner: 接收托盘消息的窗口句柄
    // hInst:     应用实例句柄（用于加载资源图标）
    // onCommand: 菜单命令回调（由主 WndProc 提供）
    // 返回 true 表示托盘图标创建成功
    bool Initialize(HWND hwndOwner, HINSTANCE hInst, TrayCommandCallback onCommand);

    // 更新托盘图标悬停提示文本
    void UpdateTooltip(const std::wstring& text);

    // 设置暂停状态（影响提示文本和下次菜单构建）
    void SetPausedState(bool paused) { m_paused = paused; }

    // 构建并显示右键菜单
    // 菜单命令将触发 onCommand 回调
    void ShowContextMenu(HWND hwnd);

    // 从任务栏移除托盘图标
    void Remove();

private:
    NOTIFYICONDATAW      m_nid = {};
    HWND                 m_hwndOwner = nullptr;
    HINSTANCE            m_hInst = nullptr;
    TrayCommandCallback  m_onCommand;
    bool                 m_paused = false; // 统计是否暂停（影响菜单文本）
};
