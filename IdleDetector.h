// TimeTrack - 空闲检测与系统状态监测模块
// 职责：检测用户空闲时长、系统锁屏/屏保/睡眠、显示器关闭状态
// 使用 GetLastInputInfo / SystemParametersInfo / WTSRegisterSessionNotification

#pragma once
#include <windows.h>

class IdleDetector {
public:
    IdleDetector() = default;
    ~IdleDetector();

    IdleDetector(const IdleDetector&) = delete;
    IdleDetector& operator=(const IdleDetector&) = delete;

    // 初始化：若 hwndMsg 有效，注册会话通知（接收 WM_WTSSESSION_CHANGE）
    bool Initialize(HWND hwndMsg);

    // 获取用户空闲秒数（自上次鼠键输入起）
    // 使用 GetLastInputInfo，开销极低
    DWORD GetIdleSeconds() const;

    // 系统是否处于阻塞状态（任一满足即为阻塞）：
    //   - 工作站已锁定
    //   - 屏保正在运行
    //   - 会话已断开 / 远程桌面断开
    bool IsSystemBlocked() const;

    // 显示器是否已关闭（使用 GetDevicePowerState 或检测）
    bool IsDisplayOff() const;

    // 收到 WM_WTSSESSION_CHANGE 时调用，更新锁屏状态
    void OnSessionChange(WPARAM reason, bool sessionLocked);

    // 主动查询锁屏状态
    bool IsWorkstationLocked() const;

    // ---- 设置（供配置变更时即时更新） ----
    // 设置空闲检测超时（分钟），TimerEngine 在每次 Tick 时读取
    void SetTimeout(int minutes) { m_idleThresholdMinutes = minutes; }

    // 启用/禁用空闲检测
    // 禁用时，TimerEngine 始终认为用户活跃（不分空闲超时）
    void SetEnabled(bool enabled) { m_enabled = enabled; }

private:
    bool m_isLocked = false;
    int  m_idleThresholdMinutes = 5;  // 空闲超时（分钟）
    bool m_enabled = true;            // 是否启用空闲检测
};
