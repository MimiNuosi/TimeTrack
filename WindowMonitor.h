// TimeTrack - 前台窗口监测模块
// 职责：获取当前前台窗口的进程路径、标题和状态（焦点/最小化）
// 支持两种模式：事件驱动（SetWinEventHook）和轮询（Refresh）

#pragma once
#include "Types.h"
#include <functional>

using WindowChangeCallback = std::function<void(const WindowInfo& oldWin, const WindowInfo& newWin)>;

class WindowMonitor {
public:
    WindowMonitor() = default;
    ~WindowMonitor();

    // 禁止拷贝
    WindowMonitor(const WindowMonitor&) = delete;
    WindowMonitor& operator=(const WindowMonitor&) = delete;

    // 初始化
    // - 若 hwndMsg 有效：注册 WinEventHook，事件驱动更新
    // - 若 hwndMsg 为 null：仅支持轮询模式，需调用方定期调用 Refresh()
    // - onChanged: 窗口变化回调（可选）
    bool Initialize(HWND hwndMsg, WindowChangeCallback onChanged = nullptr);

    // 获取当前窗口快照
    const WindowInfo& GetCurrentWindow() const { return m_current; }

    // 主动轮询当前前台窗口（控制台模式或首次获取时调用）
    void Refresh();

private:
    // 从窗口句柄构建完整 WindowInfo
    static WindowInfo BuildWindowInfo(HWND hwnd);

    // WinEvent 静态回调 → 转为成员调用
    static void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd,
        LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime);

    void OnForegroundChanged(HWND hwndNew);

    HWINEVENTHOOK       m_hEventHook = nullptr;
    WindowInfo          m_current;
    WindowChangeCallback m_onChanged;
};
