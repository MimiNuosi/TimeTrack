// TimeTrack - 空闲检测与系统状态监测实现
// 核心 API：
//   - GetLastInputInfo: 获取自上次用户输入以来的毫秒数
//   - SystemParametersInfo(SPI_GETSCREENSAVERRUNNING): 检测屏保
//   - OpenInputDesktop / SwitchDesktop 语义: 检测工作站锁定
//   - WTSRegisterSessionNotification: 接收锁屏/解锁、会话变化事件

#include "IdleDetector.h"
#include <wtsapi32.h>   // WTSRegisterSessionNotification 等

#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "user32.lib")

// ============================================================================
// 析构 — 反注册会话通知
// ============================================================================

IdleDetector::~IdleDetector() {
    // WTSUnRegisterSessionNotification 在 hwnd 销毁前调用
    // 由调用方负责确保 hwnd 生命周期正确
}

// ============================================================================
// Initialize — 注册会话通知
// ============================================================================

bool IdleDetector::Initialize(HWND hwndMsg) {
    // 注册会话变化通知：
    // NOTIFY_FOR_THIS_SESSION: 仅当前用户会话
    // 系统将向 hwndMsg 发送 WM_WTSSESSION_CHANGE 消息
    if (hwndMsg) {
        WTSRegisterSessionNotification(hwndMsg, NOTIFY_FOR_THIS_SESSION);
    }
    // 首次查询锁屏状态
    m_isLocked = IsWorkstationLocked();
    return true;
}

// ============================================================================
// GetIdleSeconds — 用户空闲时长
// ============================================================================

DWORD IdleDetector::GetIdleSeconds() const {
    LASTINPUTINFO lii = {};
    lii.cbSize = sizeof(LASTINPUTINFO);
    if (!GetLastInputInfo(&lii)) {
        return 0; // 调用失败，保守返回 0（假设用户活跃）
    }
    // lii.dwTime 是自系统启动以来的滴答数（GetTickCount 相同时间基准）
    DWORD tickCount = GetTickCount();
    // 处理 tickCount 回绕（49.7天一次，差值计算自然处理）
    DWORD idleMs = tickCount - lii.dwTime;
    return idleMs / 1000; // 转换为秒
}

// ============================================================================
// IsSystemBlocked — 综合判断系统是否阻塞
// ============================================================================

bool IdleDetector::IsSystemBlocked() const {
    // 1) 工作站锁定
    if (m_isLocked) return true;

    // 2) 屏保正在运行
    //    SPI_GETSCREENSAVERRUNNING: 返回 TRUE 表示屏保正在运行
    BOOL saverRunning = FALSE;
    if (SystemParametersInfoW(SPI_GETSCREENSAVERRUNNING, 0, &saverRunning, 0)) {
        if (saverRunning) return true;
    }

    return false;
}

// ============================================================================
// IsDisplayOff — 显示器是否关闭
// ============================================================================

bool IdleDetector::IsDisplayOff() const {
    // 方法：检查显示器是否处于低功耗状态
    // 向桌面窗口发送 WM_SYSCOMMAND + SC_MONITORPOWER 查询
    // 注意：这是一个间接方法，通过检测监视器超时判断
    // 实际实现：使用 GetDevicePowerState（但该 API 在用户模式下受限）

    // 简化方案：检测系统空闲超时设置
    // 若用户空闲时间 >= 显示器关闭超时，则认为显示器可能已关
    // 但这并不精确。更实际的做法是依赖 IsSystemBlocked 中的屏保检测
    // 来间接覆盖"关显示器"场景（屏保通常会在关显示器前启动）。

    // 对于Phase 2验证，返回 false（不做精确显示器检测）
    // TODO(Phase 5): 若有精确需求，可通过 PowerSettingNotification 注册显示器状态事件
    return false;
}

// ============================================================================
// OnSessionChange — WM_WTSSESSION_CHANGE 回调
// ============================================================================

void IdleDetector::OnSessionChange(WPARAM reason, bool sessionLocked) {
    // WTS_SESSION_LOCK   = 0x7: 工作站被锁定
    // WTS_SESSION_UNLOCK = 0x8: 工作站解锁
    switch (reason) {
    case WTS_SESSION_LOCK:
        m_isLocked = true;
        break;
    case WTS_SESSION_UNLOCK:
        m_isLocked = false;
        break;
    default:
        break;
    }
    // 也接受显式的 sessionLocked 参数作为备选
    if (sessionLocked) m_isLocked = true;
}

// ============================================================================
// IsWorkstationLocked — 主动查询锁屏状态
// ============================================================================

bool IdleDetector::IsWorkstationLocked() const {
    // 方法：尝试打开输入桌面，失败则锁定
    // OpenInputDesktop 在锁屏时会失败（仅允许特定进程访问安全桌面）
    HDESK hDesk = OpenInputDesktop(0, FALSE, GENERIC_READ);
    if (!hDesk) {
        // 无法打开输入桌面 = 工作站锁定 或 在安全桌面中
        return true;
    }
    CloseDesktop(hDesk);
    return false;
}
